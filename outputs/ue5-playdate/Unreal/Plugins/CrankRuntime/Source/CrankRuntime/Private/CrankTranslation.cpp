#include "CrankTranslation.h"
#include "CrankGameFiles.h"
#include "CrankModelDownload.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeLock.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_ANDROID
#include "Android/AndroidJavaEnv.h"
#endif

namespace {
TSharedPtr<FJsonObject> Parse(const FString& Text) {TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
FString Field(const TSharedPtr<FJsonObject>& O,const TCHAR* Name) {FString V;if(O)O->TryGetStringField(Name,V);return V;}
FString Encode(const TSharedPtr<FJsonObject>& O) {FString T;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&T));return T;}
bool ValidProvider(const FString& P) {return P==TEXT("auto") || P==TEXT("local") || P==TEXT("microsoft") || P==TEXT("tencent");}
// Q4_0 runs on the Hexagon NPU, Adreno or CPU; the 1.25-bit file needs a type remap and is not offered.
constexpr int64 LocalModelSize=1079997408;
const TCHAR* LocalModelFile=TEXT("Hy-MT2-1.8B-Q4_0.gguf");
// Same bytes and SHA-256 (5458acf2...) as the unsloth/Hugging Face file, served from a mainland CDN.
const TCHAR* LocalModelUrl=TEXT("https://modelscope.cn/models/unsloth/Hy-MT2-1.8B-GGUF/resolve/eaa43c0c98c505ffe6759528221a123dfe70383a/Hy-MT2-1.8B-Q4_0.gguf");
// Without a first character after this long, an automatic request moves to the online services.
constexpr double LocalFirstOutputSeconds=30.0;
}
FCrankTranslation::FCrankTranslation() {
    Storage=FCrankGameFiles::Root()/TEXT("Translation");IFileManager::Get().MakeDirectory(*Storage,true);
    FString Settings;
    if(FFileHelper::LoadFileToString(Settings,*(Storage/TEXT("provider.json")))) {
        const FString Saved=Field(Parse(Settings),TEXT("provider"));if(ValidProvider(Saved) && (Saved!=TEXT("local") || LocalSupported()))Provider=Saved;
    }
#if !UE_BUILD_SHIPPING
    FString Override;
    if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationProvider="),Override) && ValidProvider(Override))Provider=Override;
#endif
    // Remove only this feature's obsolete, regenerable app-private models.
    // User-selected files, descriptions, game saves and translation caches stay.
    const FString Models=Storage/TEXT("Models");
    for(const TCHAR* Name:{TEXT("Hy-MT2-1.8B-1.25Bit.gguf"),TEXT("Hy-MT2-1.8B-Q4_K_M.gguf")}) {
        IFileManager::Get().Delete(*(Models/Name),false,true);
        TArray<FString> Partials;IFileManager::Get().FindFiles(Partials,*(Models/(FString(Name)+TEXT(".part-*"))),true,false);
        for(const FString& Partial:Partials)IFileManager::Get().Delete(*(Models/Partial),false,true);
    }
    IFileManager::Get().Delete(*(Storage/TEXT("model.json")),false,true);
    // Recover only this feature's app-private staging files after process death.
    TArray<FString> Staged;IFileManager::Get().FindFiles(Staged,*(Models/(FString(LocalModelFile)+TEXT(".part-*"))),true,false);
    for(const FString& Partial:Staged)IFileManager::Get().Delete(*(Models/Partial),false,true);
    ConfigureModel();
}
bool FCrankTranslation::LocalSupported() {
#if PLATFORM_ANDROID
    return true;
#else
    return false;
#endif
}
FString FCrankTranslation::FirstProvider(const FString& Selected,bool bLocalReady) {
    if(Selected==TEXT("local"))return TEXT("local");
    if(Selected==TEXT("auto"))return bLocalReady?TEXT("local"):TEXT("microsoft");
    return Selected;
}
bool FCrankTranslation::FallsBackToOnline(const FString& Selected) {return Selected==TEXT("auto");}
void FCrankTranslation::ConfigureModel() {
    ModelPath=Storage/TEXT("Models")/LocalModelFile;
#if !UE_BUILD_SHIPPING
    FString Override;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationModel="),Override))ModelPath=FPaths::ConvertRelativePathToFull(Override);
#endif
    bModelReady=LocalSupported() && IFileManager::Get().FileSize(*ModelPath)==LocalModelSize;
    bModelVerified=bModelReady && ReadVerifiedMarker();
    ModelStatus.Empty();ModelState=bModelReady?TEXT("ready"):TEXT("missing");
}
bool FCrankTranslation::ReadVerifiedMarker() const {
    FString Text;if(!FFileHelper::LoadFileToString(Text,*(ModelPath+TEXT(".verified"))))return false;
    auto O=Parse(Text);double Size=0;
    return O && O->TryGetNumberField(TEXT("size"),Size) && Field(O,TEXT("modified"))==FString::Printf(TEXT("%lld"),IFileManager::Get().GetTimeStamp(*ModelPath).GetTicks())
        && int64(Size)==LocalModelSize && IFileManager::Get().FileSize(*ModelPath)==LocalModelSize;
}
void FCrankTranslation::WriteVerifiedMarker() const {
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("size"),double(LocalModelSize));
    O->SetStringField(TEXT("modified"),FString::Printf(TEXT("%lld"),IFileManager::Get().GetTimeStamp(*ModelPath).GetTicks()));
    FFileHelper::SaveStringToFile(Encode(O),*(ModelPath+TEXT(".verified")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
FCrankTranslation::~FCrankTranslation(){Shutdown();}
FString FCrankTranslation::Normalize(const FString& Text) {
    FString Clean=Text;Clean.ReplaceInline(TEXT("\r\n"),TEXT("\n"));Clean.ReplaceInline(TEXT("\r"),TEXT("\n"));
    Clean.ReplaceInline(TEXT("&nbsp;"),TEXT(" "));Clean.ReplaceInline(TEXT("&amp;"),TEXT("&"));return Clean.TrimStartAndEnd();
}
FString FCrankTranslation::CacheKey(int32 CatalogId,const FString& Text,const FString& P) {
    // The local key names the model so a different quantization never reuses these translations.
    const FString Identity=P==TEXT("local")?TEXT("|hy-mt2-q4_0-5458acf2|zh-CN|prompt-1|"):TEXT("|online-")+P+TEXT("|zh-CN|terms-2|");
    FTCHARToUTF8 Bytes(*(FString::FromInt(CatalogId)+Identity+Normalize(Text)));
    FMD5 Hash;Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());uint8 Digest[16];Hash.Final(Digest);return BytesToHex(Digest,16).ToLower();
}
bool FCrankTranslation::SaveJson(const FString& Path,const FString& Text) {
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);const FString Temp=Path+TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*Path,*Temp,true,true);
}
bool FCrankTranslation::SelectProvider(const FString& P) {
    if(bClosed || !ValidProvider(P) || (P==TEXT("local") && !LocalSupported()))return false;if(P==Provider)return true;
    CancelTranslationWork();Provider=P;Translated.Empty();ActiveProvider.Empty();
    State=DetailRequest.IsValid()?TEXT("loading"):Source.IsEmpty()?TEXT("idle"):TEXT("ready");Status.Empty();ReadTranslationCache();
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("provider"),Provider);
    if(!SaveJson(Storage/TEXT("provider.json"),Encode(O))){Status=TEXT("翻译服务已切换，但设置未能保存。");return false;}return true;
}
FString FCrankTranslation::ProviderMessage() const {
    return Provider==TEXT("auto")?(LocalSupported()?TEXT("模型已下载时在本机翻译，失败或超时改用微软、腾讯在线翻译。已缓存译文可离线阅读。"):TEXT("优先使用微软；不可用时尝试腾讯。首次翻译需联网，已缓存译文可离线阅读。"))
        :Provider==TEXT("local")?TEXT("只在本机翻译，介绍文本不会发送到网络。需先下载约 1 GiB 模型。")
        :Provider==TEXT("microsoft")?TEXT("使用微软在线翻译。无需下载模型，已缓存译文可离线阅读。")
        :TEXT("使用腾讯在线翻译。无需下载模型，已缓存译文可离线阅读。");
}
void FCrankTranslation::Open(int32 CatalogId) {
    if(bClosed || CatalogId<=0)return;
    Cancel();bTranslationRequested=true;Id=CatalogId;Title=TEXT("游戏介绍");Source.Empty();Translated.Empty();State=TEXT("loading");Status=TEXT("正在读取游戏介绍…");
    const uint64 Version=Revision;FString Cached;
    if(FFileHelper::LoadFileToString(Cached,*(Storage/TEXT("Descriptions")/(FString::FromInt(Id)+TEXT(".json"))))) {
        auto O=Parse(Cached);Title=Field(O,TEXT("title"));Source=Normalize(Field(O,TEXT("description")));
        if(Source.Len()>24000){State=TEXT("error");Status=TEXT("介绍过长，暂时无法翻译。原文仍可阅读。");}
        else if(!Source.IsEmpty() && !ReadTranslationCache()){State=TEXT("ready");Translate();}
    }
    FString Url=FString::Printf(TEXT("https://pdgamez.com/game/%d.json"),Id);
#if !UE_BUILD_SHIPPING
    FString Base;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationDetailBase="),Base))Url=Base/FString::Printf(TEXT("game/%d.json"),Id);
#endif
    auto R=FHttpModule::Get().CreateRequest();DetailRequest=R;R->SetURL(Url);R->SetVerb(TEXT("GET"));R->SetTimeout(8);
    R->OnRequestProgress64().BindLambda([](FHttpRequestPtr Q,uint64,uint64 N){if(N>1024*1024)Q->CancelRequest();});
    R->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version](FHttpRequestPtr,FHttpResponsePtr Response,bool Ok) {
        auto S=Weak.Pin();if(!S || S->bClosed || Version!=S->Revision)return;S->DetailRequest.Reset();
        if(Ok && Response && Response->GetResponseCode()==200 && Response->GetContent().Num()<=1024*1024){S->AcceptDescription(Response->GetContentAsString());return;}
        if(!S->Source.IsEmpty()) {
            if(S->State==TEXT("translated"))S->Status=TEXT("网络不可用，显示已保存译文。");
            else if(S->State!=TEXT("translating") && S->State!=TEXT("error")){S->State=TEXT("ready");if(S->bTranslationRequested)S->Translate();}
        }else{S->State=TEXT("error");S->Status=TEXT("读取介绍失败，请检查网络后重试。");}
    });
    if(!R->ProcessRequest()) {DetailRequest.Reset();if(Source.IsEmpty()){State=TEXT("error");Status=TEXT("无法读取介绍，请重试。");}}
}
void FCrankTranslation::AcceptDescription(const FString& Text) {
    auto O=Parse(Text);double CatalogId=0;
    if(!O || !O->TryGetNumberField(TEXT("id"),CatalogId) || CatalogId!=Id){if(Source.IsEmpty()){State=TEXT("error");Status=TEXT("游戏介绍数据无效，请重试。");}return;}
    Title=Field(O,TEXT("title"));const FString Next=Normalize(Field(O,TEXT("description")));
    if(Next==Source && (State==TEXT("translating") || State==TEXT("translated") || State==TEXT("error")))return;
    InvalidateTranslation();
    Source=Next;Translated.Empty();
    if(Source.Len()>24000){State=TEXT("error");Status=TEXT("介绍过长，暂时无法翻译。原文仍可阅读。");return;}
    auto Public=MakeShared<FJsonObject>();Public->SetStringField(TEXT("title"),Title);Public->SetStringField(TEXT("description"),Source);
    SaveJson(Storage/TEXT("Descriptions")/(FString::FromInt(Id)+TEXT(".json")),Encode(Public));
    if(Source.IsEmpty()){State=TEXT("empty");Status=TEXT("这个游戏暂时没有介绍。");return;}
    State=TEXT("ready");Status.Empty();if(!ReadTranslationCache() && bTranslationRequested)Translate();
}
bool FCrankTranslation::ReadTranslationCache() {
    if(Source.IsEmpty())return false;
    const TArray<FString> Providers=Provider==TEXT("auto")?TArray<FString>{TEXT("local"),TEXT("microsoft"),TEXT("tencent")}:TArray<FString>{Provider};
    for(const FString& P:Providers) {
        FString Text;const FString Key=CacheKey(Id,Source,P);
        if(FFileHelper::LoadFileToString(Text,*(Storage/TEXT("Cache")/(Key+TEXT(".json"))))) {
            auto O=Parse(Text);if(Field(O,TEXT("key"))==Key && !Field(O,TEXT("translation")).IsEmpty()) {
                Translated=Field(O,TEXT("translation"));ActiveProvider=P;State=TEXT("translated");Status=TEXT("已读取本地译文");return true;
            }
        }
    }return false;
}
FCrankTranslation::FPart FCrankTranslation::Protect(const FString& Text) {
    FPart P;
    const FRegexPattern Pattern(TEXT("(?i)https?://[^\\s<>()]+|\\bplay\\.date/[a-zA-Z0-9/._-]+|\\b[\\w.-]+\\.pdx(?:\\.zip)?\\b|\\bundock the crank\\b|\\bdock the crank\\b|\\bPlaydate\\b|\\bD-pad\\b|[ⒶⒷ✛▲▼◀▶]"));
    FRegexMatcher M(Pattern,Text);int32 Last=0;
    FString Prefix=TEXT("ZXQK");while(Text.Contains(Prefix))Prefix+=TEXT("X");
    while(M.FindNext()) {
        P.Text+=Text.Mid(Last,M.GetMatchBeginning()-Last);FString Value=M.GetCaptureGroup(0);
        if(Value.Equals(TEXT("D-pad"),ESearchCase::IgnoreCase))Value=TEXT("方向键");
        else if(Value.Equals(TEXT("Playdate"),ESearchCase::IgnoreCase))Value=TEXT("Playdate");
        else if(Value.Equals(TEXT("undock the crank"),ESearchCase::IgnoreCase))Value=TEXT("展开摇柄");
        else if(Value.Equals(TEXT("dock the crank"),ESearchCase::IgnoreCase))Value=TEXT("收起摇柄");
        const FString Marker=FString::Printf(TEXT("%s%03dQXZ"),*Prefix,P.Keep.Num());P.Keep.Emplace(Marker,Value);P.Text+=Marker;Last=M.GetMatchEnding();
    }
    P.Text+=Text.Mid(Last);return P;
}
bool FCrankTranslation::Restore(const FPart& P,FString& Text) {
    for(const auto& Keep:P.Keep) {
        int32 Digit=0;while(Digit<Keep.Key.Len() && !FChar::IsDigit(Keep.Key[Digit]))++Digit;
        const FString Pattern=TEXT("(?i)")+Keep.Key.Left(Digit)+TEXT("\\s*")+Keep.Key.Mid(Digit,Keep.Key.Len()-Digit-3)+TEXT("\\s*QXZ");
        FRegexMatcher M(FRegexPattern(Pattern),Text);
        if(!M.FindNext())return false;const int32 Start=M.GetMatchBeginning(),End=M.GetMatchEnding();
        if(M.FindNext())return false;
        Text=Text.Left(Start)+Keep.Value+Text.Mid(End);
    }
    Text.ReplaceInline(TEXT("曲柄"),TEXT("摇柄"));return true;
}
void FCrankTranslation::Translate() {
    if(bClosed || TranslationRequest || Source.IsEmpty() || Source.Len()>24000)return;
    bTranslationRequested=true;if(ReadTranslationCache())return;
    if(Task.IsValid()) {
        // A cancelled local run is still winding down; do not start a second one beside it.
        if(Provider==TEXT("local")){State=TEXT("ready");Status=TEXT("上一次本地翻译正在结束，请稍后重试。");return;}
        if(!FallsBackToOnline(Provider))return;
    }
    const FString First=FirstProvider(Provider,LocalReady() && !Task.IsValid());
    if(First==TEXT("local")) {
        if(!LocalReady()){State=TEXT("ready");Status=LocalSupported()?TEXT("请先在设置中下载本地翻译模型。"):TEXT("此设备不支持本地翻译，请选择在线翻译。");return;}
        StartLocal();return;
    }
    if(Control)Control->Cancelled=true;
    BuildParts();
    Attempt=0;Translated.Empty();ActiveProvider.Empty();State=TEXT("translating");Status=TEXT("正在联网翻译…");StartTranslationRequest(++TranslationRevision);
}
void FCrankTranslation::BuildParts() {
    Parts.Reset();int32 Start=0;
    while(Start<Source.Len()) {
        int32 End=FMath::Min(Start+3500,Source.Len());
        if(End<Source.Len()) {
            const int32 Break=Source.Find(TEXT("\n\n"),ESearchCase::CaseSensitive,ESearchDir::FromEnd,End);
            if(Break>Start+500)End=Break+2;
            else {const int32 Space=Source.Find(TEXT(" "),ESearchCase::CaseSensitive,ESearchDir::FromEnd,End);if(Space>Start+500)End=Space+1;}
            if(End>Start && uint32(Source[End-1])>=0xd800 && uint32(Source[End-1])<=0xdbff)--End;
        }
        FPart Part=Protect(Source.Mid(Start,End-Start));
        // Placeholders can expand glyph-heavy descriptions; bound the actual
        // payload as well as the source. Keep UTF-16 pairs together.
        while(Part.Text.Len()>3500 && End>Start+1) {
            End=Start+FMath::Max(1,(End-Start)*3500/Part.Text.Len());
            if(uint32(Source[End-1])>=0xd800 && uint32(Source[End-1])<=0xdbff) {if(End>Start+1)--End;else ++End;}
            Part=Protect(Source.Mid(Start,End-Start));
        }
        int32 Tail=End;while(Tail>Start && FChar::IsWhitespace(Source[Tail-1]))--Tail;
        Part.JoinAfter=Source.Mid(Tail,End-Tail);Parts.Add(MoveTemp(Part));Start=End;
    }
}
void FCrankTranslation::InvalidateTranslation() {
    // Anything started before this point is stale; stop the native run too instead of letting it finish unseen.
    ++TranslationRevision;if(Control)Control->Cancelled=true;
    if(TranslationRequest){TranslationRequest->CancelRequest();TranslationRequest.Reset();}
}
void FCrankTranslation::CancelTranslationWork() {
    const bool WasTranslating=State==TEXT("translating");
    InvalidateTranslation();
    if(WasTranslating){bTranslationRequested=false;State=Source.IsEmpty()?TEXT("idle"):TEXT("ready");Translated.Empty();Status=TEXT("已取消翻译，原文仍可阅读。");}
}
void FCrankTranslation::FallBackToOnline(const TCHAR* Reason) {
    UE_LOG(LogTemp,Display,TEXT("Crank translation · local run abandoned (%s), trying online"),Reason);
    if(Control)Control->Cancelled=true;
    BuildParts();Attempt=0;Translated.Empty();ActiveProvider.Empty();State=TEXT("translating");Status=TEXT("本地翻译未完成，改用在线翻译…");
    StartTranslationRequest(++TranslationRevision);
}
void FCrankTranslation::StartLocal() {
    if(!LoadNative()){
        if(FallsBackToOnline(Provider)){FallBackToOnline(TEXT("native"));return;}
        State=TEXT("error");Status=TEXT("翻译组件无法加载，请重新安装应用。");return;
    }
    ActiveProvider=TEXT("local");State=TEXT("translating");Status=TEXT("正在准备本地翻译…");Translated.Empty();
    TaskCache=CacheKey(Id,Source,TEXT("local"));LocalStarted=FPlatformTime::Seconds();WarmModelDeadline=0;
    Control=MakeShared<FControl,ESPMode::ThreadSafe>();const auto C=Control;const uint64 Version=++TranslationRevision;
    const FString M=ModelPath,B=BackendPath,Input=Source;const bool Verified=bModelVerified;
    const auto Live=NativeTranslateStream;const auto Stats=NativeStats;
    Task=Async(EAsyncExecution::Thread,[M,B,Input,C,Version,Verified,Live,Stats]() {
        FResult R;R.Version=Version;
        auto Stop=+[](void* P)->int{return static_cast<FControl*>(P)->Cancelled.Load()?1:0;};
        auto Progress=+[](void* P,int Part,int Total){auto* X=static_cast<FControl*>(P);X->Part=Part;X->Total=Total;};
        auto Stream=+[](void* P,const char* Text){auto* X=static_cast<FControl*>(P);if(X->Cancelled.Load())return;FScopeLock Lock(&X->StreamMutex);X->Partial=UTF8_TO_TCHAR(Text);};
        TArray<ANSICHAR> Output;Output.SetNumZeroed(262144);ANSICHAR Error[256]{};
        // The native call verifies the model itself (SHA-256, cached per process; trusted from the marker).
        const int Code=Live(TCHAR_TO_UTF8(*M),TCHAR_TO_UTF8(*B),TCHAR_TO_UTF8(*Input),Output.GetData(),Output.Num(),Error,UE_ARRAY_COUNT(Error),Stop,Progress,Stream,C.Get());
        ANSICHAR Metrics[2048]{};if(Stats && Stats(Metrics,UE_ARRAY_COUNT(Metrics))==0)R.Metrics=UTF8_TO_TCHAR(Metrics);
        R.ModelVerified=Code==0; // only a finished translation proves the file was hashed
        if(Code==0)R.Text=FString(UTF8_TO_TCHAR(Output.GetData())).TrimStartAndEnd();else R.Error=UTF8_TO_TCHAR(Error);
        return R;
    });
}
void FCrankTranslation::FinishLocal(FResult R) {
    WarmModelDeadline=FPlatformTime::Seconds()+60;
    if(!R.Metrics.IsEmpty())UE_LOG(LogTemp,Display,TEXT("Crank translation metrics · provider=local · %s"),*R.Metrics);
    if(bClosed || R.Version!=TranslationRevision)return;
    if(R.ModelVerified && !bModelVerified){bModelVerified=true;WriteVerifiedMarker();}
    if(R.Error==TEXT("cancelled"))return;
    if(R.Error.IsEmpty() && !R.Text.IsEmpty()) {
        bool Chinese=false;for(TCHAR C:R.Text)Chinese|=C>=0x4e00 && C<=0x9fff;
        if(Chinese) {
            Translated=R.Text;State=TEXT("translated");auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("key"),TaskCache);O->SetStringField(TEXT("translation"),Translated);O->SetStringField(TEXT("provider"),TEXT("local"));
            Status=SaveJson(Storage/TEXT("Cache")/(TaskCache+TEXT(".json")),Encode(O))?TEXT("本地翻译完成 · 已缓存"):TEXT("本地翻译完成，但缓存未能保存。");return;
        }
    }
    if(R.Error==TEXT("model_invalid")){
        bModelReady=bModelVerified=false;ModelState=TEXT("error");ModelStatus=TEXT("模型校验失败，请重新下载。");
        IFileManager::Get().Delete(*(ModelPath+TEXT(".verified")),false,true);
    }
    if(FallsBackToOnline(Provider)){FallBackToOnline(*R.Error);return;}
    Translated.Empty();State=TEXT("error");Status=R.Error==TEXT("model_invalid")?TEXT("模型校验失败，请重新下载。"):TEXT("本地翻译未完成，请重试；原文仍可阅读。");
}
void FCrankTranslation::StartTranslationRequest(uint64 Version) {
    if(bClosed || Version!=TranslationRevision)return;
    ActiveProvider=Provider==TEXT("auto")?(Attempt==0?TEXT("microsoft"):TEXT("tencent")):Provider;
    FString Body,Url;TArray<TSharedPtr<FJsonValue>> Texts;
    for(const auto& P:Parts)Texts.Add(MakeShared<FJsonValueString>(P.Text));
    if(ActiveProvider==TEXT("microsoft")) {
        FJsonSerializer::Serialize(Texts,TJsonWriterFactory<>::Create(&Body));
        Url=TEXT("https://edge.microsoft.com/translate/translatetext?from=en&to=zh-Hans&isEnterpriseClient=false");
    }else {
        Url=TEXT("https://transmart.qq.com/api/imt");auto O=MakeShared<FJsonObject>();auto Header=MakeShared<FJsonObject>();
        Header->SetStringField(TEXT("fn"),TEXT("auto_translation"));Header->SetStringField(TEXT("client_key"),TEXT("browser-chrome-110.0.0-Android-")+FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens)+TEXT("-")+LexToString(FDateTime::UtcNow().ToUnixTimestamp()*1000));
        O->SetObjectField(TEXT("header"),Header);O->SetStringField(TEXT("type"),TEXT("plain"));O->SetStringField(TEXT("model_category"),TEXT("normal"));
        auto Input=MakeShared<FJsonObject>();Input->SetArrayField(TEXT("text_list"),Texts);Input->SetStringField(TEXT("lang"),TEXT("en"));O->SetObjectField(TEXT("source"),Input);
        auto Target=MakeShared<FJsonObject>();Target->SetStringField(TEXT("lang"),TEXT("zh"));O->SetObjectField(TEXT("target"),Target);Body=Encode(O);
    }
#if !UE_BUILD_SHIPPING
    FString Base;if(FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationApiBase="),Base))Url=Base/ActiveProvider;
#endif
    auto R=FHttpModule::Get().CreateRequest();TranslationRequest=R;R->SetURL(Url);R->SetVerb(TEXT("POST"));R->SetTimeout(8);R->SetHeader(TEXT("Content-Type"),TEXT("application/json; charset=utf-8"));
    R->SetHeader(TEXT("User-Agent"),TEXT("Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/110.0.0.0 Mobile Safari/537.36"));
    if(ActiveProvider==TEXT("tencent"))R->SetHeader(TEXT("Referer"),TEXT("https://transmart.qq.com/zh-CN/index"));R->SetContentAsString(Body);RequestStarted=FPlatformTime::Seconds();
    R->OnRequestProgress64().BindLambda([](FHttpRequestPtr Q,uint64,uint64 N){if(N>1024*1024)Q->CancelRequest();});
    R->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version,P=ActiveProvider](FHttpRequestPtr,FHttpResponsePtr Response,bool Ok) {
        auto S=Weak.Pin();if(!S || S->bClosed || Version!=S->TranslationRevision)return;S->TranslationRequest.Reset();
        TArray<FString> Output;
        if(Ok && Response && Response->GetResponseCode()==200 && Response->GetContent().Num()<=1024*1024) {
            const FString Raw=Response->GetContentAsString();const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
            if(P==TEXT("microsoft")) {
                TArray<TSharedPtr<FJsonValue>> Array;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw),Array) && Array.Num()==S->Parts.Num())for(const auto& Value:Array) {
                    const auto O=Value->Type==EJson::Object?Value->AsObject():nullptr;
                    if(O && O->TryGetArrayField(TEXT("translations"),Items) && Items->Num()==1) {
                        const auto T=(*Items)[0]->Type==EJson::Object?(*Items)[0]->AsObject():nullptr;
                        if(Field(T,TEXT("to"))==TEXT("zh-Hans"))Output.Add(Field(T,TEXT("text")));
                    }
                }
            }else {
                const auto O=Parse(Raw);const TSharedPtr<FJsonObject>* Header=nullptr;
                if(O && O->TryGetObjectField(TEXT("header"),Header) && Field(*Header,TEXT("ret_code"))==TEXT("succ") && O->TryGetArrayField(TEXT("auto_translation"),Items) && Items->Num()==S->Parts.Num())
                    for(const auto& Item:*Items)if(Item->Type==EJson::String)Output.Add(Item->AsString());
            }
        }
        bool Valid=Output.Num()==S->Parts.Num();FString Completed;
        if(Valid)for(int32 I=0;I<Output.Num();++I) {
            Valid&=!Output[I].TrimStartAndEnd().IsEmpty() && Restore(S->Parts[I],Output[I]);Completed+=Output[I].TrimStartAndEnd()+S->Parts[I].JoinAfter;
        }
        bool Chinese=false;for(TCHAR C:Completed)Chinese|=C>=0x4e00 && C<=0x9fff;Valid&=Chinese;
        if(!Valid){S->TranslationFailed(Version);return;}
        S->Translated=Completed.TrimStartAndEnd();S->State=TEXT("translated");const FString Key=CacheKey(S->Id,S->Source,P);auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("key"),Key);O->SetStringField(TEXT("translation"),S->Translated);O->SetStringField(TEXT("provider"),P);
        S->Status=S->SaveJson(S->Storage/TEXT("Cache")/(Key+TEXT(".json")),Encode(O))?TEXT("在线翻译完成 · 已缓存"):TEXT("翻译已完成，但缓存未能保存。");
        UE_LOG(LogTemp,Display,TEXT("Crank online translation · provider=%s · elapsedMs=%.1f · parts=%d"),*P,(FPlatformTime::Seconds()-S->RequestStarted)*1000,S->Parts.Num());
    });
    if(!R->ProcessRequest()){TranslationRequest.Reset();TranslationFailed(Version);}
}
void FCrankTranslation::TranslationFailed(uint64 Version) {
    if(bClosed || Version!=TranslationRevision)return;
    if(Provider==TEXT("auto") && Attempt++==0){Status=TEXT("正在尝试备用翻译服务…");StartTranslationRequest(Version);return;}
    State=TEXT("error");Translated.Empty();Status=TEXT("在线翻译暂时不可用，请检查网络后重试；原文仍可阅读。");
}
void FCrankTranslation::Cancel() {
    ++Revision;++TranslationRevision;bTranslationRequested=false;if(Control)Control->Cancelled=true;
    if(DetailRequest){DetailRequest->CancelRequest();DetailRequest.Reset();}
    if(TranslationRequest){TranslationRequest->CancelRequest();TranslationRequest.Reset();}
    if(State==TEXT("loading") || State==TEXT("translating")){State=Source.IsEmpty()?TEXT("idle"):TEXT("ready");Translated.Empty();Status=TEXT("已取消翻译，原文仍可阅读。");}
}
bool FCrankTranslation::LoadNative() {
    if(NativeHandle)return true;
#if PLATFORM_ANDROID
    auto* Env=AndroidJavaEnv::GetJavaEnv();
    if(Env) {
        jobject Activity=AndroidJavaEnv::GetGameActivityThis();jclass ActivityClass=Env->GetObjectClass(Activity);
        jmethodID Method=Env->GetMethodID(ActivityClass,"getApplicationInfo","()Landroid/content/pm/ApplicationInfo;");
        jobject Info=Env->CallObjectMethod(Activity,Method);jclass InfoClass=Env->GetObjectClass(Info);
        jfieldID FieldId=Env->GetFieldID(InfoClass,"nativeLibraryDir","Ljava/lang/String;");jstring Path=static_cast<jstring>(Env->GetObjectField(Info,FieldId));
        const char* S=Env->GetStringUTFChars(Path,nullptr);BackendPath=UTF8_TO_TCHAR(S);Env->ReleaseStringUTFChars(Path,S);
        Env->DeleteLocalRef(Path);Env->DeleteLocalRef(InfoClass);Env->DeleteLocalRef(Info);Env->DeleteLocalRef(ActivityClass);
    }
    if(BackendPath.IsEmpty())return false;
    // The DSP cannot read skeletons from the app's native-library directory (session open fails
    // with 0x80000406), so give it a plain private folder. Its path must avoid '~' and '='.
    HtpPath=Storage/TEXT("htp");IFileManager::Get().MakeDirectory(*HtpPath,true);
    TArray<FString> Skeletons;IFileManager::Get().FindFiles(Skeletons,*(BackendPath/TEXT("libggml-htp-v*.so")),true,false);
    for(const FString& Name:Skeletons)if(IFileManager::Get().FileSize(*(HtpPath/Name))!=IFileManager::Get().FileSize(*(BackendPath/Name)))
        IFileManager::Get().Copy(*(HtpPath/Name),*(BackendPath/Name),true,true);
    NativeHandle=FPlatformProcess::GetDllHandle(TEXT("libcrank-translate.so"));
#endif
    if(!NativeHandle)return false;
    NativeVerify=reinterpret_cast<FVerify>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_verify_model")));
    NativeTranslateStream=reinterpret_cast<FTranslateStream>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translate_stream")));
    NativeRelease=reinterpret_cast<FRelease>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_release")));
    NativeStats=reinterpret_cast<FStats>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_stats")));
    NativeConfigure=reinterpret_cast<FConfigure>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_configure")));
    NativeTrust=reinterpret_cast<FTrust>(FPlatformProcess::GetDllExport(NativeHandle,TEXT("crank_translation_trust_model")));
    if(NativeVerify && NativeTranslateStream && NativeConfigure) {
        FString Preference=TEXT("auto");
#if !UE_BUILD_SHIPPING
        FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationDevice="),Preference);
#endif
        NativeConfigure(TCHAR_TO_UTF8(*HtpPath),TCHAR_TO_UTF8(*Preference));
        if(bModelVerified && NativeTrust)NativeTrust(TCHAR_TO_UTF8(*ModelPath));
        return true;
    }
    FPlatformProcess::FreeDllHandle(NativeHandle);NativeHandle=nullptr;return false;
}
void FCrankTranslation::ReleaseModel() {WarmModelDeadline=0;if(NativeRelease)NativeRelease();}
void FCrankTranslation::DownloadModel() {
    if(bClosed || bModelReady || IsModelBusy() || ModelCheck.IsValid() || !LocalSupported())return;
#if PLATFORM_ANDROID
    if(!CrankModelDownload::HasWifi()){ModelState=TEXT("wifi");ModelStatus=TEXT("模型仅通过 Wi-Fi 下载，请连接可联网的 Wi-Fi 后重试。");return;}
    if(!LoadNative()){ModelState=TEXT("error");ModelStatus=TEXT("翻译组件无法加载，请重新安装应用。");return;}
    const FString Folder=FPaths::GetPath(ModelPath);IFileManager::Get().MakeDirectory(*Folder,true);
    uint64 Total=0,Free=0;
    if(FPlatformMisc::GetDiskTotalAndFreeSpace(Folder,Total,Free) && Free<uint64(LocalModelSize+128*1024*1024)){ModelState=TEXT("error");ModelStatus=TEXT("存储空间不足，请至少留出 1.2 GiB 后重试。");return;}
    const uint64 Version=++ModelRevision;ModelPartial=ModelPath+FString::Printf(TEXT(".part-%llu"),Version);
    ModelBytes=MakeShared<TAtomic<int64>,ESPMode::ThreadSafe>(0);
    ModelState=TEXT("downloading");ModelStatus.Empty();ModelControl=MakeShared<FControl,ESPMode::ThreadSafe>();
    FString Url=LocalModelUrl;
#if !UE_BUILD_SHIPPING
    FParse::Value(FCommandLine::Get(),TEXT("CrankTranslationModelUrl="),Url);
#endif
    AndroidModelJob=CrankModelDownload::Begin(Url,ModelPartial,LocalModelSize);ModelPollTime=0;
    if(AndroidModelJob<=0) {
        ModelState=AndroidModelJob==-1?TEXT("wifi"):TEXT("error");
        ModelStatus=AndroidModelJob==-1?TEXT("模型仅通过 Wi-Fi 下载，请连接可联网的 Wi-Fi 后重试。"):TEXT("无法开始下载，请重试。");AndroidModelJob=0;ModelPartial.Empty();
    }
#endif
}
void FCrankTranslation::VerifyDownloadedModel(const FString& Partial) {
    if(!ModelBytes || ModelBytes->Load()!=LocalModelSize || IFileManager::Get().FileSize(*Partial)!=LocalModelSize) {
        IFileManager::Get().Delete(*Partial,false,true);ModelPartial.Empty();ModelState=TEXT("error");ModelStatus=TEXT("模型文件不完整，请重新下载。");return;
    }
    ModelState=TEXT("verifying");ModelCheckPartial=Partial;const auto Verify=NativeVerify;const auto C=ModelControl;
    ModelCheck=Async(EAsyncExecution::Thread,[Partial,Verify,C] {return Verify && Verify(TCHAR_TO_UTF8(*Partial),+[](void* P)->int{return static_cast<FControl*>(P)->Cancelled.Load()?1:0;},C.Get())==0;});
}
void FCrankTranslation::CancelModelDownload() {
    ++ModelRevision;if(ModelControl)ModelControl->Cancelled=true;
#if PLATFORM_ANDROID
    if(AndroidModelJob){CrankModelDownload::Cancel(AndroidModelJob);CrankModelDownload::Release(AndroidModelJob);AndroidModelJob=0;}
#endif
    if(!ModelPartial.IsEmpty() && ModelPartial!=ModelCheckPartial){IFileManager::Get().Delete(*ModelPartial,false,true);ModelPartial.Empty();}
    if(ModelState==TEXT("downloading") || ModelState==TEXT("verifying")){ModelState=bModelReady?TEXT("ready"):TEXT("missing");ModelStatus=TEXT("已取消下载，可重新下载。");}
}
void FCrankTranslation::Tick() {
    if(WarmModelDeadline>0 && FPlatformTime::Seconds()>=WarmModelDeadline && !Task.IsValid())ReleaseModel();
    if(Task.IsValid() && Control && !Control->Cancelled.Load() && State==TEXT("translating") && ActiveProvider==TEXT("local")) {
        {FScopeLock Lock(&Control->StreamMutex);if(!Control->Partial.IsEmpty())Translated=Control->Partial;}
        if(Translated.IsEmpty() && FallsBackToOnline(Provider) && FPlatformTime::Seconds()-LocalStarted>LocalFirstOutputSeconds)FallBackToOnline(TEXT("first output timeout"));
    }
    if(Task.IsValid() && Task.IsReady()) {
        FResult R=Task.Get();Task=TFuture<FResult>();if(Control && Control->Cancelled.Load())Control.Reset();
        FinishLocal(MoveTemp(R));
        if(!Task.IsValid() && !bClosed && bTranslationRequested && State==TEXT("ready") && !Source.IsEmpty())Translate();
    }
#if PLATFORM_ANDROID
    if(AndroidModelJob && FPlatformTime::Seconds()>=ModelPollTime) {
        ModelPollTime=FPlatformTime::Seconds()+.1;
        const auto Download=CrankModelDownload::Poll(AndroidModelJob);
        if(ModelBytes)ModelBytes->Store(FMath::Clamp(Download.Bytes,int64(0),LocalModelSize));
        if(Download.State!=1) {
            CrankModelDownload::Release(AndroidModelJob);AndroidModelJob=0;
            if(Download.State==2)VerifyDownloadedModel(ModelPartial);
            else {
                ModelState=Download.State==3?TEXT("wifi"):TEXT("error");
                ModelStatus=Download.State==3?TEXT("Wi-Fi 已断开，下载已停止。连接 Wi-Fi 后可重试。"):TEXT("模型下载失败，请检查网络和空间后重试。");
                IFileManager::Get().Delete(*ModelPartial,false,true);ModelPartial.Empty();
            }
        }
    }
#endif
    if(ModelCheck.IsValid() && ModelCheck.IsReady()) {
        const bool Ok=ModelCheck.Get();ModelCheck=TFuture<bool>();const FString Partial=ModelCheckPartial;ModelCheckPartial.Empty();if(ModelPartial==Partial)ModelPartial.Empty();
        if(!bClosed && ModelState==TEXT("verifying") && Ok && IFileManager::Get().Move(*ModelPath,*Partial,true,true)) {
            bModelReady=bModelVerified=true;WriteVerifiedMarker();if(NativeTrust)NativeTrust(TCHAR_TO_UTF8(*ModelPath));
            ModelState=TEXT("ready");ModelStatus=TEXT("模型已就绪，可离线翻译。");
            if(bTranslationRequested && State==TEXT("ready") && !Source.IsEmpty() && Provider!=TEXT("microsoft") && Provider!=TEXT("tencent"))Translate();
        }else{IFileManager::Get().Delete(*Partial,false,true);if(ModelState==TEXT("verifying")){ModelState=TEXT("error");ModelStatus=TEXT("模型校验失败，请重新下载。");}}
    }
}
FString FCrankTranslation::Message() const {
    if(State==TEXT("translating") && ActiveProvider==TEXT("local") && Control && Control->Total.Load()>0)
        return FString::Printf(TEXT("正在本机翻译 · 第 %d / %d 段"),Control->Part.Load(),Control->Total.Load());
    return Status;
}
FString FCrankTranslation::ModelMessage() const {
    if(!LocalSupported())return TEXT("此设备仅支持在线翻译。");
    if(ModelState==TEXT("downloading")) {
        const int64 Bytes=ModelBytes?FMath::Clamp(ModelBytes->Load(),int64(0),LocalModelSize):0;
        if(!Bytes)return TEXT("正在连接模型下载…");
        return FString::Printf(TEXT("下载模型 · %d%% · %.1f / %.1f MiB"),FMath::FloorToInt(100.0*Bytes/LocalModelSize),Bytes/1048576.0,LocalModelSize/1048576.0);
    }
    if(ModelState==TEXT("verifying"))return TEXT("正在校验模型文件…");
    if(!ModelStatus.IsEmpty())return ModelStatus;
    return bModelReady?TEXT("本地模型已就绪 · 支持离线翻译")
        :TEXT("本地模型约 1.0 GiB；仅通过 Wi-Fi 下载，之后可离线翻译。骁龙设备使用 NPU 加速。");
}
FString FCrankTranslation::ModelDownloadLabel() const {return TEXT("下载1G");}
TOptional<float> FCrankTranslation::ModelProgress() const {
    return ModelState==TEXT("downloading") && ModelBytes?TOptional<float>(float(FMath::Clamp(double(ModelBytes->Load())/LocalModelSize,0.0,1.0))):TOptional<float>();
}
void FCrankTranslation::Shutdown() {
    if(bClosed)return;bClosed=true;Cancel();CancelModelDownload();
    if(Task.IsValid())Task.Wait();if(ModelCheck.IsValid())ModelCheck.Wait();
    ReleaseModel();
    for(const FString& Leftover:{ModelPartial,ModelCheckPartial})if(!Leftover.IsEmpty())IFileManager::Get().Delete(*Leftover,false,true);
    if(NativeHandle){FPlatformProcess::FreeDllHandle(NativeHandle);NativeHandle=nullptr;}
}
