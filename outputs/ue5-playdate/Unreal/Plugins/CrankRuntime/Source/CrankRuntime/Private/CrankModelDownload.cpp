#include "CrankModelDownload.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"

namespace CrankModelDownload {
namespace {
jclass DownloadClass(JNIEnv* Env) {
    jclass Class=AndroidJavaEnv::FindJavaClass("com.cranklab.CrankModelDownload");
    if(Env->ExceptionCheck()) {Env->ExceptionClear(); if(Class)Env->DeleteLocalRef(Class); return nullptr;}
    return Class;
}
FString ReadString(JNIEnv* Env,jobjectArray Array,int Index) {
    auto String=static_cast<jstring>(Env->GetObjectArrayElement(Array,Index));
    if(!String) return {};
    const char* Text=Env->GetStringUTFChars(String,nullptr); FString Value=Text ? UTF8_TO_TCHAR(Text) : TEXT("");
    if(Text)Env->ReleaseStringUTFChars(String,Text); Env->DeleteLocalRef(String); return Value;
}
void End(int64 Job,const char* Name) {
    JNIEnv* Env=AndroidJavaEnv::GetJavaEnv(); if(!Env)return;
    jclass Class=DownloadClass(Env); if(!Class)return;
    jmethodID Method=Env->GetStaticMethodID(Class,Name,"(J)V");
    if(Method)Env->CallStaticVoidMethod(Class,Method,static_cast<jlong>(Job));
    if(Env->ExceptionCheck())Env->ExceptionClear(); Env->DeleteLocalRef(Class);
}
}
bool HasWifi() {
    JNIEnv* Env=AndroidJavaEnv::GetJavaEnv(); if(!Env)return false;
    jclass Class=DownloadClass(Env); if(!Class)return false;
    jmethodID Method=Env->GetStaticMethodID(Class,"hasWifi","(Landroid/app/Activity;)Z");
    bool Allowed=Method && Env->CallStaticBooleanMethod(Class,Method,AndroidJavaEnv::GetGameActivityThis());
    if(Env->ExceptionCheck()){Env->ExceptionClear();Allowed=false;} Env->DeleteLocalRef(Class);return Allowed;
}
int64 Begin(const FString& Url,const FString& Partial,int64 Size) {
    JNIEnv* Env=AndroidJavaEnv::GetJavaEnv(); if(!Env)return -2;
    jclass Class=DownloadClass(Env); if(!Class)return -2;
    jmethodID Method=Env->GetStaticMethodID(Class,"begin","(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;J)J");
    jstring Address=Env->NewStringUTF(TCHAR_TO_UTF8(*Url)),Path=Env->NewStringUTF(TCHAR_TO_UTF8(*Partial));
    int64 Job=Method ? Env->CallStaticLongMethod(Class,Method,AndroidJavaEnv::GetGameActivityThis(),Address,Path,static_cast<jlong>(Size)) : -2;
    if(Env->ExceptionCheck()){Env->ExceptionClear();Job=-2;}
    Env->DeleteLocalRef(Address);Env->DeleteLocalRef(Path);Env->DeleteLocalRef(Class);return Job;
}
FSnapshot Poll(int64 Job) {
    FSnapshot Result; JNIEnv* Env=AndroidJavaEnv::GetJavaEnv();if(!Env)return Result;
    jclass Class=DownloadClass(Env);if(!Class)return Result;
    jmethodID Method=Env->GetStaticMethodID(Class,"snapshot","(J)[Ljava/lang/String;");
    auto Array=Method ? static_cast<jobjectArray>(Env->CallStaticObjectMethod(Class,Method,static_cast<jlong>(Job))) : nullptr;
    if(Array && Env->GetArrayLength(Array)>=3) {
        Result.State=FCString::Atoi(*ReadString(Env,Array,0));Result.Bytes=FCString::Atoi64(*ReadString(Env,Array,1));Result.Error=ReadString(Env,Array,2);
    }
    if(Env->ExceptionCheck()){Env->ExceptionClear();Result=FSnapshot();}
    if(Array)Env->DeleteLocalRef(Array);Env->DeleteLocalRef(Class);return Result;
}
void Cancel(int64 Job) {End(Job,"cancel");}
void Release(int64 Job) {End(Job,"release");}
}
#endif
