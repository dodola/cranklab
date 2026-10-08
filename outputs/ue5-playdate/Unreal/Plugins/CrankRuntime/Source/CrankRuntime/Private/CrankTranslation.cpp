#include "CrankTranslation.h"
#include "CrankGameFiles.h"
#include "CrankModelDownload.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"
#endif

namespace {
TSharedPtr<FJsonObject> Parse(const FString& Text) {TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
FString Field(const TSharedPtr<FJsonObject>& O,const TCHAR* Name) {FString V;if(O)O->TryGetStringField(Name,V);return V;}
FString Encode(const TSharedPtr<FJsonObject>& O) {FString T;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&T));return T;}
class FModelStream : public FArchive {
    TUniquePtr<FArchive> File;TSharedRef<TAtomic<int64>,ESPMode::ThreadSafe> Bytes;
    FCriticalSection Mutex;bool Closed=false;int64 Limit;
public:
    FModelStream(const FString& Path,TSharedRef<TAtomic<int64>,ESPMode::ThreadSafe> B,int64 Size):File(IFileManager::Get().CreateFileWriter(*Path)),Bytes(B),Limit(Size) {if(!File)SetError();}
    void Serialize(void* Data,int64 N) override {FScopeLock G(&Mutex);if(Closed || IsError() || N<0 || N>Limit-Bytes->Load()){SetError();return;}File->Serialize(Data,N);if(File->IsError())SetError();else *Bytes+=N;}
    bool Close() override {FScopeLock G(&Mutex);if(!Closed){Closed=true;if(File && !File->Close())SetError();File.Reset();}return !IsError();}
};
}
FCrankTranslation::FCrankTranslation() {
    Storage=FCrankGameFiles::Root()/TEXT("Translation");IFileManager::Get().MakeDirectory(*Storage,true);
    FString Settings;if(FFileHelper::LoadFileToString(Settings,*(Storage/TEXT("model.json"))))bLightModel=Field(Parse(Settings),TEXT("profile"))!=TEXT("q4");
#if !UE_BUILD_SHIPPING
    FString Profile,Override;
    if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationProfile="),Profile))bLightModel=Profile!=TEXT("q4");
    else if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationModel="),Override) && Override.Contains(TEXT("Q4_K_M")))bLightModel=false;
#endif
    ConfigureModel();
    // Recover only this feature's app-private staging files after process death.
    const FString ModelFolder=Storage/TEXT("Models");TArray<FString> Partials;
    IFileManager::Get().FindFiles(Partials,*(ModelFolder/TEXT("Hy-MT2-1.8B-*.gguf.part-*")),true,false);
    for(const FString& Name:Partials)IFileManager::Get().Delete(*(ModelFolder/Name),false,true);
}
void FCrankTranslation::ConfigureModel() {
    ModelSize=bLightModel?461860800:1133080448;
    ModelFile=bLightModel?TEXT("Hy-MT2-1.8B-1.25Bit.gguf"):TEXT("Hy-MT2-1.8B-Q4_K_M.gguf");
    // Tencent's ModelScope releases have the same bytes/hashes as the pinned
    // original artifacts. Keep cache identity independent of the download host.
    ModelUrl=bLightModel?TEXT("https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-1.25Bit-GGUF/resolve/a1ba5a8101d7b257d42c0980e0d6415d0bd9e1cf/Hy-MT2-1.8B-1.25Bit.gguf")
        :TEXT("https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/resolve/ef1d40b8b315575d30d1eb6579996130b8fc0bb2/Hy-MT2-1.8B-Q4_K_M.gguf");
    ModelPath=Storage/TEXT("Models")/ModelFile;
#if !UE_BUILD_SHIPPING
    FString Override;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationModel="),Override)) ModelPath=FPaths::ConvertRelativePathToFull(Override);
#endif
    bModelReady=IFileManager::Get().FileSize(*ModelPath)==ModelSize;
    bModelVerified=false;ModelStatus.Empty();
    ModelState=bModelReady?TEXT("ready"):TEXT("missing");
}
bool FCrankTranslation::SelectModel(bool Light) {
    if(bClosed || IsModelBusy())return false;
    if(Light==bLightModel)return true;
    Cancel();ReleaseModel();bLightModel=Light;ConfigureModel();ModelPartial.Empty();
    Translated.Empty();State=Source.IsEmpty()?TEXT("idle"):TEXT("ready");Status.Empty();ReadTranslationCache();
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("profile"),Light?TEXT("stq1"):TEXT("q4"));
    if(!SaveJson(Storage/TEXT("model.json"),Encode(O)))ModelStatus=TEXT("模型已切换，但设置未能保存。");
    return true;
}
FCrankTranslation::~FCrankTranslation(){Shutdown();}
FString FCrankTranslation::Normalize(const FString& Text) {
    FString Clean=Text;Clean.ReplaceInline(TEXT("\r\n"),TEXT("\n"));Clean.ReplaceInline(TEXT("\r"),TEXT("\n"));
    Clean.ReplaceInline(TEXT("&nbsp;"),TEXT(" "));Clean.ReplaceInline(TEXT("&amp;"),TEXT("&"));
    return Clean.TrimStartAndEnd();
}
FString FCrankTranslation::CacheKey(int32 CatalogId,const FString& Text,bool Light) {
    FTCHARToUTF8 Bytes(*(FString::FromInt(CatalogId)+(Light?TEXT("|hy-mt2-stq1-cc497fe8|zh-CN|prompt-1|"):TEXT("|hy-mt2-q4-dc5f44fc|zh-CN|prompt-1|"))+Normalize(Text)));
    FMD5 Hash;Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());uint8 Digest[16];Hash.Final(Digest);return BytesToHex(Digest,16).ToLower();
}
bool FCrankTranslation::SaveJson(const FString& Path,const FString& Text) {
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const FString Temp=Path+TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*Path,*Temp,true,true);
}
bool FCrankTranslation::LoadNative() {
    if(NativeHandle)return true;
#if PLATFORM_WINDOWS
    BackendPath=IPluginManager::Get().FindPlugin(TEXT("CrankRuntime"))->GetBaseDir()/TEXT("Binaries/ThirdParty/Translation/Win64");
    if(!FPaths::FileExists(BackendPath/TEXT("crank-translate.dll")))BackendPath=FPlatformProcess::BaseDir();
    FPlatformProcess::PushDllDirectory(*BackendPath);NativeHandle=FPlatformProcess::GetDllHandle(*(BackendPath/TEXT("crank-translate.dll")));FPlatformProcess::PopDllDirectory(*BackendPath);
#elif PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv();
    if(Env) {
        jobject Activity=AndroidJavaEnv::GetGameActivityThis();jclass ActivityClass=Env->GetObjectClass(Activity);
        jmethodID Method=Env->GetMethodID(ActivityClass,"getApplicationInfo","()Landroid/content/pm/ApplicationInfo;");
        jobject Info=Env->CallObjectMethod(Activity,Method);jclass InfoClass=Env->GetObjectClass(Info);
        jfieldID FieldId=Env->GetFieldID(InfoClass,"nativeLibraryDir","Ljava/lang/String;");jstring Path=static_cast<jstring>(Env->GetObjectField(Info,FieldId));
        const char* S=Env->GetStringUTFChars(Path,nullptr);BackendPath=UTF8_TO_TCHAR(S);Env->ReleaseStringUTFChars(Path,S);
        Env->DeleteLocalRef(Path);Env->DeleteLocalRef(InfoClass);Env->DeleteLocalRef(Info);Env->DeleteLocalRef(ActivityClass);
    }
    NativeHandle=FPlatformProcess::GetDllHandle(TEXT("libcrank-translate.so"));
#endif
    if(!NativeHandle)return false;
    NativeTranslate=reinterpret_cast<FTranslate>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translate")));
    NativeVerify=reinterpret_cast<FVerify>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_verify_model")));
    NativeTranslateStream=reinterpret_cast<FTranslateStream>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translate_stream")));
    NativeRelease=reinterpret_cast<FRelease>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_release")));
    NativeStats=reinterpret_cast<FStats>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_stats")));
    if(NativeTranslate && NativeVerify)return true;
    FPlatformProcess::FreeDllHandle(NativeHandle);NativeHandle=nullptr;return false;
}
void FCrankTranslation::Open(int32 CatalogId) {
    if(bClosed || CatalogId<=0)return;
    Cancel();bTranslationRequested=true;Id=CatalogId;Title=TEXT("游戏介绍");Source.Empty();Translated.Empty();State=TEXT("loading");Status=TEXT("正在读取游戏介绍…");
    const uint64 Version=Revision;FString Cached;
    if(FFileHelper::LoadFileToString(Cached,*(Storage/TEXT("Descriptions")/(FString::FromInt(Id)+TEXT(".json"))))) {
        auto O=Parse(Cached);Title=Field(O,TEXT("title"));Source=Normalize(Field(O,TEXT("description")));ReadTranslationCache();
    }
    State=TEXT("loading");Status=Translated.IsEmpty()?TEXT("正在读取游戏介绍…"):TEXT("正在更新介绍 · 已保存译文仍可阅读");
    FString DetailUrl=FString::Printf(TEXT("https://pdgamez.com/game/%d.json"),Id);
#if !UE_BUILD_SHIPPING
    FString Base;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationDetailBase="),Base))DetailUrl=Base/FString::Printf(TEXT("game/%d.json"),Id);
#endif
    auto R=FHttpModule::Get().CreateRequest();DetailRequest=R;R->SetURL(DetailUrl);R->SetVerb(TEXT("GET"));R->SetTimeout(30);
    R->OnRequestProgress64().BindLambda([](FHttpRequestPtr Q,uint64,uint64 N){if(N>1024*1024)Q->CancelRequest();});
    R->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version](FHttpRequestPtr,FHttpResponsePtr Response,bool Ok) {
        auto S=Weak.Pin();if(!S || S->bClosed || Version!=S->Revision)return;S->DetailRequest.Reset();
        if(Ok && Response && EHttpResponseCodes::IsOk(Response->GetResponseCode()) && Response->GetContentLength()<=1024*1024) {S->AcceptDescription(Response->GetContentAsString());return;}
        if(!S->Source.IsEmpty()) {S->State=S->Translated.IsEmpty()?TEXT("ready"):TEXT("translated");S->Status=TEXT("网络不可用，显示已保存的介绍。");if(S->Translated.IsEmpty() && S->HasModel())S->Translate();}
        else {S->State=TEXT("error");S->Status=TEXT("读取介绍失败，请检查网络后重试。");}
    });
    if(!R->ProcessRequest()){DetailRequest.Reset();State=TEXT("error");Status=TEXT("无法读取介绍，请重试。");}
}
void FCrankTranslation::AcceptDescription(const FString& Text) {
    auto O=Parse(Text);double CatalogId=0;
    if(!O || !O->TryGetNumberField(TEXT("id"),CatalogId) || int32(CatalogId)!=Id){State=TEXT("error");Status=TEXT("游戏介绍数据无效，请重试。");return;}
    Title=Field(O,TEXT("title"));Source=Normalize(Field(O,TEXT("description")));Translated.Empty();
    if(Source.Len()>24000){State=TEXT("error");Status=TEXT("介绍过长，暂时无法翻译。原文仍可阅读。");return;}
    // Persist only public fields used by this feature.
    auto Public=MakeShared<FJsonObject>();Public->SetStringField(TEXT("title"),Title);Public->SetStringField(TEXT("description"),Source);
    SaveJson(Storage/TEXT("Descriptions")/(FString::FromInt(Id)+TEXT(".json")),Encode(Public));
    if(Source.IsEmpty()){State=TEXT("empty");Status=TEXT("这个游戏暂时没有介绍。");return;}
    State=TEXT("ready");Status.Empty();
    if(!ReadTranslationCache() && bModelReady)Translate();
}
bool FCrankTranslation::ReadTranslationCache() {
    if(Source.IsEmpty())return false;
    FString Text;const FString Key=CacheKey(Id,Source,bLightModel);
    if(FFileHelper::LoadFileToString(Text,*(Storage/TEXT("Cache")/(Key+TEXT(".json"))))) {
        auto O=Parse(Text);if(Field(O,TEXT("key"))==Key && !Field(O,TEXT("translation")).IsEmpty()) {
            Translated=Field(O,TEXT("translation"));State=TEXT("translated");Status=TEXT("已读取本地译文");return true;
        }
    }return false;
}
void FCrankTranslation::Translate() {
    if(bClosed || Task.IsValid() || Source.IsEmpty() || State==TEXT("loading"))return;
    bTranslationRequested=true;if(ReadTranslationCache())return;
    if(!bModelReady){State=TEXT("ready");Status=TEXT("下载翻译模型后即可在本机翻译。");return;}
    if(!LoadNative()){State=TEXT("error");Status=TEXT("翻译组件无法加载，请重新安装应用。");return;}
    State=TEXT("translating");Status=TEXT("正在准备本地翻译…");TaskCache=CacheKey(Id,Source,bLightModel);
    Control=MakeShared<FControl,ESPMode::ThreadSafe>();const auto C=Control;const uint64 Version=++Revision;
    WarmModelDeadline=0;
    const FString M=ModelPath,B=BackendPath,Input=Source;const bool Verified=bModelVerified;const auto Run=NativeTranslate;const auto Verify=NativeVerify;const auto Live=NativeTranslateStream;const auto Stats=NativeStats;
    Task=Async(EAsyncExecution::Thread,[M,B,Input,C,Version,Verified,Run,Verify,Live,Stats]() {
        FResult R;R.Version=Version;
        auto Stop=+[](void* P)->int{return static_cast<FControl*>(P)->Cancelled.Load()?1:0;};
        auto Progress=+[](void* P,int Part,int Total){auto* X=static_cast<FControl*>(P);X->Part=Part;X->Total=Total;};
        auto Stream=+[](void* P,const char* Text){auto* X=static_cast<FControl*>(P);if(X->Cancelled.Load())return;FScopeLock Lock(&X->StreamMutex);X->Partial=UTF8_TO_TCHAR(Text);};
        const double VerifyStart=FPlatformTime::Seconds();
        if(!Verified && Verify(TCHAR_TO_UTF8(*M),Stop,C.Get())!=0){R.Error=C->Cancelled.Load()?TEXT("cancelled"):TEXT("model_invalid");return R;}
        R.VerifyMs=(FPlatformTime::Seconds()-VerifyStart)*1000;
        R.ModelVerified=true;TArray<ANSICHAR> Output;Output.SetNumZeroed(262144);ANSICHAR Error[256]{};
        const int Code=Live?Live(TCHAR_TO_UTF8(*M),TCHAR_TO_UTF8(*B),TCHAR_TO_UTF8(*Input),Output.GetData(),Output.Num(),Error,UE_ARRAY_COUNT(Error),Stop,Progress,Stream,C.Get())
            :Run(TCHAR_TO_UTF8(*M),TCHAR_TO_UTF8(*B),TCHAR_TO_UTF8(*Input),Output.GetData(),Output.Num(),Error,UE_ARRAY_COUNT(Error),Stop,Progress,C.Get());
        ANSICHAR Metrics[2048]{};if(Stats && Stats(Metrics,UE_ARRAY_COUNT(Metrics))==0)R.Metrics=UTF8_TO_TCHAR(Metrics);
        if(Code==0)R.Text=FString(UTF8_TO_TCHAR(Output.GetData())).TrimStartAndEnd();else R.Error=UTF8_TO_TCHAR(Error);return R;
    });
}
void FCrankTranslation::Cancel() {
    ++Revision;bTranslationRequested=false;if(Control)Control->Cancelled=true;
    if(DetailRequest){DetailRequest->CancelRequest();DetailRequest.Reset();}
    if(State==TEXT("loading") || State==TEXT("translating")){if(State==TEXT("translating"))Translated.Empty();State=TEXT("ready");Status=TEXT("已取消，可再次翻译。");}
}
void FCrankTranslation::ReleaseModel() {WarmModelDeadline=0;if(NativeRelease)NativeRelease();}
void FCrankTranslation::DownloadModel() {
    if(bClosed || bModelReady || IsModelBusy())return;
#if PLATFORM_ANDROID
    if(!CrankModelDownload::HasWifi()){ModelState=TEXT("wifi");ModelStatus=TEXT("模型仅通过 Wi-Fi 下载，请连接可联网的 Wi-Fi 后重试。");return;}
#endif
    if(!LoadNative()){ModelState=TEXT("error");ModelStatus=TEXT("翻译组件无法加载，请重新安装应用。");return;}
    const FString Folder=FPaths::GetPath(ModelPath);IFileManager::Get().MakeDirectory(*Folder,true);
    uint64 Total=0,Free=0;if(FPlatformMisc::GetDiskTotalAndFreeSpace(Folder,Total,Free) && Free<uint64(ModelSize+128*1024*1024)){ModelState=TEXT("error");ModelStatus=bLightModel?TEXT("存储空间不足，请至少留出 600 MB 后重试。"):TEXT("存储空间不足，请至少留出 1.3 GB 后重试。");return;}
    const uint64 Version=++ModelRevision;const FString Partial=ModelPath+FString::Printf(TEXT(".part-%llu"),Version);ModelPartial=Partial;
    ModelBytes=MakeShared<TAtomic<int64>,ESPMode::ThreadSafe>(0);
    ModelState=TEXT("downloading");ModelStatus.Empty();ModelControl=MakeShared<FControl,ESPMode::ThreadSafe>();
    FString Url=ModelUrl;
#if !UE_BUILD_SHIPPING
    FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationModelUrl="),Url);
#endif
#if PLATFORM_ANDROID
    AndroidModelJob=CrankModelDownload::Begin(Url,Partial,ModelSize);ModelPollTime=0;
    if(AndroidModelJob<=0) {
        ModelState=AndroidModelJob==-1?TEXT("wifi"):TEXT("error");
        ModelStatus=AndroidModelJob==-1?TEXT("模型仅通过 Wi-Fi 下载，请连接可联网的 Wi-Fi 后重试。"):TEXT("无法开始下载，请重试。");AndroidModelJob=0;
    }
#else
    auto Stream=MakeShared<FModelStream>(Partial,ModelBytes.ToSharedRef(),ModelSize);ModelStream=Stream;
    if(Stream->IsError()){ModelState=TEXT("error");ModelStatus=TEXT("无法写入模型，请检查剩余空间后重试。");ModelStream.Reset();return;}
    auto R=FHttpModule::Get().CreateRequest();ModelRequest=R;R->SetURL(Url);R->SetVerb(TEXT("GET"));R->SetTimeout(1800);R->SetActivityTimeout(45);
    R->SetResponseBodyReceiveStream(Stream);
    R->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version,Stream,Partial](FHttpRequestPtr,FHttpResponsePtr Response,bool Ok) {
        const bool Closed=Stream->Close();auto S=Weak.Pin();
        if(!S || S->bClosed || Version!=S->ModelRevision){IFileManager::Get().Delete(*Partial,false,true);return;}
        S->ModelStream.Reset();S->ModelRequest.Reset();
        if(!Ok || !Closed || !Response || !EHttpResponseCodes::IsOk(Response->GetResponseCode()) || S->ModelBytes->Load()!=S->ModelSize) {
            IFileManager::Get().Delete(*Partial,false,true);S->ModelState=TEXT("error");S->ModelStatus=TEXT("模型下载失败，请检查网络和空间后重试。");return;
        }
        S->VerifyDownloadedModel(Partial);
    });
    if(!R->ProcessRequest()){CancelModelDownload();ModelState=TEXT("error");ModelStatus=TEXT("无法开始下载，请重试。");}
#endif
}
void FCrankTranslation::VerifyDownloadedModel(const FString& Partial) {
    if(!ModelBytes || ModelBytes->Load()!=ModelSize || IFileManager::Get().FileSize(*Partial)!=ModelSize) {
        IFileManager::Get().Delete(*Partial,false,true);ModelState=TEXT("error");ModelStatus=TEXT("模型文件不完整，请重新下载。");return;
    }
    ModelState=TEXT("verifying");const auto Verify=NativeVerify;const auto C=ModelControl;
    ModelCheck=Async(EAsyncExecution::Thread,[Partial,Verify,C] {return Verify(TCHAR_TO_UTF8(*Partial),+[](void* P)->int{return static_cast<FControl*>(P)->Cancelled.Load()?1:0;},C.Get())==0;});
}
void FCrankTranslation::CancelModelDownload() {
    ++ModelRevision;if(ModelControl)ModelControl->Cancelled=true;
#if PLATFORM_ANDROID
    if(AndroidModelJob){CrankModelDownload::Cancel(AndroidModelJob);AndroidModelJob=0;ModelPartial.Empty();}
#endif
    if(ModelRequest){ModelRequest->CancelRequest();ModelRequest.Reset();}
    if(ModelStream){ModelStream->Close();ModelStream.Reset();}
    if(!ModelCheck.IsValid() && !ModelPartial.IsEmpty())IFileManager::Get().Delete(*ModelPartial,false,true);
    ModelState=bModelReady?TEXT("ready"):TEXT("missing");ModelStatus=TEXT("已取消下载，可重新下载。");
}
void FCrankTranslation::Tick() {
    if(WarmModelDeadline>0 && FPlatformTime::Seconds()>=WarmModelDeadline && !Task.IsValid())ReleaseModel();
    if(Task.IsValid() && Control && !Control->Cancelled.Load() && State==TEXT("translating")) {
        FScopeLock Lock(&Control->StreamMutex);if(!Control->Partial.IsEmpty())Translated=Control->Partial;
    }
#if PLATFORM_ANDROID
    if(AndroidModelJob && FPlatformTime::Seconds()>=ModelPollTime) {
        ModelPollTime=FPlatformTime::Seconds()+.1;
        const auto Download=CrankModelDownload::Poll(AndroidModelJob);
        if(ModelBytes)ModelBytes->Store(FMath::Clamp(Download.Bytes,int64(0),ModelSize));
        if(Download.State!=1) {
            CrankModelDownload::Release(AndroidModelJob);AndroidModelJob=0;
            if(Download.State==2)VerifyDownloadedModel(ModelPartial);
            else {
                ModelState=Download.State==3?TEXT("wifi"):TEXT("error");
                ModelStatus=Download.State==3?TEXT("Wi-Fi 已断开，下载已停止。连接 Wi-Fi 后可重试。"):TEXT("模型下载失败，请检查网络和空间后重试。");
            }
        }
    }
#endif
    if(Task.IsValid() && Task.IsReady()) {
        auto R=Task.Get();Task=TFuture<FResult>();Control.Reset();
        WarmModelDeadline=FPlatformTime::Seconds()+60;
        if(R.Version==Revision && !R.Metrics.IsEmpty())UE_LOG(LogTemp,Display,TEXT("Crank translation metrics · profile=%s · hostVerifyMs=%.2f · %s"),bLightModel?TEXT("stq1"):TEXT("q4"),R.VerifyMs,*R.Metrics);
        if(R.Version==Revision && !bClosed) {
            bModelVerified=R.ModelVerified;
            if(!R.Error.IsEmpty() || R.Text.IsEmpty()) {
                Translated.Empty();
                State=TEXT("error");Status=R.Error==TEXT("model_invalid") ? TEXT("模型校验失败，请重新下载。") : TEXT("翻译未完成，请重试；原文仍可阅读。");
                if(R.Error==TEXT("model_invalid")){bModelReady=false;ModelState=TEXT("error");ModelStatus=Status;}
            } else {
                Translated=R.Text;State=TEXT("translated");auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("key"),TaskCache);O->SetStringField(TEXT("translation"),Translated);
                Status=SaveJson(Storage/TEXT("Cache")/(TaskCache+TEXT(".json")),Encode(O))?TEXT("本地翻译完成 · 已缓存"):TEXT("本地翻译完成，但缓存未能保存。");
            }
        } else if(!bClosed && bTranslationRequested && State==TEXT("ready") && bModelReady && !Source.IsEmpty()) Translate();
    }
    if(ModelCheck.IsValid() && ModelCheck.IsReady()) {
        const bool Ok=ModelCheck.Get();ModelCheck=TFuture<bool>();const FString Partial=ModelPartial;
        if(!bClosed && ModelState==TEXT("verifying") && Ok && IFileManager::Get().Move(*ModelPath,*Partial,true,true)) {bModelReady=bModelVerified=true;ModelState=TEXT("ready");ModelStatus=TEXT("模型已就绪，可离线翻译。");if(bTranslationRequested && !Source.IsEmpty() && State==TEXT("ready"))Translate();}
        else {IFileManager::Get().Delete(*Partial,false,true);if(ModelState==TEXT("verifying")){ModelState=TEXT("error");ModelStatus=TEXT("模型校验失败，请重新下载。");}}
    }
}
FString FCrankTranslation::Message() const {
    if(State==TEXT("translating") && Control && Control->Total.Load()>0)return FString::Printf(TEXT("正在本机翻译 · 第 %d / %d 段"),Control->Part.Load(),Control->Total.Load());
    return Status;
}
FString FCrankTranslation::ModelMessage() const {
    if(ModelState==TEXT("downloading")) {
        const int64 Bytes=ModelBytes?FMath::Clamp(ModelBytes->Load(),int64(0),ModelSize):0;
        if(!Bytes)return TEXT("正在连接模型下载…");
        const int Percent=FMath::FloorToInt(100.0*Bytes/ModelSize);
        return FString::Printf(TEXT("下载模型 · %d%% · %.1f / %.1f MiB"),Percent,Bytes/1048576.0,ModelSize/1048576.0);
    }
    if(ModelState==TEXT("verifying"))return TEXT("正在校验模型文件…");
    if(!ModelStatus.IsEmpty())return ModelStatus;
    return bModelReady?(bLightModel?TEXT("1.25Bit 轻量模型已就绪 · 支持离线翻译"):TEXT("Q4 模型已就绪 · 支持离线翻译"))
        :(bLightModel?TEXT("轻量版约 440 MiB；仅通过 Wi-Fi 下载，之后可离线翻译。"):TEXT("Q4 版约 1.06 GiB；仅通过 Wi-Fi 下载，之后可离线翻译。"));
}
FString FCrankTranslation::ModelDownloadLabel() const {return bLightModel?TEXT("下载440M"):TEXT("下载1G");}
TOptional<float> FCrankTranslation::ModelProgress() const {return ModelState==TEXT("downloading") && ModelBytes && ModelSize>0?TOptional<float>(float(FMath::Clamp(double(ModelBytes->Load())/ModelSize,0.0,1.0))):TOptional<float>();}
void FCrankTranslation::Shutdown() {
    if(bClosed)return;bClosed=true;Cancel();CancelModelDownload();
    if(Task.IsValid())Task.Wait();if(ModelCheck.IsValid())ModelCheck.Wait();
    ReleaseModel();
    if(!ModelPartial.IsEmpty())IFileManager::Get().Delete(*ModelPartial,false,true);
    if(NativeHandle){FPlatformProcess::FreeDllHandle(NativeHandle);NativeHandle=nullptr;}
}
