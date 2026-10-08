#include "llama.h"
#include "ggml-backend.h"
#include "gguf.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <thread>
#ifdef __ANDROID__
#include <sys/stat.h>
#endif
extern "C" {
#include "deps/llama.cpp/vendor/hash/sha256/sha256.h"
}

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
using Cancel = int(*)(void*);
using Progress = void(*)(void*,int,int);
using Stream = void(*)(void*,const char*);
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
struct Control {Cancel cancel;void* data;};
static bool abort_decode(void* p) {auto* c=static_cast<Control*>(p);return c->cancel && c->cancel(c->data);}
static bool load_progress(float,void* p) {return !abort_decode(p);}
static void quiet_log(ggml_log_level,const char*,void*) {}
static int prefill_threads() {
#ifdef __ANDROID__
    static const int count=[] {
        const int cores=static_cast<int>(std::thread::hardware_concurrency());
        const int available=std::min(6,std::max(2,cores-2));
        std::vector<uint64_t> frequencies;
        for(int cpu=0;cpu<cores;++cpu) {
            std::ifstream file("/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/cpufreq/cpuinfo_max_freq");
            uint64_t frequency=0;if(!(file>>frequency) || frequency==0)return std::min(4,available);
            frequencies.push_back(frequency);
        }
        if(frequencies.empty())return 4;
        const auto peak=*std::max_element(frequencies.begin(),frequencies.end());
        // Equal-sized jobs stall behind little cores on heterogeneous phones.
        // Count the faster clusters, retaining a conservative fallback if sysfs
        // is unavailable. Never force affinity or change the supported CPU ISA.
        const int fast=static_cast<int>(std::count_if(frequencies.begin(),frequencies.end(),[peak](uint64_t f){return f*4>=peak*3;}));
        return std::min(available,std::max(2,fast));
    }();
    return count;
#else
    return 4;
#endif
}

struct Stamp {
    std::string path;uintmax_t size=0;std::filesystem::file_time_type time{};
#ifdef __ANDROID__
    uint64_t device=0,inode=0;int64_t changed=0,nanos=0;
#endif
    bool operator==(const Stamp& b) const {return path==b.path && size==b.size && time==b.time
#ifdef __ANDROID__
        && device==b.device && inode==b.inode && changed==b.changed && nanos==b.nanos
#endif
        ;}
};
static bool stamp(const char* path,Stamp& out) {
    if(!path || !*path)return false;
    std::error_code e;auto file=std::filesystem::u8path(path);
    out.path=std::filesystem::absolute(file,e).lexically_normal().u8string();if(e)return false;
    out.size=std::filesystem::file_size(file,e);if(e)return false;
    out.time=std::filesystem::last_write_time(file,e);if(e)return false;
#ifdef __ANDROID__
    struct stat info{};if(::stat(path,&info)!=0)return false;
    out.device=info.st_dev;out.inode=info.st_ino;out.changed=info.st_ctim.tv_sec;out.nanos=info.st_ctim.tv_nsec;
#endif
    return true;
}
static std::mutex verifyMutex,engineMutex;
static Stamp verifiedFile;static bool hasVerifiedFile=false;
struct Engine {
    Stamp file;std::string backend;Control control{};
    std::unique_ptr<llama_model,decltype(&llama_model_free)> model{nullptr,llama_model_free};
    std::unique_ptr<llama_context,decltype(&llama_free)> context{nullptr,llama_free};
    std::vector<llama_token> prefix;bool prefixReady=false;
};
static std::unique_ptr<Engine> engine;
static std::atomic<bool> releaseRequested{false};
EXPORT void crank_translation_release() {
    releaseRequested=true;std::unique_lock<std::mutex> guard(engineMutex,std::try_to_lock);
    if(guard.owns_lock()){engine.reset();releaseRequested=false;}
}
struct Stats {
    double verify=0,load=0,prefill=0,decode=0,firstOutput=-1,total=0;
    int input=0,output=0,prefixReused=0,batchThreads=4;bool reused=false;
};
static thread_local Stats lastStats;
EXPORT int crank_translation_stats(char* output,int capacity) {
    if(!output || capacity<2)return 1;const auto& s=lastStats;
    int n=std::snprintf(output,capacity,"{\"verifyMs\":%.2f,\"loadMs\":%.2f,\"prefillMs\":%.2f,\"decodeMs\":%.2f,\"firstOutputMs\":%.2f,\"totalMs\":%.2f,\"inputTokens\":%d,\"outputTokens\":%d,\"prefixTokensReused\":%d,\"threads\":4,\"batchThreads\":%d,\"modelReused\":%s}",s.verify,s.load,s.prefill,s.decode,s.firstOutput,s.total,s.input,s.output,s.prefixReused,s.batchThreads,s.reused?"true":"false");return n>=0 && n<capacity?0:1;
}
static size_t complete_utf8_size(const std::string& text) {
    if(text.empty())return 0;
    size_t lead=text.size()-1;while(lead>0 && (static_cast<unsigned char>(text[lead])&0xc0)==0x80)--lead;
    unsigned char b=text[lead];size_t n=b<0x80?1:(b&0xe0)==0xc0?2:(b&0xf0)==0xe0?3:(b&0xf8)==0xf0?4:1;
    return text.size()-lead<n?lead:text.size();
}

// Split long descriptions without cutting a UTF-8 codepoint or silently losing
// text. Each chunk replaces the previous source/output KV and checks its budget.
static std::vector<std::string> chunks(const std::string& text) {
    std::vector<std::string> out;
    size_t begin=0;
    while(begin<text.size()) {
        size_t end=std::min(begin+1600,text.size());
        if(end<text.size()) {
            size_t split=text.rfind("\n\n",end);
            if(split!=std::string::npos && split>begin+400) end=split+2;
            else {split=text.rfind(' ',end);if(split!=std::string::npos && split>begin+400) end=split+1;}
            while(end>begin && (static_cast<unsigned char>(text[end])&0xc0)==0x80) --end;
        }
        out.push_back(text.substr(begin,end-begin));begin=end;
    }
    return out;
}
static int fail(char* error,int capacity,const char* message,int code=1) {
    if(error && capacity>0) {std::strncpy(error,message,capacity-1);error[capacity-1]=0;}return code;
}
EXPORT int crank_translate_version() {return 2;}
EXPORT int crank_translation_verify_model(const char* path,Cancel cancel,void* data) {
    if(cancel && cancel(data))return 2;
    Stamp before;if(!stamp(path,before) || (before.size!=1133080448 && before.size!=461860800))return 1;
    std::lock_guard<std::mutex> guard(verifyMutex);
    if(cancel && cancel(data))return 2;
    if(hasVerifiedFile && verifiedFile==before)return 0;
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
    if(!file) return 1;
    sha256_t sha;sha256_init(&sha);std::vector<unsigned char> buffer(1024*1024);uint64_t total=0;
    while(file) {
        if(cancel && cancel(data)) return 2;
        file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());auto n=file.gcount();
        total+=n;sha256_update(&sha,buffer.data(),static_cast<size_t>(n));
    }
    Stamp after;if(!file.eof() || total!=before.size || !stamp(path,after) || !(before==after))return 1;
    unsigned char digest[32];sha256_final(&sha,digest);char hex[65];
    const char* digits="0123456789abcdef";
    for(int i=0;i<32;++i){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}hex[64]=0;
    const char* expected=total==461860800
        ? "cc497fe8f033b52b3b8b00a7669e9661435432f9d4cd43f7ed24400c01507a93"
        : "dc5f44fcf1fa496ee7ad725982c0c8c553a4de00259b53af84c4b89fb0c06699";
    if(std::strcmp(hex,expected)!=0)return 1;verifiedFile=after;hasVerifiedFile=true;return 0;
}
// The host retains one warm context while browsing and releases it on game
// launch, backgrounding, model switching, shutdown or 60 seconds of idle time.
static int run(const char* model_path,const char* backend_path,const char* text,
    char* output,int output_capacity,char* error,int error_capacity,
    Cancel cancel,Progress progress,Stream stream,void* data) {
    std::lock_guard<std::mutex> guard(engineMutex);lastStats=Stats();auto started=Clock::now();bool success=false;
    struct Finish {
        Clock::time_point started;bool& success;
        ~Finish(){lastStats.total=ms(started);if(engine)engine->control={};const bool release=releaseRequested.exchange(false);if(!success || release)engine.reset();}
    } finish{started,success};
    try {
        if(output && output_capacity>0)output[0]=0;
        if(!model_path || !backend_path || !text || !*text || std::strlen(text)>65536 || !output || output_capacity<2)
            return fail(error,error_capacity,"invalid_input");
        Control control{cancel,data};
        if(abort_decode(&control)) return fail(error,error_capacity,"cancelled",2);
        static std::once_flag init;
        std::call_once(init,[&]{llama_log_set(quiet_log,nullptr);ggml_backend_load_all_from_path(backend_path);llama_backend_init();});
        auto mp=llama_model_default_params();mp.load_mode=LLAMA_LOAD_MODE_MMAP;mp.n_gpu_layers=0;
        mp.progress_callback=load_progress;mp.progress_callback_user_data=&control;
        if(progress) progress(data,0,0);
        // The official lightweight file uses the old STQ enum 42, now Q2_0 in
        // upstream. Scope the read-only compatibility shim to a verified file.
        std::error_code ec;
        const bool legacy=std::filesystem::file_size(std::filesystem::u8path(model_path),ec)==461860800;
        {
            auto begin=Clock::now();int code=crank_translation_verify_model(model_path,cancel,data);lastStats.verify=ms(begin);
            if(code!=0)return fail(error,error_capacity,code==2?"cancelled":"model_invalid",code==2?2:1);
        }
        Stamp current;if(!stamp(model_path,current))return fail(error,error_capacity,"model_load_failed");
        if(engine && (!(engine->file==current) || engine->backend!=backend_path))engine.reset();
        lastStats.reused=bool(engine);auto loading=Clock::now();
        if(!engine){engine=std::make_unique<Engine>();engine->file=current;engine->backend=backend_path;engine->control=control;}
        engine->control=control;mp.progress_callback_user_data=&engine->control;
        auto load_model=[&]() {
            struct ScopedSTQ {
                ScopedSTQ(bool enabled){gguf_crank_legacy_stq1(enabled);}
                ~ScopedSTQ(){gguf_crank_legacy_stq1(false);}
            } scope(legacy);
            return llama_model_load_from_file(model_path,mp);
        };
        auto& model=engine->model;if(!model)model.reset(load_model());
        lastStats.load=ms(loading);
        if(!model) return fail(error,error_capacity,abort_decode(&control) ? "cancelled" : "model_load_failed",abort_decode(&control)?2:1);
        const auto* vocab=llama_model_get_vocab(model.get());
        auto parts=chunks(text);std::string result;
        for(size_t index=0;index<parts.size();++index) {
            if(abort_decode(&control)) return fail(error,error_capacity,"cancelled",2);
            if(progress) progress(data,static_cast<int>(index)+1,static_cast<int>(parts.size()));
            // Official Hy-MT2 user/assistant template. Never parse special
            // tokens in the externally supplied description itself.
            std::string instruction=u8"参考下面的翻译：\nPlaydate 翻译成 Playdate\ncrank 翻译成 摇柄\nD-pad 翻译成 方向键\n将以下文本翻译为中文，注意只需要输出翻译后的结果，不要额外解释：\n\n";
            auto tokenize=[&](const std::string& s,bool special) {
                int size=-llama_tokenize(vocab,s.data(),static_cast<int>(s.size()),nullptr,0,false,special);
                std::vector<llama_token> tokens(std::max(0,size));
                if(size>0 && llama_tokenize(vocab,s.data(),static_cast<int>(s.size()),tokens.data(),size,false,special)<0) tokens.clear();
                return tokens;
            };
            auto tokens=tokenize(u8"<｜hy_begin▁of▁sentence｜><｜hy_User｜>",true);
            auto fixed=tokenize(instruction,false);tokens.insert(tokens.end(),fixed.begin(),fixed.end());auto prefix=tokens;
            auto body=tokenize(parts[index],false);tokens.insert(tokens.end(),body.begin(),body.end());
            auto suffix=tokenize(u8"<｜hy_Assistant｜>",true);tokens.insert(tokens.end(),suffix.begin(),suffix.end());
            if(tokens.empty() || tokens.size()>900) return fail(error,error_capacity,"input_chunk_too_long");
            auto cp=llama_context_default_params();cp.n_ctx=2048;cp.n_batch=256;cp.n_ubatch=128;
            cp.n_threads=4;cp.n_threads_batch=prefill_threads();
            cp.abort_callback=abort_decode;cp.abort_callback_data=&engine->control;
            auto& ctx=engine->context;if(!ctx)ctx.reset(llama_init_from_model(model.get(),cp));lastStats.batchThreads=cp.n_threads_batch;
            if(!ctx) return fail(error,error_capacity,"context_failed");
            auto memory=llama_get_memory(ctx.get());size_t start=0;
            if(engine->prefixReady && engine->prefix==prefix && llama_memory_seq_rm(memory,0,static_cast<llama_pos>(prefix.size()),-1)) {start=prefix.size();lastStats.prefixReused+=static_cast<int>(start);}
            else {llama_memory_clear(memory,false);engine->prefix=prefix;engine->prefixReady=false;}
            auto prefill=Clock::now();lastStats.input+=static_cast<int>(tokens.size()-start);
            // n_batch is a hard limit, so prefill in bounded batches.
            for(size_t offset=start;offset<tokens.size();offset+=256) {
                auto batch=llama_batch_get_one(tokens.data()+offset,static_cast<int>(std::min(size_t(256),tokens.size()-offset)));
                if(llama_decode(ctx.get(),batch)!=0) return fail(error,error_capacity,abort_decode(&control)?"cancelled":"prefill_failed",abort_decode(&control)?2:1);
            }
            lastStats.prefill+=ms(prefill);engine->prefixReady=true;
            std::unique_ptr<llama_sampler,decltype(&llama_sampler_free)> sampler(llama_sampler_chain_init(llama_sampler_chain_default_params()),llama_sampler_free);
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_top_k(20));
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_top_p(.6f,1));
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_temp(.7f));
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_dist(3407));
            std::string translated;bool complete=false;auto decoding=Clock::now(),published=started;
            auto publish=[&]{if(stream){std::string partial=result+(result.empty()?"":"\n\n")+translated.substr(0,complete_utf8_size(translated));if(!partial.empty())stream(data,partial.c_str());published=Clock::now();}};
            for(int step=0;step<1100;++step) {
                if(abort_decode(&control)) return fail(error,error_capacity,"cancelled",2);
                auto token=llama_sampler_sample(sampler.get(),ctx.get(),-1);
                if(llama_vocab_is_eog(vocab,token)) {complete=true;break;}
                char buffer[256];int length=llama_token_to_piece(vocab,token,buffer,sizeof(buffer),0,false);
                if(length<0 || translated.size()+length>65536) return fail(error,error_capacity,"output_too_long");
                translated.append(buffer,length);
                ++lastStats.output;
                if(lastStats.firstOutput<0 && complete_utf8_size(translated)>0)lastStats.firstOutput=ms(started);
                if(stream && complete_utf8_size(translated)>0 && (step==0 || ms(published)>=600))publish();
                auto batch=llama_batch_get_one(&token,1);
                if(llama_decode(ctx.get(),batch)!=0) return fail(error,error_capacity,abort_decode(&control)?"cancelled":"decode_failed",abort_decode(&control)?2:1);
            }
            lastStats.decode+=ms(decoding);
            if(!complete || translated.empty()) return fail(error,error_capacity,"translation_incomplete");
            publish();
            if(!result.empty()) result+="\n\n";result+=translated;
        }
        if(result.size()+1>static_cast<size_t>(output_capacity)) return fail(error,error_capacity,"output_too_long");
        std::memcpy(output,result.c_str(),result.size()+1);success=true;return 0;
    } catch(...) {return fail(error,error_capacity,"inference_failed");}
}
EXPORT int crank_translate(const char* model,const char* backend,const char* text,char* output,int output_capacity,char* error,int error_capacity,Cancel cancel,Progress progress,void* data) {
    return run(model,backend,text,output,output_capacity,error,error_capacity,cancel,progress,nullptr,data);
}
EXPORT int crank_translate_stream(const char* model,const char* backend,const char* text,char* output,int output_capacity,char* error,int error_capacity,Cancel cancel,Progress progress,Stream stream,void* data) {
    return run(model,backend,text,output,output_capacity,error,error_capacity,cancel,progress,stream,data);
}
