#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Interfaces/IHttpRequest.h"

// One host-owned download job. The service never calls Lua or switches games;
// the pawn consumes a completed launch on the UE game thread.
class CRANKRUNTIME_API FCrankLauncher : public TSharedFromThis<FCrankLauncher> {
public:
    FCrankLauncher();
    ~FCrankLauncher();
    void Select(int32 Id);
    void Play(int32 Id);
    void Cancel();
    void OpenPublisher();
    void Tick();
    FString Snapshot() const;
    bool IsBusy() const {return bBusy;}
    bool HasError() const {return State==TEXT("error");}
    FString Message() const {return StatusMessage;}
    FString TakeLaunch();
    int32 GetSelectedId() const {return SelectedId;}
    int32 GetLaunchId() const {return JobId;}
    void Shutdown();
private:
    struct FPlan {FString Source,Kind,Upload;bool Demo=false;};
    struct FInstallResult {FString Directory,Error;};
    TMap<int32,FPlan> Catalog;
    TMap<int32,FString> Installed;
    TSet<int32> InstalledDemos;
    int32 SelectedId=0,JobId=0,DeferredSelection=0;
    uint64 Revision=0;
    bool bBusy=false,bInstalling=false,bClosed=false;
    FString State=TEXT("checking"),Label=TEXT("Checking..."),StatusMessage;
    FString Storage,Publisher,DownloadPage,Csrf,ArchivePath,LaunchDirectory;
    FPlan Plan;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> Request;
    TSharedPtr<FArchive> DownloadStream;
    TSharedPtr<TAtomic<bool>,ESPMode::ThreadSafe> CancelFlag;
    TSharedPtr<TAtomic<int64>,ESPMode::ThreadSafe> Received;
    TFuture<FInstallResult> InstallTask;
    void SetState(const FString& NewState,const FString& NewLabel,const FString& Message);
    void Fail(const FString& Message);
    void CleanupTransfer();
    void GetDetail(uint64 Version);
    void GetPublisher(uint64 Version);
    void ResolvePage(const FString& Html,uint64 Version,bool DownloadPageReady);
    void ResolveUpload(const FString& Html,uint64 Version);
    void Download(const FString& Url,uint64 Version);
    void Fetch(const FString& Url,const FString& Verb,const FString& Body,uint64 Version,TFunction<void(const FString&)> Complete);
    bool SaveIndex();
};
