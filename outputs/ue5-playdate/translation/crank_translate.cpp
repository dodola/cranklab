#include "llama.h"
#include "ggml-backend.h"
#include "gguf.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
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
struct Control {Cancel cancel;void* data;};
static bool abort_decode(void* p) {auto* c=static_cast<Control*>(p);return c->cancel && c->cancel(c->data);}
static bool load_progress(float,void* p) {return !abort_decode(p);}
static void quiet_log(ggml_log_level,const char*,void*) {}

// Split long descriptions without cutting a UTF-8 codepoint or silently losing
// text. Each chunk receives its own context and completion-budget check.
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
EXPORT int crank_translate_version() {return 1;}
EXPORT int crank_translation_verify_model(const char* path,Cancel cancel,void* data) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
    if(!file) return 1;
    sha256_t sha;sha256_init(&sha);std::vector<unsigned char> buffer(1024*1024);uint64_t total=0;
    while(file) {
        if(cancel && cancel(data)) return 2;
        file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());auto n=file.gcount();
        total+=n;sha256_update(&sha,buffer.data(),static_cast<size_t>(n));
    }
    if(!file.eof() || (total!=1133080448 && total!=461860800)) return 1;
    unsigned char digest[32];sha256_final(&sha,digest);char hex[65];
    const char* digits="0123456789abcdef";
    for(int i=0;i<32;++i){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}hex[64]=0;
    const char* expected=total==461860800
        ? "cc497fe8f033b52b3b8b00a7669e9661435432f9d4cd43f7ed24400c01507a93"
        : "dc5f44fcf1fa496ee7ad725982c0c8c553a4de00259b53af84c4b89fb0c06699";
    return std::strcmp(hex,expected)==0 ? 0 : 1;
}
// Serialized by the UE service. All model/context objects are freed at the end
// of a job, including cancellation: no 1+ GiB resident model while playing.
EXPORT int crank_translate(const char* model_path,const char* backend_path,const char* text,
    char* output,int output_capacity,char* error,int error_capacity,
    Cancel cancel,Progress progress,void* data) {
    static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
    try {
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
        if(legacy && crank_translation_verify_model(model_path,cancel,data)!=0)
            return fail(error,error_capacity,abort_decode(&control)?"cancelled":"model_invalid",abort_decode(&control)?2:1);
        auto load_model=[&]() {
            struct ScopedSTQ {
                ScopedSTQ(bool enabled){gguf_crank_legacy_stq1(enabled);}
                ~ScopedSTQ(){gguf_crank_legacy_stq1(false);}
            } scope(legacy);
            return llama_model_load_from_file(model_path,mp);
        };
        std::unique_ptr<llama_model,decltype(&llama_model_free)> model(load_model(),llama_model_free);
        if(!model) return fail(error,error_capacity,abort_decode(&control) ? "cancelled" : "model_load_failed",abort_decode(&control)?2:1);
        const auto* vocab=llama_model_get_vocab(model.get());
        auto parts=chunks(text);std::string result;
        for(size_t index=0;index<parts.size();++index) {
            if(abort_decode(&control)) return fail(error,error_capacity,"cancelled",2);
            if(progress) progress(data,static_cast<int>(index)+1,static_cast<int>(parts.size()));
            // Official Hy-MT2 user/assistant template. Never parse special
            // tokens in the externally supplied description itself.
            std::string instruction=u8"参考下面的翻译：\nPlaydate 翻译成 Playdate\ncrank 翻译成 摇柄\nD-pad 翻译成 方向键\n将以下文本翻译为中文，注意只需要输出翻译后的结果，不要额外解释：\n\n";
            instruction+=parts[index];
            auto tokenize=[&](const std::string& s,bool special) {
                int size=-llama_tokenize(vocab,s.data(),static_cast<int>(s.size()),nullptr,0,false,special);
                std::vector<llama_token> tokens(std::max(0,size));
                if(size>0 && llama_tokenize(vocab,s.data(),static_cast<int>(s.size()),tokens.data(),size,false,special)<0) tokens.clear();
                return tokens;
            };
            auto tokens=tokenize(u8"<｜hy_begin▁of▁sentence｜><｜hy_User｜>",true);
            auto body=tokenize(instruction,false);tokens.insert(tokens.end(),body.begin(),body.end());
            auto suffix=tokenize(u8"<｜hy_Assistant｜>",true);tokens.insert(tokens.end(),suffix.begin(),suffix.end());
            if(tokens.empty() || tokens.size()>900) return fail(error,error_capacity,"input_chunk_too_long");
            auto cp=llama_context_default_params();cp.n_ctx=2048;cp.n_batch=256;cp.n_ubatch=128;
            cp.n_threads=4;cp.n_threads_batch=4;cp.abort_callback=abort_decode;cp.abort_callback_data=&control;
            std::unique_ptr<llama_context,decltype(&llama_free)> ctx(llama_init_from_model(model.get(),cp),llama_free);
            if(!ctx) return fail(error,error_capacity,"context_failed");
            // n_batch is a hard limit, so prefill in bounded batches.
            for(size_t offset=0;offset<tokens.size();offset+=256) {
                auto batch=llama_batch_get_one(tokens.data()+offset,static_cast<int>(std::min(size_t(256),tokens.size()-offset)));
                if(llama_decode(ctx.get(),batch)!=0) return fail(error,error_capacity,abort_decode(&control)?"cancelled":"prefill_failed",abort_decode(&control)?2:1);
            }
            std::unique_ptr<llama_sampler,decltype(&llama_sampler_free)> sampler(llama_sampler_chain_init(llama_sampler_chain_default_params()),llama_sampler_free);
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_top_k(20));
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_top_p(.6f,1));
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_temp(.7f));
            llama_sampler_chain_add(sampler.get(),llama_sampler_init_dist(3407));
            std::string translated;bool complete=false;
            for(int step=0;step<1100;++step) {
                if(abort_decode(&control)) return fail(error,error_capacity,"cancelled",2);
                auto token=llama_sampler_sample(sampler.get(),ctx.get(),-1);
                if(llama_vocab_is_eog(vocab,token)) {complete=true;break;}
                char buffer[256];int length=llama_token_to_piece(vocab,token,buffer,sizeof(buffer),0,false);
                if(length<0 || translated.size()+length>65536) return fail(error,error_capacity,"output_too_long");
                translated.append(buffer,length);
                auto batch=llama_batch_get_one(&token,1);
                if(llama_decode(ctx.get(),batch)!=0) return fail(error,error_capacity,abort_decode(&control)?"cancelled":"decode_failed",abort_decode(&control)?2:1);
            }
            if(!complete || translated.empty()) return fail(error,error_capacity,"translation_incomplete");
            if(!result.empty()) result+="\n\n";result+=translated;
        }
        if(result.size()+1>static_cast<size_t>(output_capacity)) return fail(error,error_capacity,"output_too_long");
        std::memcpy(output,result.c_str(),result.size()+1);return 0;
    } catch(...) {return fail(error,error_capacity,"inference_failed");}
}
