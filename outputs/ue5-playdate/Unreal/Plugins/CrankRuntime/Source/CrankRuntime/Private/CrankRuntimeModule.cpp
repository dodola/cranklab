#include "CrankNative.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"
#endif
FCrankNative& FCrankNative::Get() { static FCrankNative Native; return Native; }
bool FCrankNative::Load() {
    if (Handle) return true;
#if PLATFORM_WINDOWS
    FString Path = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("CrankRuntime"))->GetBaseDir(), TEXT("Binaries/ThirdParty/Win64"));
    if (!FPaths::FileExists(FPaths::Combine(Path, TEXT("crank-core.dll")))) Path = FPlatformProcess::BaseDir();
    FPlatformProcess::PushDllDirectory(*Path);
    Handle = FPlatformProcess::GetDllHandle(*FPaths::Combine(Path, TEXT("crank-core.dll")));
    FPlatformProcess::PopDllDirectory(*Path);
    if (!Handle) return false;
#elif PLATFORM_ANDROID
    Handle = FPlatformProcess::GetDllHandle(TEXT("libcrank-core.so"));
    if (!Handle) return false;
    auto Init = reinterpret_cast<int(*)(void*,void*,void*)>(FPlatformProcess::GetDllExport(Handle,TEXT("crank_android_init")));
    JNIEnv* Env = AndroidJavaEnv::GetJavaEnv();
    JavaVM* Vm = nullptr;
    jclass HttpClass = Env ? AndroidJavaEnv::FindJavaClass("com.cranklab.CrankHttp") : nullptr;
    if (Env) Env->GetJavaVM(&Vm);
    const bool bInitialized = Init && Env && Vm && HttpClass && Init(Vm,Env,HttpClass);
    if (HttpClass) Env->DeleteLocalRef(HttpClass);
    if (!bInitialized) { if(Env && Env->ExceptionCheck()) Env->ExceptionClear(); FPlatformProcess::FreeDllHandle(Handle); Handle=nullptr; return false; }
#endif
#if PLATFORM_WINDOWS || PLATFORM_ANDROID
#define CRANK_BIND(Field, Name) Field = reinterpret_cast<decltype(Field)>(FPlatformProcess::GetDllExport(Handle, TEXT(Name)))
    CRANK_BIND(Start,"crank_start"); CRANK_BIND(Stop,"crank_stop"); CRANK_BIND(Status,"crank_status");
    CRANK_BIND(StartLauncher,"crank_start_launcher"); CRANK_BIND(LauncherRequest,"crank_launcher_request");
    CRANK_BIND(LauncherReply,"crank_launcher_reply"); CRANK_BIND(LauncherActivate,"crank_launcher_activate"); CRANK_BIND(LauncherDetailAction,"crank_launcher_detail_action");
    CRANK_BIND(Buttons,"crank_set_buttons"); CRANK_BIND(Crank,"crank_add_crank");
    CRANK_BIND(Frame,"crank_copy_frame"); CRANK_BIND(Error,"crank_last_error");
    CRANK_BIND(NativeInfo,"crank_copy_native_info");
    CRANK_BIND(Audio,"crank_copy_audio");
    CRANK_BIND(Suspend,"crank_set_suspended");
    CRANK_BIND(Menu,"crank_copy_menu"); CRANK_BIND(MenuImage,"crank_copy_menu_image");
    CRANK_BIND(MenuOpen,"crank_set_menu_open"); CRANK_BIND(MenuSelect,"crank_menu_select");
#undef CRANK_BIND
    if (Start && StartLauncher && LauncherRequest && LauncherReply && LauncherActivate && LauncherDetailAction && Stop && Status && Suspend && Buttons && Crank && Frame && Audio && Error && NativeInfo && Menu && MenuImage && MenuOpen && MenuSelect) return true;
    FPlatformProcess::FreeDllHandle(Handle); Handle = nullptr;
#endif
    return false;
}
void FCrankNative::Unload() {
    if (Handle) { if (Stop) Stop(); FPlatformProcess::FreeDllHandle(Handle); Handle = nullptr; }
}
class FCrankRuntimeModule : public IModuleInterface {
public:
    void ShutdownModule() override { FCrankNative::Get().Unload(); }
};
IMPLEMENT_MODULE(FCrankRuntimeModule, CrankRuntime)
