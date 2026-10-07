// ARM device-binary execution with SDK API emulation. The game thread owns this
// object, including Lua calls; the UI only uses the existing host command queue.
extern "C" {
#include "pd_runtime.h"
#include "pd_native.h"
#include "pd_pdx.h"
#include "crank_host.h"
}
#include <unicorn/unicorn.h>
#include <unicorn/arm.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <ctime>
#include <cmath>
#include <set>
#include <filesystem>
#include <cerrno>

namespace {
constexpr uint32_t CodeBase=0x60000000, CodeAddress=CodeBase+32;
constexpr uint32_t ApiBase=0x70000000, TrapBase=0x80000000;
constexpr uint32_t StackBase=0x90000000, HeapBase=0xa0000000;
constexpr uint32_t HeapSize=16*1024*1024, StackSize=64*1024, ReturnAddress=TrapBase+0xf000;
struct ApiTableDef { const char *name; uint32_t count; };
struct ApiFieldDef { uint32_t table, slot; int child, function; };
struct ApiFunctionDef { const char *name, *result, *arguments; };
#include "pd_native_api.inc"
struct NativeError:std::runtime_error { using std::runtime_error::runtime_error; };
struct Cancelled {};
uint32_t u32(const uint8_t *p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
float f32(uint32_t v) { float f; memcpy(&f,&v,4); return f; }
uint32_t floatBits(float f) { uint32_t v; memcpy(&v,&f,4); return v; }
struct Resource { std::string type; int ref=LUA_NOREF; uint32_t userdata=0; bool weak=false; int retained=LUA_NOREF,count=0; uint32_t stringData=0; std::array<uint32_t,3> callbacks{}; };
struct Mirror { LCDBitmap *bitmap; uint32_t data,mask,stride,size; bool screen; };
struct Args;
struct SoundCallback {uint32_t callback=0,userdata=0;bool playing=false;};

class Native {
public:
    uc_engine *cpu=nullptr;
    lua_State *root,*context;
    std::vector<uint8_t> code,heap,stack,api,traps;
    std::map<uint32_t,uint32_t> blocks;
    std::map<uint32_t,Resource> objects;
    std::vector<Mirror> mirrors;
    uint32_t entry=0,update=0,updateData=0,buttonCallback=0,buttonData=0;
    uint32_t apiRoot=ApiBase,elapsedStart=0; int crankSounds=1;
    int trap=-1,depth=0; bool terminating=false;
    uint64_t calls=0,apiCalls=0; std::string lastApi;
    std::set<std::string> usedApis;
    std::map<uint32_t,SoundCallback> sounds;
    std::string fileError;
    uint32_t fileErrorData=0;
    uint32_t displayFrame=0;
    std::vector<uint32_t> temporaryStrings;
    std::vector<int> argumentCounts;
    explicit Native(lua_State *L):root(L),context(L),heap(HeapSize),stack(StackSize),api(16384),traps(65536) {
        try {
        check(uc_open(UC_ARCH_ARM,UC_MODE_THUMB,&cpu));
        check(uc_ctl_set_cpu_model(cpu,UC_CPU_ARM_CORTEX_M7));
        map(HeapBase,heap,UC_PROT_READ|UC_PROT_WRITE);
        map(StackBase,stack,UC_PROT_READ|UC_PROT_WRITE);
        // Build table data before mapping it read-only.
        std::vector<uint32_t> addresses; uint32_t cursor=0;
        for(const auto &t:native_tables) {addresses.push_back(ApiBase+cursor); cursor+=t.count*4;}
        if(cursor>api.size()) throw NativeError("SDK function tables exceed API region");
        for(const auto &f:native_fields) {
            uint32_t value=f.child>=0 ? addresses[f.child] : f.function>=0 ? (TrapBase+f.function*4)|1 : 0;
            if(f.function>=0 && !strcmp(native_functions[f.function].name,"graphics.getDebugBitmap")) value=0;
            memcpy(api.data()+addresses[f.table]-ApiBase+f.slot*4,&value,4);
        }
        map(ApiBase,api,UC_PROT_READ);
        // Only API/return addresses are executable here, and every visit is
        // stopped by the hook before executing a host dispatch on the same thread.
        for(size_t i=0;i<traps.size();i+=2) {traps[i]=0x70; traps[i+1]=0x47;}
        map(TrapBase,traps,UC_PROT_READ|UC_PROT_EXEC);
        uc_hook hook; check(uc_hook_add(cpu,&hook,UC_HOOK_CODE,(void*)onTrap,this,TrapBase,TrapBase+traps.size()-1));
        uint32_t cpacr=0xf00000; check(uc_reg_write(cpu,UC_ARM_REG_C1_C0_2,&cpacr));
        elapsedStart=(uint32_t)pd_time_milliseconds();
        lua_newtable(L); lua_newtable(L); lua_pushliteral(L,"v"); lua_setfield(L,-2,"__mode"); lua_setmetatable(L,-2);
        lua_setfield(L,LUA_REGISTRYINDEX,"crank.native.weak");
        } catch(...) {if(cpu) uc_close(cpu);cpu=nullptr;throw;}
    }
    ~Native() {
        for(auto &o:objects) {
            if(o.second.type=="sample" || o.second.type=="sampleplayer" || o.second.type=="fileplayer"){context=root;pushResource(o.second);pd_sound_native_dispose(root,-1);lua_pop(root,1);}
            if(o.second.weak) {lua_getfield(root,LUA_REGISTRYINDEX,"crank.native.weak");luaL_unref(root,-1,o.second.ref);lua_pop(root,1);}
            else luaL_unref(root,LUA_REGISTRYINDEX,o.second.ref);
            if(o.second.retained!=LUA_NOREF) luaL_unref(root,LUA_REGISTRYINDEX,o.second.retained);
        }
        if(cpu) uc_close(cpu);
    }
    void check(uc_err err) { if(err!=UC_ERR_OK) throw NativeError(std::string(uc_strerror(err))+" ("+lastApi+")"); }
    void map(uint32_t address,std::vector<uint8_t>& data,uint32_t permissions) { check(uc_mem_map_ptr(cpu,address,data.size(),permissions,data.data())); }
    uint32_t reg(int id) { uint32_t v; check(uc_reg_read(cpu,id,&v)); return v; }
    void reg(int id,uint32_t v) { check(uc_reg_write(cpu,id,&v)); }
    uint8_t *memory(uint32_t addr,size_t count) {
        struct Region {uint32_t base; std::vector<uint8_t>*data;};
        for(auto r:{Region{HeapBase,&heap},Region{StackBase,&stack},Region{CodeBase,&code},Region{ApiBase,&api}})
            if(addr>=r.base && uint64_t(addr)-r.base<=r.data->size() && count<=r.data->size()-(addr-r.base)) return r.data->data()+addr-r.base;
        char msg[128]; snprintf(msg,sizeof(msg),"Invalid guest memory 0x%08x length %zu (%s)",addr,count,lastApi.c_str()); throw NativeError(msg);
    }
    template<class T>T read(uint32_t addr) {T v; memcpy(&v,memory(addr,sizeof(T)),sizeof(T)); return v;}
    template<class T>void write(uint32_t addr,T v) {if(addr) memcpy(writable(addr,sizeof(T)),&v,sizeof(T));}
    uint8_t *writable(uint32_t address,size_t count) {
        if(address>=ApiBase && uint64_t(address)<uint64_t(ApiBase)+api.size()) throw NativeError("Write to read-only SDK table");
        return memory(address,count);
    }
    std::string string(uint32_t address,size_t limit=1024*1024) {
        if(!address) return {};
        auto p=memory(address,1); size_t n=0;
        while(n<limit) { if(!*memory(address+(uint32_t)n,1)) return std::string((char*)p,n); n++; }
        throw NativeError("Unterminated guest string");
    }
    uint32_t allocate(uint32_t size) {
        if(size>HeapSize-32) throw NativeError("Native heap allocation exceeds 16 MB");
        size=std::max(8u,(size+7)&~7u); uint32_t address=HeapBase+16;
        for(auto b:blocks) { if(uint64_t(address)+size<=b.first) break; address=b.first+b.second; }
        if(uint64_t(address)+size>uint64_t(HeapBase)+HeapSize) throw NativeError("Native heap exhausted");
        blocks.emplace(address,size); memset(memory(address,size),0,size); return address;
    }
    uint32_t copyString(const std::string &s) {uint32_t p=allocate((uint32_t)s.size()+1); memcpy(memory(p,s.size()+1),s.c_str(),s.size()+1); return p;}
    uint32_t resize(uint32_t p,uint32_t size) {
        if(!p) return size ? allocate(size) : 0;
        auto it=blocks.find(p); if(it==blocks.end() || objects.count(p)) throw NativeError("Invalid realloc pointer");
        if(!size) {blocks.erase(it); return 0;}
        if(size<=it->second) return p;
        uint32_t q=allocate(size); memcpy(memory(q,size),memory(p,it->second),it->second); blocks.erase(p); return q;
    }
    static void onTrap(uc_engine *cpu,uint64_t address,uint32_t,void *self) {
        auto &n=*(Native*)self; n.trap=address==ReturnAddress ? -2 : int((address-TrapBase)/4); uc_emu_stop(cpu);
    }
    void sync(bool fromGuest) {
        for(auto &m:mirrors) {
            LCDBitmap *bm=m.screen ? pd_screen_bitmap : m.bitmap;
            for(int y=0;y<bm->height;y++) for(int x=0;x<bm->rowbytes;x++) {
                auto &host=bm->data[y*bm->rowbytes+x]; auto &guest=*memory(m.data+y*m.stride+x,1);
                if(fromGuest) host=~guest; else guest=~host;
                if(m.mask && bm->mask) {auto &mask=*memory(m.mask+y*m.stride+x,1); if(fromGuest) bm->mask[y*bm->rowbytes+x]=mask; else mask=bm->mask[y*bm->rowbytes+x];}
            }
        }
    }
    Mirror &mirror(LCDBitmap *bitmap,bool screen=false) {
        for(auto &m:mirrors) if(m.bitmap==bitmap) return m;
        uint32_t stride=screen ? 52u : (uint32_t)bitmap->rowbytes;
        uint32_t size=stride*bitmap->height; uint32_t data=allocate(size),mask=bitmap->mask ? allocate(size) : 0;
        mirrors.push_back({bitmap,data,mask,stride,size,screen}); sync(false); return mirrors.back();
    }
    uint32_t invoke(uint32_t function,const std::vector<uint32_t> &args={},int timeoutMs=2000,const std::vector<float> &floatArgs={}) {
        if(++depth>32) {--depth; throw NativeError("Native callback nesting exceeds 32");}
        uc_context *saved=nullptr;
        try {
            check(uc_context_alloc(cpu,&saved)); check(uc_context_save(cpu,saved));
            sync(false); uint32_t sp=depth==1 ? StackBase+StackSize-16 : (reg(UC_ARM_REG_SP)-256)&~7u;
            if(args.size()>4) sp-=uint32_t((args.size()-4)*4+7)&~7u;
            for(size_t i=0;i<args.size();i++) if(i<4) reg(UC_ARM_REG_R0+int(i),args[i]); else write(sp+uint32_t((i-4)*4),args[i]);
            if(floatArgs.size()>16)throw NativeError("Too many callback VFP arguments");
            for(size_t i=0;i<floatArgs.size();i++)reg(UC_ARM_REG_S0+(int)i,floatBits(floatArgs[i]));
            reg(UC_ARM_REG_SP,sp); reg(UC_ARM_REG_LR,ReturnAddress|1); uint32_t pc=function|1;
            auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
            calls++;
            for(;;) {
                if(!terminating && pd_host_should_stop()) throw Cancelled{};
                if(std::chrono::steady_clock::now()>until) throw NativeError("Native execution exceeded watchdog time budget");
                trap=-1; auto result=uc_emu_start(cpu,pc,0,5000,200000); pc=reg(UC_ARM_REG_PC)|1;
                if(result!=UC_ERR_OK) {
                    char msg[200]; snprintf(msg,sizeof(msg),"ARM execution failed at PC=0x%08x LR=0x%08x: %s; last API=%s",pc,reg(UC_ARM_REG_LR),uc_strerror(result),lastApi.c_str()); throw NativeError(msg);
                }
                if(trap==-2) break;
                if(trap>=0) {sync(true); dispatch(trap); sync(false); pc=reg(UC_ARM_REG_LR)|1;}
            }
            sync(true); uint32_t result=reg(UC_ARM_REG_R0); check(uc_context_restore(cpu,saved)); uc_context_free(saved); --depth; return result;
        } catch(...) {if(saved) {uc_context_restore(cpu,saved); uc_context_free(saved);} --depth; throw;}
    }
    void load(const char *directory) {
        std::string path=std::string(directory)+"/pdex.bin"; FILE *f=fopen(path.c_str(),"rb"); if(!f) throw NativeError("Cannot open pdex.bin");
        fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
        if(size<48 || size>64*1024*1024) {fclose(f); throw NativeError("Invalid pdex.bin file size");}
        std::vector<uint8_t> data(size); size_t got=fread(data.data(),1,size,f); fclose(f); if(got!=(size_t)size) throw NativeError("Truncated pdex.bin");
        if(memcmp(data.data(),"Playdate PDX",12)) throw NativeError("Unsupported/encrypted pdex.bin signature");
        uint32_t unpacked=u32(data.data()+32),program=u32(data.data()+36),shim=u32(data.data()+40),relocations=u32(data.data()+44);
        uint64_t decodedSize=uint64_t(unpacked)+uint64_t(relocations)*4;
        if(unpacked<4 || decodedSize>64*1024*1024 || program>64*1024*1024 || uint64_t(relocations)*4>unpacked) throw NativeError("Invalid pdex.bin header lengths");
        // The header's decompressed size excludes the relocation list, which
        // is appended to the same zlib stream (verified against device PDX).
        std::vector<uint8_t> binary((size_t)decodedSize); uLongf length=(uLongf)decodedSize;
        int result=uncompress(binary.data(),&length,data.data()+48,(uLong)data.size()-48);
        if(result!=Z_OK) {
            if(data.size()-48!=decodedSize) throw NativeError("Invalid compressed pdex.bin payload");
            memcpy(binary.data(),data.data()+48,(size_t)decodedSize); length=(uLongf)decodedSize;
        }
        if(length!=decodedSize) throw NativeError("pdex.bin decompressed size mismatch");
        uint32_t actual=unpacked;
        if(actual>program || shim>=actual || program<4) throw NativeError("Invalid pdex.bin code/entry bounds");
        for(uint32_t i=0;i<relocations;i++) {
            uint32_t offset=u32(binary.data()+actual+i*4);
            if(offset>actual-4 || (offset&3)) throw NativeError("Invalid pdex.bin relocation offset");
            uint32_t value=u32(binary.data()+offset)+CodeAddress; memcpy(binary.data()+offset,&value,4);
        }
        code.resize((program+32+4095)&~4095u); memcpy(code.data()+32,binary.data(),actual); map(CodeBase,code,UC_PROT_ALL);
        entry=CodeAddress+shim;
        fprintf(stderr,"[native] ARM Cortex-M7: %u code bytes, %u program bytes, %u relocations\n",actual,program,relocations);
        invoke(entry,{apiRoot,0,2},10000);
        if(!update) invoke(entry,{apiRoot,1,2},10000);
    }
    void pushResource(const Resource &r) {
        if(r.weak) {lua_getfield(context,LUA_REGISTRYINDEX,"crank.native.weak");lua_rawgeti(context,-1,r.ref);lua_remove(context,-2);}
        else lua_rawgeti(context,LUA_REGISTRYINDEX,r.ref);
    }
    uint32_t hold(int index,const char *type,bool weak=false) {
        auto L=context; index=lua_absindex(L,index); if(lua_isnil(L,index)) return 0;
        auto identity=lua_topointer(L,index);
        for(auto &o:objects) {pushResource(o.second); bool same=lua_topointer(L,-1)==identity; lua_pop(L,1); if(same) return o.first;}
        uint32_t handle=allocate(16); int ref;
        if(weak) {lua_getfield(L,LUA_REGISTRYINDEX,"crank.native.weak");lua_pushvalue(L,index);ref=luaL_ref(L,-2);lua_pop(L,1);}
        else {lua_pushvalue(L,index);ref=luaL_ref(L,LUA_REGISTRYINDEX);}
        objects.emplace(handle,Resource{type,ref,0,weak}); return handle;
    }
    Resource &resource(uint32_t handle,const char *type=nullptr) {
        auto it=objects.find(handle); if(it==objects.end() || (type && it->second.type!=type)) throw NativeError("Invalid or released native resource handle"); return it->second;
    }
    void push(uint32_t handle,const char *type=nullptr) {if(!handle) lua_pushnil(context); else {pushResource(resource(handle,type));if(lua_isnil(context,-1)) throw NativeError("Native Lua resource was collected");}}
    LCDBitmap *bitmap(uint32_t handle) {push(handle,"bitmap"); auto ud=(LCDBitmap**)luaL_testudata(context,-1,"pd.bitmap"); auto result=ud ? *ud : nullptr; lua_pop(context,1); if(!result) throw NativeError("Invalid bitmap"); return result;}
    void drop(uint32_t handle) {
        if(!handle) return; auto &r=resource(handle);
        if(r.weak) {lua_getfield(root,LUA_REGISTRYINDEX,"crank.native.weak");luaL_unref(root,-1,r.ref);lua_pop(root,1);}
        else luaL_unref(root,LUA_REGISTRYINDEX,r.ref);
        if(r.retained!=LUA_NOREF) luaL_unref(root,LUA_REGISTRYINDEX,r.retained);
        blocks.erase(r.stringData);
        objects.erase(handle); blocks.erase(handle);
    }
    void setGlobal(const std::string &key) {
        auto L=context; int value=lua_gettop(L);size_t dot=key.find('.');if(dot==std::string::npos) {lua_setglobal(L,key.c_str());return;}
        lua_getglobal(L,key.substr(0,dot).c_str());
        if(lua_isnil(L,-1)) {lua_pop(L,1);lua_newtable(L);lua_pushvalue(L,-1);lua_setglobal(L,key.substr(0,dot).c_str());}
        size_t start=dot+1,next;
        while((next=key.find('.',start))!=std::string::npos) {auto part=key.substr(start,next-start);lua_getfield(L,-1,part.c_str());if(lua_isnil(L,-1)) {lua_pop(L,1);lua_newtable(L);lua_pushvalue(L,-1);lua_setfield(L,-3,part.c_str());}lua_remove(L,-2);start=next+1;}
        lua_pushvalue(L,value);lua_setfield(L,-2,key.substr(start).c_str());lua_pop(L,2);
    }
    void function(const char *path) {
        std::string p=path; size_t start=0,end=p.find('.'); lua_getglobal(context,p.substr(0,end).c_str());
        while(end!=std::string::npos) {start=end+1; end=p.find('.',start);if(!lua_istable(context,-1))throw NativeError(std::string("Missing API table: ")+path); lua_getfield(context,-1,p.substr(start,end-start).c_str()); lua_remove(context,-2);}
        if(!lua_isfunction(context,-1)) throw NativeError(std::string("Missing implemented Lua API: ")+path);
    }
    void call(int count,int returns) {if(lua_pcall(context,count,returns,0)!=LUA_OK) throw NativeError(std::string("SDK bridge: ")+lua_tostring(context,-1));if(crank_status()==3)throw NativeError("Native callback failed; see runtime error");}
    static int luaFunction(lua_State *L);
    static int menuCallback(lua_State *L);
    static int spriteCallback(lua_State *L);
    std::string format(const std::string &fmt,Args &arguments);
    std::string filePath(const std::string &path,const char *base) {
        std::filesystem::path p(path);if(p.is_absolute() || p.has_root_name()) throw NativeError("File path must be relative to game/save directory");
        for(const auto &part:p) if(part=="..") throw NativeError("File path escapes game/save directory");
        return (std::filesystem::path(base)/p).string();
    }
    FILE **file(uint32_t handle) {
        push(handle,"file");auto f=(FILE**)luaL_testudata(context,-1,"pd.file");lua_pop(context,1);
        if(!f || !*f) throw NativeError("Invalid or closed native file");return f;
    }
    void closure(uint32_t callback,bool gc=false) {lua_pushlightuserdata(context,this); lua_pushinteger(context,callback);lua_pushboolean(context,gc); lua_pushcclosure(context,luaFunction,3);}
    void dispatch(int id);
};
std::unique_ptr<Native> native;
struct Args {
    Native &n; int core=0,floats=0; uint32_t offset=0,va=0;
    uint32_t word() {if(va){auto v=n.read<uint32_t>(va);va+=4;return v;}if(core<4) return n.reg(UC_ARM_REG_R0+core++); auto v=n.read<uint32_t>(n.reg(UC_ARM_REG_SP)+offset); offset+=4; return v;}
    uint64_t wide() {if(va)va=(va+7)&~7u;else if(core<4)core=(core+1)&~1;else offset=(offset+7)&~7u;uint64_t low=word();return low|(uint64_t(word())<<32);}
    int integer() {return (int32_t)word();}
    float number() {if(floats<16) return f32(n.reg(UC_ARM_REG_S0+floats++)); return f32(word());}
};
std::string Native::format(const std::string &fmt,Args &a) {
    std::string output;
    for(size_t i=0;i<fmt.size();) {
        if(fmt[i]!='%') {output+=fmt[i++];continue;}
        i++;if(i<fmt.size() && fmt[i]=='%') {output+='%';i++;continue;}
        std::string spec="%";
        while(i<fmt.size() && strchr("-+ #0",fmt[i])) spec+=fmt[i++];
        auto field=[&](bool precision) {
            int value=0;bool present=false;
            if(i<fmt.size() && fmt[i]=='*') {value=a.integer();present=true;i++;}
            else while(i<fmt.size() && fmt[i]>='0' && fmt[i]<='9') {present=true;value=value*10+fmt[i++]-'0';if(value>4096)throw NativeError("Format width/precision exceeds 4096");}
            if(value>4096 || value< -4096)throw NativeError("Format width/precision exceeds 4096");
            if(present && (!precision || value>=0)) {if(precision)spec+='.';spec+=std::to_string(value);}
            else if(precision && !present)spec+=".0";
        };
        field(false);if(i<fmt.size() && fmt[i]=='.'){i++;field(true);}
        std::string length;while(i<fmt.size() && strchr("hljztL",fmt[i]))length+=fmt[i++];
        if(i==fmt.size())throw NativeError("Incomplete native format specifier");
        char conversion=fmt[i++];char buffer[8192];int count=-1;
        if(strchr("diuoxX",conversion)) {
            bool wide=length=="ll" || length=="j";uint64_t value=wide?a.wide():a.word();
            if(length=="hh")value=(conversion=='d'||conversion=='i')?(uint64_t)(int64_t)(int8_t)value:(uint8_t)value;
            if(length=="h")value=(conversion=='d'||conversion=='i')?(uint64_t)(int64_t)(int16_t)value:(uint16_t)value;
            spec+="ll";spec+=conversion;
            if(conversion=='d'||conversion=='i')count=snprintf(buffer,sizeof(buffer),spec.c_str(),wide?(long long)value:(long long)(int32_t)value);
            else count=snprintf(buffer,sizeof(buffer),spec.c_str(),(unsigned long long)(wide?value:(uint32_t)value));
        } else if(strchr("fFeEgGaA",conversion)) {
            uint64_t bits=a.wide();double value;memcpy(&value,&bits,8);spec+=conversion;count=snprintf(buffer,sizeof(buffer),spec.c_str(),value);
        } else if(conversion=='s') {
            uint32_t p=a.word();std::string value=p?string(p):"(null)";spec+='s';count=snprintf(buffer,sizeof(buffer),spec.c_str(),value.c_str());
        } else if(conversion=='c') {spec+='c';count=snprintf(buffer,sizeof(buffer),spec.c_str(),a.integer());}
        else if(conversion=='p') {spec+="08x";count=snprintf(buffer,sizeof(buffer),spec.c_str(),a.word());}
        else throw NativeError(std::string("Unsupported native format conversion: %")+conversion);
        if(count<0 || count>=(int)sizeof(buffer))throw NativeError("Native formatted field exceeds buffer");
        output.append(buffer,count);if(output.size()>1024*1024)throw NativeError("Native formatted output exceeds 1 MB");
    }
    return output;
}
int Native::luaFunction(lua_State *L) {
    auto n=(Native*)lua_touserdata(L,lua_upvalueindex(1));
    if(n!=native.get()) return 0;
    auto old=n->context; n->context=L;
    const size_t temporary=n->temporaryStrings.size();n->argumentCounts.push_back(lua_gettop(L));
    bool failed=false; uint32_t result=0;
    try {result=n->invoke((uint32_t)lua_tointeger(L,lua_upvalueindex(2)),{0}); if(result>(uint32_t)lua_gettop(L)) throw NativeError("C Lua function returned invalid result count");}
    catch(const Cancelled&) {failed=true; lua_pushliteral(L,"Game stopped");}
    catch(const std::exception &ex) {failed=true; lua_pushstring(L,ex.what());}
    while(n->temporaryStrings.size()>temporary) {n->blocks.erase(n->temporaryStrings.back());n->temporaryStrings.pop_back();}
    if(lua_toboolean(L,lua_upvalueindex(3))) {auto handle=(uint32_t*)lua_touserdata(L,1);if(handle && n->objects.count(*handle)) {n->drop(*handle);*handle=0;}}
    n->argumentCounts.pop_back();
    n->context=old;
    if(failed) return lua_error(L); return (int)result;
}
int Native::menuCallback(lua_State *L) {
    auto n=(Native*)lua_touserdata(L,lua_upvalueindex(1));if(n!=native.get())return 0;
    uint32_t id=(uint32_t)lua_tointeger(L,lua_upvalueindex(2));if(!n->objects.count(id))return 0;
    auto old=n->context;n->context=L;bool failed=false;bool terminating=n->terminating;n->terminating|=pd_host_should_stop()!=0;
    try {uint32_t callback=(uint32_t)lua_tointeger(L,lua_upvalueindex(3));if(callback)n->invoke(callback,{n->resource(id,"menu").userdata});}
    catch(const Cancelled&) {} catch(const std::exception &ex){failed=true;lua_pushstring(L,ex.what());}
    n->context=old;n->terminating=terminating;if(failed)return lua_error(L);return 0;
}
int Native::spriteCallback(lua_State *L) {
    auto n=(Native*)lua_touserdata(L,lua_upvalueindex(1));if(n!=native.get())return 0;
    auto handle=(uint32_t)lua_tointeger(L,lua_upvalueindex(2)),callback=(uint32_t)lua_tointeger(L,lua_upvalueindex(3));
    if(!n->objects.count(handle) || !callback)return 0;
    auto old=n->context;n->context=L;bool failed=false;uint32_t result=0;int kind=(int)lua_tointeger(L,lua_upvalueindex(4));
    int ox=g_pd.draw_offset_x,oy=g_pd.draw_offset_y;
    try {
        std::vector<uint32_t> arguments{handle};std::vector<float> floats;
        if(kind==2){
            n->function("playdate.graphics.sprite.getBounds");n->push(handle,"sprite");n->call(1,4);
            for(int i=-4;i<0;i++)floats.push_back((float)lua_tonumber(L,i));lua_pop(L,4);
            float x=floats[0],y=floats[1],w=floats[2],h=floats[3];
            float left=std::max(x,0.f),top=std::max(y,0.f),right=std::min(x+w,400.f),bottom=std::min(y+h,240.f);
            floats.insert(floats.end(),{left,top,std::max(0.f,right-left),std::max(0.f,bottom-top)});
            // Lua draw methods use a local origin; SDK C callbacks draw in
            // global coordinates and receive two PDRect homogeneous aggregates.
            g_pd.draw_offset_x-=(int)x;g_pd.draw_offset_y-=(int)y;
        }else if(kind==3)arguments.push_back(n->hold(2,"sprite"));
        result=n->invoke(callback,arguments,2000,floats);
    }catch(const Cancelled&){}catch(const std::exception &ex){failed=true;pd_host_fail(ex.what());lua_pushstring(L,ex.what());}
    g_pd.draw_offset_x=ox;g_pd.draw_offset_y=oy;n->context=old;
    if(failed)return lua_error(L);if(kind==3){const char*types[]={"slide","freeze","overlap","bounce"};if(result>3){pd_host_fail("Invalid C sprite collision response");return 0;}lua_pushstring(L,types[result]);return 1;}return 0;
}

void Native::dispatch(int id) {
    if(id<0 || id>=(int)std::size(native_functions)) throw NativeError("Invalid SDK function address");
    lastApi=native_functions[id].name; usedApis.insert(lastApi);apiCalls++;
    if(getenv("PD_NATIVE_TRACE")) fprintf(stderr,"[native-api %s]\n",lastApi.c_str());
    const std::string name=lastApi; Args a{*this}; auto L=context;
    auto result=[&](uint32_t v){reg(UC_ARM_REG_R0,v);};
    auto number=[&](float v){reg(UC_ARM_REG_S0,floatBits(v));};
    auto pushInt=[&](int v){lua_pushinteger(L,v);};
    auto pushNum=[&](float v){lua_pushnumber(L,v);};
    auto text=[&](uint32_t p){auto s=string(p); lua_pushlstring(L,s.data(),s.size());};
    auto graphicsColor=[&](uint32_t color){
        function("playdate.graphics.setColor");pushInt(color<=3?color:0);call(1,0);
        if(color>3){function("playdate.graphics.setPattern");lua_newtable(L);auto pattern=memory(color,16);
            // Device bitmap/pattern bits encode white as 1; the shared backend
            // encodes black as 1. Coverage-mask bytes retain their polarity.
            for(int i=0;i<16;i++){pushInt(i<8?(uint8_t)~pattern[i]:pattern[i]);lua_rawseti(L,-2,i+1);}call(1,0);}
    };
    // Lua stack operations intentionally survive dispatch. Other SDK adapters
    // restore their temporary arguments/results, including on exceptions.
    if(name.rfind("lua.",0)==0) {
        if(name=="lua.registerClass") {
            std::string key=string(a.word());auto functions=a.word(),values=a.word();int isStatic=a.integer();uint32_t err=a.word();int base=lua_gettop(L);
            luaL_newmetatable(L,("crank.native."+key).c_str());int table=lua_gettop(L);
            lua_pushvalue(L,table);lua_setfield(L,table,"__index");
            if(functions) for(int i=0;i<256;i++) {
                uint32_t p=read<uint32_t>(functions+i*8),f=read<uint32_t>(functions+i*8+4);if(!p) break;
                std::string field=string(p);closure(f,field=="__gc");lua_setfield(L,table,field.c_str());
                if(i==255) throw NativeError("Unterminated native class function list");
            }
            if(values) for(int i=0;i<256;i++) {
                uint32_t p=read<uint32_t>(values+i*12);if(!p) break;int type=read<int>(values+i*12+4);uint32_t val=read<uint32_t>(values+i*12+8);
                if(type==0)pushInt((int32_t)val);else if(type==1)pushNum(f32(val));else if(type==2)text(val);else throw NativeError("Invalid native class constant type");
                lua_setfield(L,table,string(p).c_str());if(i==255)throw NativeError("Unterminated native class constant list");
            }
            lua_pushvalue(L,table);setGlobal(key);lua_settop(L,base);write<uint32_t>(err,0);result(1);return;
        }
        if(name=="lua.pushObject") {
            uint32_t object=a.word();std::string type=string(a.word());int values=a.integer();if(values<0 || values>64)throw NativeError("Invalid object uservalue count");
            luaL_getmetatable(L,("crank.native."+type).c_str());if(lua_isnil(L,-1))throw NativeError("Unregistered native object class: "+type);lua_pop(L,1);
            auto handle=(uint32_t*)lua_newuserdatauv(L,4,values);*handle=0;luaL_setmetatable(L,("crank.native."+type).c_str());
            uint32_t id=hold(-1,("object:"+type).c_str(),true);*handle=id;objects[id].userdata=object;result(id);return;
        }
        if(name=="lua.getArgObject") {
            int pos=a.integer();std::string type=string(a.word());uint32_t out=a.word();auto handle=(uint32_t*)luaL_testudata(L,pos,("crank.native."+type).c_str());
            if(!handle || !objects.count(*handle) || resource(*handle).type!="object:"+type) {write<uint32_t>(out,0);result(0);return;}
            write<uint32_t>(out,*handle);result(resource(*handle).userdata);return;
        }
        if(name=="lua.retainObject") {
            uint32_t p=a.word();auto &r=resource(p);if(r.count++==0){push(p);r.retained=luaL_ref(L,LUA_REGISTRYINDEX);}result(p);return;
        }
        if(name=="lua.releaseObject") {
            auto &r=resource(a.word());if(r.count<=0)throw NativeError("Unbalanced native object release");if(--r.count==0){luaL_unref(L,LUA_REGISTRYINDEX,r.retained);r.retained=LUA_NOREF;}return;
        }
        if(name=="lua.setUserValue") {
            uint32_t p=a.word();int slot=a.integer();if(slot<1 || slot>64 || lua_gettop(L)<1)throw NativeError("Invalid native uservalue slot");push(p);lua_pushvalue(L,-2);
            if(!lua_setiuservalue(L,-2,slot)) {lua_pop(L,1);throw NativeError("Native uservalue slot out of range");}lua_pop(L,2);return;
        }
        if(name=="lua.getUserValue") {
            uint32_t p=a.word();int slot=a.integer();if(slot<1 || slot>64)throw NativeError("Invalid native uservalue slot");push(p);lua_getiuservalue(L,-1,slot);lua_remove(L,-2);result(lua_gettop(L));return;
        }
        if(name=="lua.addFunction") {
            auto f=a.word(),p=a.word(),err=a.word(); std::string key=string(p);
            int base=lua_gettop(L); closure(f); int fn=lua_gettop(L);
            size_t dot=key.find('.'); if(dot==std::string::npos) {lua_setglobal(L,key.c_str());}
            else {
                lua_getglobal(L,key.substr(0,dot).c_str());
                if(lua_isnil(L,-1)) {lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setglobal(L,key.substr(0,dot).c_str());}
                size_t start=dot+1,next;
                while((next=key.find('.',start))!=std::string::npos) {
                    auto part=key.substr(start,next-start); lua_getfield(L,-1,part.c_str());
                    if(lua_isnil(L,-1)) {lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setfield(L,-3,part.c_str());}
                    lua_remove(L,-2); start=next+1;
                }
                lua_pushvalue(L,fn); lua_setfield(L,-2,key.substr(start).c_str()); lua_settop(L,base);
            }
            write<uint32_t>(err,0); result(1); return;
        }
        if(name=="lua.pushFunction") {closure(a.word()); return;}
        if(name=="lua.indexMetatable") {
            if(lua_gettop(L)>=2 && lua_getmetatable(L,1)){lua_pushvalue(L,2);lua_rawget(L,-2);lua_remove(L,-2);if(!lua_isnil(L,-1)){result(1);return;}lua_pop(L,1);}result(0);return;
        }
        if(name=="lua.getArgCount") {result(argumentCounts.empty()?lua_gettop(L):argumentCounts.back()); return;}
        if(name=="lua.argIsNil") {result(lua_isnoneornil(L,a.integer())); return;}
        if(name=="lua.getArgBool") {result(lua_toboolean(L,a.integer())); return;}
        if(name=="lua.getArgInt") {result((uint32_t)lua_tointeger(L,a.integer())); return;}
        if(name=="lua.getArgFloat") {number((float)lua_tonumber(L,a.integer())); return;}
        if(name=="lua.getArgType") {
            int pos=a.integer(); uint32_t out=a.word(); int kind=lua_type(L,pos),value=0;
            switch(kind) {case LUA_TBOOLEAN:value=1;break;case LUA_TNUMBER:value=lua_isinteger(L,pos)?2:3;break;case LUA_TSTRING:value=4;break;case LUA_TTABLE:value=5;break;case LUA_TFUNCTION:value=6;break;case LUA_TTHREAD:value=7;break;case LUA_TUSERDATA:value=8;break;}
            uint32_t className=0;
            if(out && value==8){auto handle=(uint32_t*)lua_touserdata(L,pos);if(lua_rawlen(L,pos)==4 && handle && objects.count(*handle)){auto &r=resource(*handle);if(r.type.rfind("object:",0)==0){if(!r.stringData)r.stringData=copyString(r.type.substr(7));className=r.stringData;}}}
            write<uint32_t>(out,className); result(value); return;
        }
        if(name=="lua.getArgString" || name=="lua.getArgBytes") {
            int pos=a.integer(); uint32_t out=name=="lua.getArgBytes" ? a.word() : 0; size_t count=0; const char *s=lua_tolstring(L,pos,&count);
            if(!s) {write<uint32_t>(out,0); result(0);return;}
            uint32_t p=allocate((uint32_t)count+1);temporaryStrings.push_back(p); memcpy(memory(p,count),s,count); write<uint32_t>(out,(uint32_t)count); result(p); return;
        }
        if(name=="lua.getBitmap") {int pos=a.integer(); if(!luaL_testudata(L,pos,"pd.bitmap")) throw NativeError("Lua argument is not a bitmap"); result(hold(pos,"bitmap")); return;}
        if(name=="lua.getSprite") {int pos=a.integer(); result(hold(pos,"sprite")); return;}
        if(name=="lua.pushNil") {lua_pushnil(L);return;}
        if(name=="lua.pushBool") {lua_pushboolean(L,a.integer());return;}
        if(name=="lua.pushInt") {pushInt(a.integer());return;}
        if(name=="lua.pushFloat") {pushNum(a.number());return;}
        if(name=="lua.pushString") {text(a.word());return;}
        if(name=="lua.pushBytes") {uint32_t p=a.word(),len=a.word(); lua_pushlstring(L,(char*)memory(p,len),len);return;}
        if(name=="lua.pushBitmap" || name=="lua.pushSprite") {push(a.word(),name=="lua.pushBitmap"?"bitmap":"sprite");return;}
        if(name=="lua.callFunction" || name=="lua.callFunction_deprecated") {
            std::string key=string(a.word()); int nargs=a.integer(); uint32_t err=name=="lua.callFunction"?a.word():0;
            int base=lua_gettop(L); if(nargs<0 || nargs>base) throw NativeError("Invalid C to Lua argument count");
            try {function(key.c_str());}catch(const NativeError &ex){lua_settop(L,base-nargs);write<uint32_t>(err,copyString(ex.what()));result(0);return;}
            lua_insert(L,base-nargs+1);
            if(lua_pcall(L,nargs,0,0)!=LUA_OK) {write<uint32_t>(err,copyString(lua_tostring(L,-1))); lua_pop(L,1);result(0);} else {write<uint32_t>(err,0);result(1);} return;
        }
        if(name=="lua.stop") {g_pd.wait_until=UINT64_MAX;return;}
        if(name=="lua.start") {g_pd.wait_until=0;return;}
        throw NativeError("Unimplemented C API: "+name);
    }
    struct StackGuard {lua_State *L; int top; ~StackGuard(){lua_settop(L,top);}} guard{L,lua_gettop(L)};
    if(name=="system.realloc") {uint32_t p=a.word(),size=a.word();result(resize(p,size));return;}
    if(name=="system.formatString" || name=="system.vaFormatString" || name=="system.logToConsole" || name=="system.error") {
        uint32_t out=0;if(name=="system.formatString" || name=="system.vaFormatString")out=a.word();
        auto fmt=string(a.word());if(name=="system.vaFormatString")a.va=a.word();auto value=format(fmt,a);
        if(out){write<uint32_t>(out,copyString(value));result((uint32_t)value.size());}
        else if(name=="system.error")throw NativeError(value);else fprintf(stderr,"[native console] %s\n",value.c_str());return;
    }
    if(name=="system.setMenuImage") {auto p=a.word();int offset=a.integer();function("playdate.setMenuImage");push(p,"bitmap");pushInt(offset);call(2,0);return;}
    if(name=="system.addMenuItem" || name=="system.addCheckmarkMenuItem" || name=="system.addOptionsMenuItem") {
        auto title=a.word();int kind=name=="system.addMenuItem"?0:name=="system.addCheckmarkMenuItem"?1:2;
        auto value=kind?a.word():0;int count=kind==2?a.integer():0;auto callback=a.word(),ud=a.word();
        if(kind==2 && (count<1 || count>1024))throw NativeError("Invalid C menu option count");
        function(("playdate."+name.substr(7)).c_str());text(title);int arguments=1;
        if(kind==1){lua_pushboolean(L,value);arguments++;}
        if(kind==2){lua_newtable(L);for(int i=0;i<count;i++){text(read<uint32_t>(value+i*4));lua_rawseti(L,-2,i+1);}arguments++;}
        call(arguments,1);auto handle=hold(-1,"menu");if(!handle){result(0);return;}objects[handle].userdata=ud;
        lua_pushlightuserdata(L,this);lua_pushinteger(L,handle);lua_pushinteger(L,callback);lua_pushcclosure(L,menuCallback,3);lua_setfield(L,-2,"_callback");result(handle);return;
    }
    if(name=="system.removeAllMenuItems") {function("playdate.removeAllMenuItems");call(0,0);std::vector<uint32_t> ids;for(auto &o:objects)if(o.second.type=="menu")ids.push_back(o.first);for(auto id:ids)drop(id);return;}
    if(name=="system.removeMenuItem") {auto p=a.word();function("playdate.removeMenuItem");push(p,"menu");call(1,0);drop(p);return;}
    if(name=="system.getMenuItemUserdata") {result(resource(a.word(),"menu").userdata);return;}
    if(name=="system.setMenuItemUserdata") {auto p=a.word(),value=a.word();resource(p,"menu").userdata=value;return;}
    if(name=="system.getMenuItemValue" || name=="system.setMenuItemValue") {
        auto p=a.word();int value=name=="system.setMenuItemValue"?a.integer():0;push(p,"menu");lua_getfield(L,-1,"_kind");int kind=(int)lua_tointeger(L,-1);lua_pop(L,1);
        if(name=="system.getMenuItemValue"){lua_getfield(L,-1,"_value");result((int)lua_tointeger(L,-1)-(kind==2?1:0));}
        else {function("playdate.setMenuItemValue");push(p);pushInt(value+(kind==2?1:0));call(2,0);}return;
    }
    if(name=="system.getMenuItemTitle" || name=="system.setMenuItemTitle") {
        auto p=a.word();auto &r=resource(p,"menu");if(name=="system.getMenuItemTitle") {if(!r.stringData){push(p);lua_getfield(L,-1,"_title");r.stringData=copyString(lua_tostring(L,-1));}result(r.stringData);}
        else {auto title=a.word();function("playdate.setMenuItemTitle");push(p);text(title);call(2,0);blocks.erase(r.stringData);r.stringData=0;}return;
    }
    if(name.rfind("file.",0)==0) {
        #include "pd_native_file.inc"
    }
    if(name.rfind("sprite.",0)==0) {
        #include "pd_native_sprite.inc"
    }
    if(name.rfind("sound.",0)==0) {
        #include "pd_native_sound.inc"
    }
    if(name=="system.setUpdateCallback") {update=a.word();updateData=a.word();return;}
    if(name=="system.getButtonState") {auto current=a.word(),pressed=a.word(),released=a.word();write<uint32_t>(current,g_pd.buttons_current);write<uint32_t>(pressed,g_pd.buttons_pushed);write<uint32_t>(released,g_pd.buttons_released);return;}
    if(name=="system.getCurrentTimeMilliseconds") {result((uint32_t)(pd_time_milliseconds()-g_pd.start_time));return;}
    if(name=="system.getSecondsSinceEpoch") {uint32_t p=a.word();write<uint32_t>(p,(uint32_t)(pd_time_milliseconds()%1000));result((uint32_t)(time(nullptr)-946684800));return;}
    if(name=="system.getLanguage") {result(0);return;}
    if(name=="system.getCrankAngle") {number(g_pd.crank_angle);return;}
    if(name=="system.getCrankChange") {number(g_pd.crank_change);g_pd.crank_change=0;return;}
    if(name=="system.isCrankDocked") {result(g_pd.crank_docked);return;}
    if(name=="system.setCrankSoundsDisabled") {int value=a.integer();result(!crankSounds);crankSounds=!value;return;}
    if(name=="system.getElapsedTime") {number((float)(pd_time_milliseconds()-elapsedStart)/1000);return;}
    if(name=="system.resetElapsedTime") {elapsedStart=(uint32_t)pd_time_milliseconds();return;}
    if(name=="system.getFlipped" || name=="system.getReduceFlashing") {result(0);return;}
    if(name=="system.setPeripheralsEnabled" || name=="system.setAutoLockDisabled") {return;}
    if(name=="system.getAccelerometer") {auto x=a.word(),y=a.word(),z=a.word();write<float>(x,0);write<float>(y,0);write<float>(z,1);return;}
    if(name=="system.getBatteryPercentage") {number(100);return;}
    if(name=="system.getBatteryVoltage") {number(4.2f);return;}
    if(name=="system.getVolume") {number(1);return;}
    if(name=="system.getPowerStatus") {result(0);return;}
    if(name=="system.setButtonCallback") {buttonCallback=a.word();buttonData=a.word();return;}
    if(name=="system.clearICache") {check(uc_ctl_remove_cache(cpu,CodeBase,CodeBase+code.size()));return;}
    if(name=="system.delay") {uint32_t end=SDL_GetTicks()+a.word();while((int32_t)(end-SDL_GetTicks())>0) {if(pd_host_should_stop()) throw Cancelled{};SDL_Delay(1);}return;}
    if(name=="system.drawFPS") {int x=a.integer(),y=a.integer();function("playdate.drawFPS");pushInt(x);pushInt(y);call(2,0);return;}
    if(name=="display.getWidth") {result(400/std::max(1,g_pd.display_scale));return;}
    if(name=="display.getHeight") {result(240/std::max(1,g_pd.display_scale));return;}
    if(name=="display.setRefreshRate") {float rate=a.number();if(!std::isfinite(rate) || rate<0 || rate>120) throw NativeError("Invalid display refresh rate");g_pd.refresh_rate=rate;return;}
    if(name=="display.setInverted") {g_pd.inverted=a.integer()!=0;return;}
    if(name=="display.setScale") {int s=a.integer();if(s!=1 && s!=2 && s!=4 && s!=8) throw NativeError("Invalid display scale");g_pd.display_scale=s;return;}
    if(name=="graphics.getFrame") {result(mirror(pd_screen_bitmap,true).data);return;}
    if(name=="graphics.getDisplayFrame") {if(!displayFrame){displayFrame=allocate(52*240);pd_native_present();}result(displayFrame);return;}
    if(name=="graphics.markUpdatedRows" || name=="graphics.display") {pd_frame_draw_ops++;return;}
    if(name=="graphics.getBitmapData") {
        auto p=a.word();auto w=a.word(),h=a.word(),row=a.word(),mask=a.word(),data=a.word();auto bm=bitmap(p); auto &m=mirror(bm);
        write<int>(w,bm->width);write<int>(h,bm->height);write<int>(row,m.stride);write<uint32_t>(mask,m.mask);write<uint32_t>(data,m.data);return;
    }
    if(name=="graphics.newBitmap" || name=="graphics.loadBitmap" || name=="graphics.loadFont") {
        if(name=="graphics.newBitmap") {int w=a.integer(),h=a.integer();uint32_t color=a.word();if(w<1 || h<1 || w>4096 || h>4096 || uint64_t((w+7)/8)*h>4*1024*1024) throw NativeError("Invalid native bitmap dimensions");if(color>3)throw NativeError("Unimplemented C API variant: graphics.newBitmap pattern background");function("playdate.graphics.image.new");pushInt(w);pushInt(h);pushInt(color);call(3,1);result(hold(-1,"bitmap"));return;}
        auto p=a.word(),err=a.word();function(name=="graphics.loadFont"?"playdate.graphics.font.new":"playdate.graphics.image.new");text(p);call(1,2);
        if(lua_isnil(L,-2)) {write<uint32_t>(err,copyString(lua_isstring(L,-1)?lua_tostring(L,-1):"Cannot load resource"));result(0);} else {write<uint32_t>(err,0);result(hold(-2,name=="graphics.loadFont"?"font":"bitmap"));}return;
    }
    if(name=="graphics.freeBitmap") {
        uint32_t p=a.word();auto bm=bitmap(p);for(auto it=mirrors.begin();it!=mirrors.end();) {if(it->bitmap==bm) {blocks.erase(it->data);blocks.erase(it->mask);it=mirrors.erase(it);} else ++it;}
        // Release C ownership. A bitmap already pushed to Lua or attached to
        // a sprite remains alive through that Lua reference until its own GC.
        drop(p);return;
    }
    if(name=="graphics.setFont") {uint32_t p=a.word();function("playdate.graphics.setFont");push(p,"font");call(1,0);return;}
    if(name=="graphics.getFontHeight") {push(a.word(),"font");auto f=(LCDFont**)luaL_testudata(L,-1,"pd.font");result(f && *f?(*f)->glyph_height:0);return;}
    if(name=="graphics.drawText" || name=="graphics.getTextWidth") {
        uint32_t font=0;if(name=="graphics.getTextWidth") font=a.word();uint32_t p=a.word(),len=a.word();int encoding=a.integer();int x=0,y=0,tracking=0;
        if(encoding<0 || encoding>2) throw NativeError("Unknown text encoding");
        if(encoding==2) throw NativeError("UTF-16 C text not implemented yet");
        if(name=="graphics.drawText") {x=a.integer();y=a.integer();}else tracking=a.integer();
        LCDFont *old=g_pd.current_font; if(font) {push(font,"font");auto f=(LCDFont**)luaL_testudata(L,-1,"pd.font");g_pd.current_font=f?*f:old;lua_pop(L,1);}
        function(name=="graphics.drawText"?"playdate.graphics.drawText":"playdate.graphics.getTextWidth");lua_pushlstring(L,(char*)memory(p,len),len);int args=1;
        if(name=="graphics.drawText") {pushInt(x);pushInt(y);args=3;}
        call(args,1);int width=(int)lua_tointeger(L,-1);g_pd.current_font=old;result(width+(name=="graphics.getTextWidth"?tracking*(int)len:0));return;
    }
    if(name=="graphics.drawBitmap" || name=="graphics.drawScaledBitmap") {
        uint32_t bitmap=a.word();int x=a.integer(),y=a.integer();function(name=="graphics.drawBitmap"?"playdate.graphics.drawBitmap":"playdate.graphics.drawScaledBitmap");push(bitmap,"bitmap");pushInt(x);pushInt(y);
        if(name=="graphics.drawBitmap") {pushInt(a.integer());call(4,0);} else {float sx=a.number(),sy=a.number();pushNum(sx);pushNum(sy);call(5,0);}return;
    }
    if(name=="graphics.getBitmapPixel") {auto bm=bitmap(a.word());int x=a.integer(),y=a.integer();int c=x<0 || y<0 || x>=bm->width || y>=bm->height ? 2 : (bm->mask && !(bm->mask[y*bm->rowbytes+x/8]&(128>>(x%8)))) ? 2 : (bm->data[y*bm->rowbytes+x/8]&(128>>(x%8))) ? 0 : 1;result(c);return;}
    if(name=="graphics.setPixel") {int x=a.integer(),y=a.integer();uint32_t c=a.word();function("playdate.graphics.setColor");pushInt(c);call(1,0);function("playdate.graphics.drawPixel");pushInt(x);pushInt(y);call(2,0);return;}
    if(name=="graphics.clear" || name=="graphics.setBackgroundColor" || name=="graphics.setDrawMode" || name=="graphics.setTextTracking" || name=="graphics.setTextLeading") {
        int v=a.integer();if(name=="graphics.clear" && (uint32_t)v>3)throw NativeError("Unimplemented C API variant: graphics.clear pattern");int old=g_pd.draw_mode;std::string path="playdate."+name;function(path.c_str());pushInt(v);call(1,0);if(name=="graphics.setDrawMode") result(old);return;
    }
    if(name=="graphics.setDrawOffset" || name=="graphics.setClipRect" || name=="graphics.setScreenClipRect") {int count=name=="graphics.setDrawOffset"?2:4;function(("playdate."+name).c_str());for(int i=0;i<count;i++)pushInt(a.integer());call(count,0);return;}
    if(name=="graphics.clearClipRect" || name=="graphics.popContext") {function(("playdate."+name).c_str());call(0,0);return;}
    if(name=="graphics.pushContext") {auto p=a.word();function("playdate.graphics.pushContext");push(p,"bitmap");call(1,0);return;}
    if(name=="graphics.setStencil" || name=="graphics.setStencilImage") {uint32_t p=a.word();function(p?"playdate.graphics.setStencilImage":"playdate.graphics.clearStencil");if(p) push(p,"bitmap");call(p?1:0,0);return;}
    if(name=="graphics.drawRect" || name=="graphics.fillRect" || name=="graphics.drawLine") {
        int coords[4];for(auto &c:coords)c=a.integer();int width=name=="graphics.drawLine"?a.integer():1;uint32_t color=a.word();
        graphicsColor(color);function("playdate.graphics.setLineWidth");pushInt(width);call(1,0);
        function(("playdate."+name).c_str());for(auto c:coords)pushInt(c);call(4,0);function("playdate.graphics.clearPattern");call(0,0);return;
    }
    if(name=="graphics.drawEllipse" || name=="graphics.fillEllipse") {
        int c[4];for(auto &v:c)v=a.integer();int w=name=="graphics.drawEllipse"?a.integer():1;float start=a.number(),end=a.number();uint32_t color=a.word();
        graphicsColor(color);function("playdate.graphics.setLineWidth");pushInt(w);call(1,0);function(("playdate."+name).c_str());for(auto v:c)pushInt(v);pushNum(start);pushNum(end);call(6,0);function("playdate.graphics.clearPattern");call(0,0);return;
    }
    if(name=="graphics.getTextTracking") {result(g_pd.text_tracking);return;}
    throw NativeError("Unimplemented C API: "+name);
}
} // namespace

extern "C" int pd_native_load(lua_State *L,const char *directory) {
    pd_native_shutdown();pd_host_native_info("{\"mode\":\"Lua\",\"armCalls\":0,\"apiCalls\":0,\"heapBytes\":0,\"resources\":0,\"usedApis\":[]}"); std::string path=std::string(directory)+"/pdex.bin"; FILE *f=fopen(path.c_str(),"rb"); if(!f) return 0; fclose(f);
    try {native=std::make_unique<Native>(L); native->load(directory); return 1;}
    catch(const std::exception &ex) {fprintf(stderr,"[native error] %s\n",ex.what()); pd_host_fail(ex.what()); native.reset(); return -1;}
    catch(const Cancelled&) {native.reset(); return -1;}
}
extern "C" int pd_native_has_update(void) {return native && native->update!=0;}
extern "C" int pd_native_update(void) {
    if(!pd_native_has_update()) return 0;
    try {return native->invoke(native->update,{native->updateData})?1:2;}
    catch(const Cancelled&) {g_pd.running=0; return 1;}
    catch(const std::exception &ex) {fprintf(stderr,"[native error] %s\n",ex.what()); pd_host_fail(ex.what()); return -1;}
}
extern "C" int pd_native_event(int event,uint32_t argument) {
    if(!native) return 1;
    try {native->terminating=event==6; native->invoke(native->entry,{native->apiRoot,(uint32_t)event,argument}); native->terminating=false; return 1;}
    catch(const Cancelled&) {return 1;}
    catch(const std::exception &ex) {pd_host_fail(ex.what()); return 0;}
}
extern "C" int pd_native_lifecycle(const char *callback) {
    const char *names[]={"", "", "deviceWillLock", "deviceDidUnlock", "gameWillPause", "gameWillResume", "gameWillTerminate"};
    for(int i=2;i<=6;i++) if(!strcmp(names[i],callback)) return pd_native_event(i,0); return 1;
}
extern "C" int pd_native_button(int button,int down) {
    if(!native)return 1;
    try {if(native->buttonCallback)native->invoke(native->buttonCallback,{(uint32_t)button,(uint32_t)down,(uint32_t)(pd_time_milliseconds()-g_pd.start_time),native->buttonData});}
    catch(const Cancelled&){return 1;}catch(const std::exception &ex){pd_host_fail(ex.what());return 0;}
    return pd_native_event(down?7:8,(uint32_t)button);
}
extern "C" void pd_native_publish(void) {
    if(!native)return;
    auto &n=*native;std::vector<uint32_t> pending;int top=lua_gettop(n.context);
    try {
        for(auto &s:n.sounds)if(s.second.playing && s.second.callback && n.objects.count(s.first))pending.push_back(s.first);
        for(auto id:pending){if(!n.sounds.count(id) || !n.objects.count(id))continue;
            n.push(id);lua_getfield(n.context,-1,"isPlaying");lua_insert(n.context,-2);n.call(1,1);bool playing=lua_toboolean(n.context,-1)!=0;lua_pop(n.context,1);
            if(!playing){auto callback=n.sounds[id];n.sounds[id].playing=false;n.invoke(callback.callback,{id,callback.userdata});}
        }
    }catch(const Cancelled&){}catch(const std::exception &ex){pd_host_fail(ex.what());}
    lua_settop(n.context,top);
    uint64_t bytes=0;for(auto b:native->blocks)bytes+=b.second;
    std::string json="{\"mode\":\""+std::string(native->update?"C":"Lua+C")+"\",\"armCalls\":"+std::to_string(native->calls)+",\"apiCalls\":"+std::to_string(native->apiCalls)+",\"heapBytes\":"+std::to_string(bytes)+",\"resources\":"+std::to_string(native->objects.size())+",\"usedApis\":[";
    bool first=true;for(auto &name:native->usedApis){if(!first)json+=',';first=false;json+='"'+name+'"';}json+="]}";pd_host_native_info(json.c_str());
}
extern "C" void pd_native_present(void) {
    if(!native || !native->displayFrame || !pd_screen_bitmap)return;
    auto bm=pd_screen_bitmap;auto output=native->memory(native->displayFrame,52*240);
    for(int y=0;y<240;y++)for(int x=0;x<bm->rowbytes;x++)output[y*52+x]=~bm->data[y*bm->rowbytes+x];
}
extern "C" void pd_native_shutdown(void) {pd_native_publish();native.reset();}
