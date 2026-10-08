#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Interfaces/IHttpRequest.h"

// Host-owned translation of public descriptions. The scoped PDGames adapter
// receives completed text through launcher snapshots.
class CRANKRUNTIME_API FCrankTranslation : public TSharedFromThis<FCrankTranslation> {
public:
    FCrankTranslation();
    ~FCrankTranslation();
    void Open(int32 CatalogId);
    void Translate();
    void Cancel();
    void ReleaseModel();
    void DownloadModel();
    void CancelModelDownload();
    void Tick();
    void Shutdown();
    bool IsLightModel() const {return bLightModel;}
    bool SelectModel(bool Light);
    FString ModelDownloadLabel() const;
    bool HasModel() const {return bModelReady;}
    bool IsModelBusy() const {return ModelState==TEXT("downloading") || ModelCheck.IsValid();}
    bool IsBusy() const {return DetailRequest.IsValid() || State==TEXT("loading") || Task.IsValid();}
    bool HasModelError() const {return ModelState==TEXT("error");}
    bool HasError() const {return State==TEXT("error");}
    FString GetState() const {return State;}
    FString GetModelState() const {return ModelState;}
    FString GetTitle() const {return Title;}
    FString GetSource() const {return Source;}
    FString GetTranslated() const {return Translated;}
    FString Message() const;
    FString ModelMessage() const;
    TOptional<float> ModelProgress() const;
    int32 GetId() const {return Id;}
    // Stable normalization/cache key shared with verification.
    static FString Normalize(const FString& Text);
    static FString CacheKey(int32 Id,const FString& Text,bool Light=true);
private:
    struct FResult {FString Text,Error,Metrics;double VerifyMs=0;bool ModelVerified=false;uint64 Version=0;};
    struct FControl {TAtomic<bool> Cancelled{false};TAtomic<int32> Part{0},Total{0};FCriticalSection StreamMutex;FString Partial;};
    FString Storage,ModelPath,ModelPartial,BackendPath,Title,Source,Translated,Status;
    FString ModelFile,ModelUrl;
    int64 ModelSize=0;
    bool bLightModel=true;
    FString State=TEXT("idle"),ModelState=TEXT("missing"),ModelStatus;
    int32 Id=0;
    uint64 Revision=0,ModelRevision=0;
    int64 AndroidModelJob=0;
    double ModelPollTime=0;
    double WarmModelDeadline=0;
    bool bClosed=false,bModelReady=false,bModelVerified=false,bTranslationRequested=false;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> DetailRequest,ModelRequest;
    TSharedPtr<FArchive> ModelStream;
    TSharedPtr<TAtomic<int64>,ESPMode::ThreadSafe> ModelBytes;
    TSharedPtr<FControl,ESPMode::ThreadSafe> Control,ModelControl;
    TFuture<FResult> Task;
    TFuture<bool> ModelCheck;
    FString TaskCache;
    void* NativeHandle=nullptr;
    using FCancel=int(*)(void*);
    using FProgress=void(*)(void*,int,int);
    using FTranslate=int(*)(const char*,const char*,const char*,char*,int,char*,int,FCancel,FProgress,void*);
    using FVerify=int(*)(const char*,FCancel,void*);
    using FStream=void(*)(void*,const char*);
    using FTranslateStream=int(*)(const char*,const char*,const char*,char*,int,char*,int,FCancel,FProgress,FStream,void*);
    using FRelease=void(*)();
    using FStats=int(*)(char*,int);
    FTranslate NativeTranslate=nullptr;
    FTranslateStream NativeTranslateStream=nullptr;
    FRelease NativeRelease=nullptr;
    FStats NativeStats=nullptr;
    FVerify NativeVerify=nullptr;
    bool LoadNative();
    void ConfigureModel();
    void VerifyDownloadedModel(const FString& Partial);
    void AcceptDescription(const FString& Text);
    bool ReadTranslationCache();
    bool SaveJson(const FString& Path,const FString& Text);
};
