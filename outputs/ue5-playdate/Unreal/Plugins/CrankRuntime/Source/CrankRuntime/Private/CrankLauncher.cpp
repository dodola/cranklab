#include "CrankLauncher.h"
#include "CrankGameFiles.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winhttp.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace {
constexpr int64 MaxPackage=512ll*1024*1024;
FString Match(const FString& Text,const FString& Pattern,int32 Group=1) {
    FRegexMatcher Matcher(FRegexPattern(Pattern),Text);return Matcher.FindNext() ? Matcher.GetCaptureGroup(Group) : FString();
}
TSharedPtr<FJsonObject> Json(const FString& Text) {
    TSharedPtr<FJsonObject> Object;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Object);return Object;
}
FString Field(const TSharedPtr<FJsonObject>& Object,const TCHAR* Name) {FString Value;if(Object) Object->TryGetStringField(Name,Value);return Value;}
bool WebUrl(const FString& Url) {return Url.StartsWith(TEXT("https://")) || Url.StartsWith(TEXT("http://"));}
bool ItchUrl(const FString& Url) {
    return !Match(Url,TEXT("^(https://[a-zA-Z0-9-]+\\.itch\\.io/[a-zA-Z0-9_-]+/?)$")).IsEmpty();
}
// HTTP invokes Serialize on its receive thread. Only atomics cross back to UI.
class FLimitedDownload : public FArchive {
    TUniquePtr<FArchive> File;
    TSharedRef<TAtomic<bool>,ESPMode::ThreadSafe> Cancelled;
    TSharedRef<TAtomic<int64>,ESPMode::ThreadSafe> Bytes;
    bool bClosed=false,bCloseResult=false;
    FCriticalSection Mutex;
public:
    FLimitedDownload(const FString& Path,TSharedRef<TAtomic<bool>,ESPMode::ThreadSafe> Flag,TSharedRef<TAtomic<int64>,ESPMode::ThreadSafe> Count)
      :File(IFileManager::Get().CreateFileWriter(*Path)),Cancelled(Flag),Bytes(Count) {if(!File) SetError();}
    virtual void Serialize(void* Data,int64 Count) override {
        FScopeLock Guard(&Mutex);
        if(bClosed || IsError() || Cancelled->Load() || Count<0 || Bytes->Load()+Count>MaxPackage) {SetError();return;}
        File->Serialize(Data,Count);if(File->IsError()) SetError();else *Bytes+=Count;
    }
    virtual bool Close() override {FScopeLock Guard(&Mutex);if(!bClosed) {bClosed=true;bCloseResult=File && File->Close() && !IsError();File.Reset();}return bCloseResult;}
};
}
FCrankLauncher::FCrankLauncher() {
#if PLATFORM_WINDOWS
    // The Lua network backend already follows Windows proxy settings. Mirror
    // an explicitly configured system proxy for the UE curl download client.
    auto& Http=FHttpModule::Get();
    if(Http.GetProxyAddress().IsEmpty()) {
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG Proxy{};
        if(WinHttpGetIEProxyConfigForCurrentUser(&Proxy)) {
            if(Proxy.lpszProxy) {
                FString Address(Proxy.lpszProxy);
                if(Address.Contains(TEXT("="))) {
                    FString Selected;TArray<FString> Parts;Address.ParseIntoArray(Parts,TEXT(";"));
                    for(const auto& Part:Parts) if(Part.StartsWith(TEXT("https="))) Selected=Part.Mid(6);
                    if(Selected.IsEmpty()) for(const auto& Part:Parts) if(Part.StartsWith(TEXT("http="))) Selected=Part.Mid(5);
                    Address=Selected;
                }
                if(!Address.IsEmpty()) Http.SetProxyAddress(Address);
            }
            if(Proxy.lpszProxy) GlobalFree(Proxy.lpszProxy);
            if(Proxy.lpszProxyBypass) GlobalFree(Proxy.lpszProxyBypass);
            if(Proxy.lpszAutoConfigUrl) GlobalFree(Proxy.lpszAutoConfigUrl);
        }
    }
#endif
    Storage=FCrankGameFiles::Root();
    // Recover only our transfer scratch files after process death. Active
    // Windows handles deny deletion; Android owns a single app process.
    TArray<FString> PartialFiles;const FString DownloadFolder=Storage/TEXT("Downloads");
    IFileManager::Get().FindFiles(PartialFiles,*(DownloadFolder/TEXT("launcher-*.zip")),true,false);
    for(const auto& Name:PartialFiles) IFileManager::Get().Delete(*(DownloadFolder/Name),false,true);
    FString Text;
    if(!FParse::Param(FCommandLine::Get(),TEXT("CrankLauncherLiveOnly")) && FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Games/launcher-catalog.json")))) if(auto Root=Json(Text)) {
        const TSharedPtr<FJsonObject>* Games=nullptr;
        if(Root->TryGetObjectField(TEXT("games"),Games)) for(const auto& Pair:(*Games)->Values) {
            auto Game=Pair.Value->AsObject();if(!Game) continue;FPlan P;
            P.Source=Field(Game,TEXT("source"));P.Kind=Field(Game,TEXT("kind"));P.Upload=Field(Game,TEXT("upload"));Game->TryGetBoolField(TEXT("demo"),P.Demo);
            Catalog.Add(FCString::Atoi(*Pair.Key),P);
        }
    }
    if(FFileHelper::LoadFileToString(Text,*(Storage/TEXT("launcher-library.json")))) if(auto Root=Json(Text)) {
        for(const auto& Pair:Root->Values) {
            const auto Record=Pair.Value->Type==EJson::Object ? Pair.Value->AsObject() : nullptr;
            const FString Name=Record ? Field(Record,TEXT("directory")) : Pair.Value->AsString();const FString Directory=Storage/TEXT("Games")/Name;
            if(!Name.Contains(TEXT("/")) && !Name.Contains(TEXT("\\")) && !Name.StartsWith(TEXT(".")) && FPaths::FileExists(Directory/TEXT("pdxinfo")))
                Installed.Add(FCString::Atoi(*Pair.Key),Directory);
            bool Demo=false;if(Record && Record->TryGetBoolField(TEXT("demo"),Demo) && Demo) InstalledDemos.Add(FCString::Atoi(*Pair.Key));
        }
    }
}
FCrankLauncher::~FCrankLauncher() {Shutdown();}
void FCrankLauncher::SetState(const FString& NewState,const FString& NewLabel,const FString& Message) {State=NewState;Label=NewLabel;StatusMessage=Message;}
void FCrankLauncher::CleanupTransfer() {
    DownloadStream.Reset();
    if(!ArchivePath.IsEmpty()) {IFileManager::Get().Delete(*ArchivePath,false,true);ArchivePath.Empty();}
}
void FCrankLauncher::Fail(const FString& Message) {
    bBusy=false;Request.Reset();CleanupTransfer();SetState(TEXT("error"),TEXT("Retry download"),Message);
    if(DeferredSelection) {int32 Id=DeferredSelection;DeferredSelection=0;Select(Id);}
}
void FCrankLauncher::Select(int32 Id) {
    if(bClosed || Id<=0) return;
    if(bBusy) {DeferredSelection=Id;return;} // Keep transfer ownership, restore the new selection after completion.
    Revision++;if(Request) {Request->CancelRequest();Request.Reset();}
    SelectedId=Id;Publisher.Empty();Plan=Catalog.FindRef(Id);
    if(const auto* Directory=Installed.Find(Id)) if(FPaths::FileExists(*Directory/TEXT("pdxinfo"))) {
        Plan.Demo=InstalledDemos.Contains(Id);SetState(TEXT("installed"),Plan.Demo ? TEXT("Run demo") : TEXT("Run game"),Plan.Demo ? TEXT("已安装试玩版，可离线运行。") : TEXT("已安装，可离线运行。"));return;
    }
    if(!Plan.Source.IsEmpty()) {
        Publisher=Plan.Source;
        if(Plan.Kind==TEXT("itch") || Plan.Kind==TEXT("direct")) SetState(TEXT("ready"),Plan.Demo ? TEXT("Download demo") : TEXT("Download & Play"),Plan.Demo ? TEXT("此入口下载试玩版。"):TEXT("下载并安装后运行。"));
        else SetState(TEXT("unavailable"),TEXT("Publisher page"),TEXT("当前没有可直接安装的公开 ZIP，可打开发布者页面或从游戏库导入已取得的游戏包。"));
    } else {SetState(TEXT("checking"),TEXT("Checking..."),TEXT("正在查询游戏来源…"));GetDetail(Revision);}
}
void FCrankLauncher::GetDetail(uint64 Version) {
    Fetch(FString::Printf(TEXT("https://pdgamez.com/game/%d.json"),SelectedId),TEXT("GET"),{},Version,[Weak=AsWeak(),Version](const FString& Text) {
        auto Self=Weak.Pin();if(!Self) return;auto Detail=Json(Text);const TSharedPtr<FJsonObject>* Urls=nullptr;
        if(!Detail || !Detail->TryGetObjectField(TEXT("urls"),Urls)) {Self->Fail(TEXT("无法读取游戏来源，请重试。"));return;}
        Self->Publisher=Field(*Urls,TEXT("itch"));
        if(!ItchUrl(Self->Publisher)) {Self->Publisher=Field(*Urls,TEXT("catalog"));Self->SetState(TEXT("unavailable"),TEXT("Publisher page"),TEXT("此游戏需要从发布者取得游戏包，再从游戏库导入。"));return;}
        Self->Plan.Source=Self->Publisher;Self->Plan.Kind=TEXT("itch");
        Self->SetState(TEXT("ready"),TEXT("Download & Play"),TEXT("将查询发布者当前提供的游戏包。"));
    });
}
void FCrankLauncher::Fetch(const FString& Url,const FString& Verb,const FString& Body,uint64 Version,TFunction<void(const FString&)> Complete) {
    if(!WebUrl(Url)) {Fail(TEXT("游戏来源地址无效。"));return;}
    auto Http=FHttpModule::Get().CreateRequest();Request=Http;Http->SetURL(Url);Http->SetVerb(Verb);Http->SetTimeout(30);
    Http->SetHeader(TEXT("User-Agent"),TEXT("CrankLab/1.0"));
    if(!DownloadPage.IsEmpty()) Http->SetHeader(TEXT("Referer"),DownloadPage);
    if(Verb==TEXT("POST")) {Http->SetHeader(TEXT("Content-Type"),TEXT("application/x-www-form-urlencoded"));Http->SetContentAsString(Body);}
    Http->OnRequestProgress64().BindLambda([](FHttpRequestPtr R,uint64 Sent,uint64 Read){if(Read>4*1024*1024) R->CancelRequest();});
    Http->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version,Complete=MoveTemp(Complete)](FHttpRequestPtr R,FHttpResponsePtr Response,bool Ok) {
        auto Self=Weak.Pin();if(!Self || Self->bClosed || Version!=Self->Revision) return;
        Self->Request.Reset();
        if(!Ok || !Response || !EHttpResponseCodes::IsOk(Response->GetResponseCode()) || Response->GetContentLength()>4*1024*1024) {Self->Fail(TEXT("网络请求失败或超时，请检查网络后重试。"));return;}
        Complete(Response->GetContentAsString());
    });
    if(!Http->ProcessRequest()) Fail(TEXT("无法开始网络请求，请重试。"));
}
void FCrankLauncher::Play(int32 Id) {
    if(bClosed || bBusy || Id!=SelectedId || State==TEXT("checking")) return;
    if(const auto* Directory=Installed.Find(Id)) if(FPaths::FileExists(*Directory/TEXT("pdxinfo"))) {JobId=Id;LaunchDirectory=*Directory;return;}
    if(State==TEXT("unavailable")) {OpenPublisher();return;}
    Revision++;JobId=Id;bBusy=true;CancelFlag=MakeShared<TAtomic<bool>,ESPMode::ThreadSafe>(false);
    Received.Reset();Csrf.Empty();DownloadPage.Empty();
    SetState(TEXT("busy"),TEXT("Preparing..."),TEXT("正在查询当前下载地址…"));
    if(Plan.Kind==TEXT("direct")) Download(Plan.Source,Revision);else GetPublisher(Revision);
}
void FCrankLauncher::GetPublisher(uint64 Version) {
    if(!ItchUrl(Plan.Source)) {Fail(TEXT("未找到可下载的发布者页面，可从游戏库手动导入。"));return;}
    DownloadPage=Plan.Source;DownloadPage.RemoveFromEnd(TEXT("/"));
    Fetch(DownloadPage,TEXT("GET"),{},Version,[Weak=AsWeak(),Version](const FString& Html){if(auto Self=Weak.Pin()) Self->ResolvePage(Html,Version,false);});
}
void FCrankLauncher::ResolvePage(const FString& Html,uint64 Version,bool DownloadPageReady) {
    Csrf=Match(Html,TEXT("<meta[^>]*name=[\"']csrf_token[\"'][^>]*(?:content|value)=[\"']([^\"']+)[\"']"));
    if(Csrf.IsEmpty()) Csrf=Match(Html,TEXT("<meta[^>]*(?:content|value)=[\"']([^\"']+)[\"'][^>]*name=[\"']csrf_token[\"']"));
    if(Csrf.IsEmpty()) {Fail(TEXT("发布者页面无法解析，请重试或手动导入游戏包。"));return;}
    if(!DownloadPageReady) {
        const FString ConfigText=Match(Html,TEXT("init_View(?:Html)?Game\\([^,]+,\\s*(\\{.*?\\})\\);"));
        auto Config=Json(ConfigText);const TSharedPtr<FJsonObject>* Game=nullptr;double Price=-1;
        if(Config && Config->TryGetObjectField(TEXT("game"),Game)) if(!(*Game)->TryGetNumberField(TEXT("actual_price"),Price)) (*Game)->TryGetNumberField(TEXT("min_price"),Price);
        const FString Generate=Field(Config,TEXT("generate_download_url"));
        if(Price==0 && WebUrl(Generate)) {
            Fetch(Generate,TEXT("POST"),TEXT("csrf_token=")+FGenericPlatformHttp::UrlEncode(Csrf),Version,[Weak=AsWeak(),Version](const FString& Text) {
                auto Self=Weak.Pin();if(!Self) return;const FString Url=Field(Json(Text),TEXT("url"));
                if(!WebUrl(Url)) {Self->Fail(TEXT("发布者没有返回公开下载入口，请重试或手动导入。"));return;}
                Self->DownloadPage=Url;
                Self->Fetch(Url,TEXT("GET"),{},Version,[Weak,Version](const FString& Page){if(auto S=Weak.Pin()) S->ResolvePage(Page,Version,true);});
            });return;
        }
        // Paid pages expose demo buttons directly. Never label a demo as full.
        if(Price>0) Plan.Demo=true;
    }
    ResolveUpload(Html,Version);
}
void FCrankLauncher::ResolveUpload(const FString& Html,uint64 Version) {
    FString Upload;
    if(!Plan.Upload.IsEmpty() && Html.Contains(TEXT("data-upload_id=\"")+Plan.Upload+TEXT("\""))) Upload=Plan.Upload;
    if(Upload.IsEmpty()) {
        // Inspect only publisher-exposed download buttons, not account APIs.
        FRegexMatcher Buttons(FRegexPattern(TEXT("<a[^>]*>")),Html);
        while(Buttons.FindNext()) {
            const FString Tag=Buttons.GetCaptureGroup(0);if(!Tag.Contains(TEXT("download_btn"))) continue;
            const FString Id=Match(Tag,TEXT("data-upload_id=[\"']([0-9]+)[\"']"));if(Id.IsEmpty()) continue;
            FString Region=Html.Mid(Buttons.GetMatchEnding(),1500);
            const int32 Next=Region.Find(TEXT("<div class=\"upload\">"));if(Next>=0) Region.LeftInline(Next);
            const FString Name=Match(Region,TEXT("class=[\"']name[\"'][^>]*title=[\"']([^\"']+)[\"']"));
            if(Name.Contains(TEXT(".pdx"),ESearchCase::IgnoreCase) || Name.Contains(TEXT("Playdate"),ESearchCase::IgnoreCase)) {Upload=Id;Plan.Demo=Plan.Demo || Name.Contains(TEXT("demo"),ESearchCase::IgnoreCase);break;}
        }
    }
    if(Upload.IsEmpty()) {bBusy=false;SetState(TEXT("unavailable"),TEXT("Publisher page"),TEXT("发布者当前没有提供可识别的公开 PDX ZIP；可打开页面取得游戏包后导入。"));return;}
    const FString Endpoint=Plan.Source/TEXT("file")/Upload+TEXT("?source=")+(Plan.Demo ? TEXT("view_game") : TEXT("game_download"))+TEXT("&as_props=1");
    Fetch(Endpoint,TEXT("POST"),TEXT("csrf_token=")+FGenericPlatformHttp::UrlEncode(Csrf),Version,[Weak=AsWeak(),Version](const FString& Text) {
        auto Self=Weak.Pin();if(!Self) return;auto Object=Json(Text);const FString Url=Field(Object,TEXT("url"));
        if(!WebUrl(Url)) {Self->bBusy=false;Self->SetState(TEXT("unavailable"),TEXT("Publisher page"),TEXT("下载需要在发布者页面完成操作，请打开页面后导入取得的游戏包。"));return;}
        Self->Download(Url,Version);
    });
}
void FCrankLauncher::Download(const FString& Url,uint64 Version) {
    if(!WebUrl(Url)) {Fail(TEXT("下载地址无效。"));return;}
    const FString Folder=Storage/TEXT("Downloads");IFileManager::Get().MakeDirectory(*Folder,true);
    ArchivePath=Folder/(TEXT("launcher-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".zip"));
    Received=MakeShared<TAtomic<int64>,ESPMode::ThreadSafe>(0);
    DownloadStream=MakeShared<FLimitedDownload>(ArchivePath,CancelFlag.ToSharedRef(),Received.ToSharedRef());
    if(DownloadStream->IsError()) {Fail(TEXT("无法写入下载文件，请检查存储空间。"));return;}
    auto Http=FHttpModule::Get().CreateRequest();Request=Http;Http->SetURL(Url);Http->SetVerb(TEXT("GET"));Http->SetTimeout(600);
    Http->SetActivityTimeout(30);
    Http->SetHeader(TEXT("User-Agent"),TEXT("CrankLab/1.0"));Http->SetHeader(TEXT("Referer"),Plan.Source);
    Http->SetResponseBodyReceiveStream(DownloadStream.ToSharedRef());
    Http->OnHeaderReceived().BindLambda([](FHttpRequestPtr R,const FString& Name,const FString& Value){if(Name.Equals(TEXT("Content-Length"),ESearchCase::IgnoreCase) && FCString::Atoi64(*Value)>MaxPackage) R->CancelRequest();});
    Http->OnProcessRequestComplete().BindLambda([Weak=AsWeak(),Version,Stream=DownloadStream,Path=ArchivePath](FHttpRequestPtr R,FHttpResponsePtr Response,bool Ok) {
        const bool Written=Stream->Close();
        auto Self=Weak.Pin();if(!Self || Self->bClosed || Version!=Self->Revision) {IFileManager::Get().Delete(*Path,false,true);return;}
        Self->Request.Reset();
        Self->DownloadStream.Reset();
        if(!Ok || !Written || !Response || !EHttpResponseCodes::IsOk(Response->GetResponseCode()) || !Self->Received->Load()) {Self->Fail(TEXT("下载失败、超过 512 MB 或存储空间不足，请重试。"));return;}
        Self->SetState(TEXT("busy"),TEXT("Installing..."),TEXT("下载完成，正在校验并安装…"));Self->bInstalling=true;
        const FString Archive=Self->ArchivePath,Library=Self->Storage/TEXT("Games");auto Flag=Self->CancelFlag;
        Self->InstallTask=Async(EAsyncExecution::Thread,[Archive,Library,Flag](){FInstallResult Result;Result.Directory=FCrankGameFiles::InstallArchive(Archive,Library,Result.Error,Flag.Get());return Result;});
    });
    if(!Http->ProcessRequest()) Fail(TEXT("无法开始下载，请重试。"));
}
void FCrankLauncher::Tick() {
    if(bClosed) return;
    if(bBusy && !bInstalling && Received) StatusMessage=(Plan.Demo ? TEXT("正在下载试玩版… ") : TEXT("正在下载… "))+FString::Printf(TEXT("%lld KB"),Received->Load()/1024);
    if(bInstalling && InstallTask.IsReady()) {
        auto Result=InstallTask.Get();bInstalling=false;CleanupTransfer();
        if(CancelFlag->Load()) {bBusy=false;SetState(TEXT("ready"),TEXT("Download & Play"),TEXT("已取消下载，可重新开始。"));if(DeferredSelection) {int32 Id=DeferredSelection;DeferredSelection=0;Select(Id);}return;}
        if(Result.Directory.IsEmpty()) {Fail(Result.Error);return;}
        Installed.Add(JobId,Result.Directory);
        if(Plan.Demo) InstalledDemos.Add(JobId);else InstalledDemos.Remove(JobId);
        if(!SaveIndex()) {bBusy=false;SetState(TEXT("error"),TEXT("Run game"),TEXT("游戏已安装，但启动器索引无法保存；可在游戏库中打开，或重试。"));return;}
        bBusy=false;SetState(TEXT("installed"),Plan.Demo ? TEXT("Run demo") : TEXT("Run game"),Plan.Demo ? TEXT("试玩版已安装，正在启动…") : TEXT("游戏已安装，正在启动…"));LaunchDirectory=Result.Directory;
        if(DeferredSelection) {int32 Id=DeferredSelection;DeferredSelection=0;Select(Id);}
    }
}
bool FCrankLauncher::SaveIndex() {
    auto Object=MakeShared<FJsonObject>();for(const auto& Pair:Installed) {
        auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("directory"),FPaths::GetCleanFilename(Pair.Value));Record->SetBoolField(TEXT("demo"),InstalledDemos.Contains(Pair.Key));
        Object->SetObjectField(FString::FromInt(Pair.Key),Record);
    }
    FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));
    const FString Path=Storage/TEXT("launcher-library.json"),Temporary=Path+TEXT(".tmp");
    return FFileHelper::SaveStringToFile(Text,*Temporary) && IFileManager::Get().Move(*Path,*Temporary,true,true);
}
void FCrankLauncher::Cancel() {
    if(!bBusy) return;
    if(CancelFlag) CancelFlag->Store(true);
    if(bInstalling) {StatusMessage=TEXT("正在取消安装…");return;}
    Revision++;if(Request) {Request->CancelRequest();Request.Reset();}
    // The archive serializes Close against receive-thread writes. A stale HTTP
    // callback retains a closed stream and cannot recreate the deleted file.
    if(DownloadStream) DownloadStream->Close();CleanupTransfer();
    bBusy=false;Received.Reset();SetState(TEXT("ready"),Plan.Demo ? TEXT("Download demo") : TEXT("Download & Play"),TEXT("已取消下载，可重新开始。"));
    if(DeferredSelection) {int32 Id=DeferredSelection;DeferredSelection=0;Select(Id);}
}
void FCrankLauncher::OpenPublisher() {if(WebUrl(Publisher)) FPlatformProcess::LaunchURL(*Publisher,nullptr,nullptr);}
FString FCrankLauncher::Snapshot() const {
    auto Object=MakeShared<FJsonObject>();Object->SetNumberField(TEXT("id"),DeferredSelection>0 ? DeferredSelection : SelectedId);Object->SetStringField(TEXT("state"),State);
    Object->SetStringField(TEXT("label"),Label);Object->SetBoolField(TEXT("busy"),bBusy);Object->SetBoolField(TEXT("demo"),Plan.Demo);
    Object->SetNumberField(TEXT("receivedBytes"),Received ? Received->Load() : 0);
    FString Text;FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));return Text;
}
FString FCrankLauncher::TakeLaunch() {FString Result=MoveTemp(LaunchDirectory);LaunchDirectory.Empty();return Result;}
void FCrankLauncher::Shutdown() {
    if(bClosed) return;bClosed=true;Revision++;if(CancelFlag) CancelFlag->Store(true);
    if(Request) {Request->CancelRequest();Request.Reset();}
    if(bInstalling) {InstallTask.Wait();InstallTask.Get();bInstalling=false;}
    if(DownloadStream) DownloadStream->Close();CleanupTransfer();
}
