#include "CrankHaptics.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"

namespace {
void DispatchHaptic(bool Cancel,bool CrankTick=false,float Strength=0) {
    JNIEnv* Env=AndroidJavaEnv::GetJavaEnv();
    if(!Env) return;
    jclass Class=AndroidJavaEnv::FindJavaClass("com.cranklab.CrankHaptics");
    if(!Env->ExceptionCheck() && Class) {
        jmethodID Method=Env->GetStaticMethodID(Class,Cancel ? "cancel" : "pulse",
            Cancel ? "(Landroid/app/Activity;)V" : "(Landroid/app/Activity;ZF)V");
        if(Method && !Env->ExceptionCheck()) {
            jobject Activity=AndroidJavaEnv::GetGameActivityThis();
            if(Cancel) Env->CallStaticVoidMethod(Class,Method,Activity);
            else Env->CallStaticVoidMethod(Class,Method,Activity,static_cast<jboolean>(CrankTick),static_cast<jfloat>(Strength));
        }
    }
    if(Env->ExceptionCheck()) {
        Env->ExceptionClear();
        UE_LOG(LogTemp,Warning,TEXT("Android haptic bridge failed (%s)"),Cancel ? TEXT("cancel") : TEXT("pulse"));
    }
    if(Class) Env->DeleteLocalRef(Class);
}
}
#endif

void FCrankHaptics::Pulse(bool CrankTick,float Strength) {
#if PLATFORM_ANDROID
    DispatchHaptic(false,CrankTick,FMath::Clamp(Strength,.1f,1.f));
#endif
}
void FCrankHaptics::Cancel() {
#if PLATFORM_ANDROID
    DispatchHaptic(true);
#endif
}
