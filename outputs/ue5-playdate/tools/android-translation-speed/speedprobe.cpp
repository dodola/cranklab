#include <jni.h>
#include <android/log.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
using Cancel=int(*)(void*);
using Progress=void(*)(void*,int,int);
using Stream=void(*)(void*,const char*);
using Run=int(*)(const char*,const char*,const char*,char*,int,char*,int,Cancel,Progress,void*);
using Live=int(*)(const char*,const char*,const char*,char*,int,char*,int,Cancel,Progress,Stream,void*);
static double seconds(Clock::time_point since){return std::chrono::duration<double>(Clock::now()-since).count();}
static std::string text(JNIEnv* env,jstring value){const char* chars=env->GetStringUTFChars(value,nullptr);std::string copy(chars);env->ReleaseStringUTFChars(value,chars);return copy;}
static bool utf8(const std::string& text) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(text.data());
    for(size_t i=0;i<text.size();) {
        unsigned char lead=bytes[i++];if(lead<0x80)continue;
        int continuation=lead>=0xc2 && lead<=0xdf?1:lead>=0xe0 && lead<=0xef?2:lead>=0xf0 && lead<=0xf4?3:-1;
        if(continuation<0 || i+continuation>text.size())return false;
        unsigned char next=bytes[i];
        if((lead==0xe0 && next<0xa0) || (lead==0xed && next>=0xa0) || (lead==0xf0 && next<0x90) || (lead==0xf4 && next>=0x90))return false;
        while(continuation-->0)if((bytes[i++]&0xc0)!=0x80)return false;
    }
    return true;
}
struct Control {Clock::time_point begin;double first=-1;int updates=0;bool valid=true;std::string partial;};
static int cancel(void* pointer){return seconds(static_cast<Control*>(pointer)->begin)>300?1:0;}
static void progress(void*,int part,int total){__android_log_print(ANDROID_LOG_INFO,"PlaydatePerf","Part %d/%d",part,total);}
static void stream(void* pointer,const char* value){auto& c=*static_cast<Control*>(pointer);if(c.first<0)c.first=seconds(c.begin);++c.updates;c.partial=value;c.valid&=utf8(c.partial);}
extern "C" JNIEXPORT jstring JNICALL Java_com_cranklab_SpeedProbe_runNative(JNIEnv* env,jclass,jstring modelValue,jstring backendValue,jstring bridgeValue,jbyteArray input,jint repeats) {
    auto model=text(env,modelValue),backend=text(env,backendValue),bridge=text(env,bridgeValue);
    std::string source(env->GetArrayLength(input),'\0');env->GetByteArrayRegion(input,0,static_cast<jsize>(source.size()),reinterpret_cast<jbyte*>(source.data()));
    void* library=dlopen(bridge.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!library)return env->NewStringUTF("[]");
    auto run=reinterpret_cast<Run>(dlsym(library,"crank_translate"));
    auto live=reinterpret_cast<Live>(dlsym(library,"crank_translate_stream"));
    auto stats=reinterpret_cast<int(*)(char*,int)>(dlsym(library,"crank_translation_stats"));
    auto release=reinterpret_cast<void(*)()>(dlsym(library,"crank_translation_release"));
    std::string report="[";
    if(run)for(int i=0;i<repeats;++i) {
        Control c{Clock::now()};std::vector<char> output(262144);char error[256]{},metrics[2048]{};
        int code=live?live(model.c_str(),backend.c_str(),source.c_str(),output.data(),output.size(),error,sizeof(error),cancel,progress,stream,&c)
            :run(model.c_str(),backend.c_str(),source.c_str(),output.data(),output.size(),error,sizeof(error),cancel,progress,&c);
        double total=seconds(c.begin);if(!stats || stats(metrics,sizeof(metrics))!=0)std::strcpy(metrics,"{}");
        bool valid=c.valid && utf8(output.data());bool match=live && c.partial==output.data();
        char record[4096];std::snprintf(record,sizeof(record),"%s{\"run\":%d,\"code\":%d,\"seconds\":%.3f,\"firstStreamSeconds\":%.3f,\"streamUpdates\":%d,\"streamAvailable\":%s,\"streamMatchesOutput\":%s,\"utf8Valid\":%s,\"inputBytes\":%zu,\"outputBytes\":%zu,\"error\":\"%s\",\"metrics\":%s}",i?",":"",i,code,total,c.first,c.updates,live?"true":"false",match?"true":"false",valid?"true":"false",source.size(),std::strlen(output.data()),error,metrics);
        report+=record;__android_log_print(ANDROID_LOG_INFO,"PlaydatePerf","Run %d: %.3f seconds, code %d",i,total,code);if(code!=0)break;
    }
    if(release)release();dlclose(library);report+="]";return env->NewStringUTF(report.c_str());
}
