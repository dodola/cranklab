#pragma once
#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Interfaces/IHttpRequest.h"

// Translation of public game descriptions. On Android the Hy-MT2 Q4_0 model
// runs locally (Hexagon HTP, Adreno OpenCL or CPU) and the Microsoft/Tencent
// web endpoints are the fallback. Completed translations are cached privately;
// the PDGames adapter continues to own Chinese/original UI.
class CRANKRUNTIME_API FCrankTranslation : public TSharedFromThis<FCrankTranslation> {
public:
    FCrankTranslation();
    ~FCrankTranslation();
    void Open(int32 CatalogId);
    void Translate();
    void Cancel();
    // Stops translation (local run and online request) but leaves the description download alone.
    void CancelTranslationWork();
    void Tick();
    void Shutdown();
    bool SelectProvider(const FString& Provider);
    FString GetProvider() const {return Provider;}
    FString ProviderMessage() const;
    // Local model management (Android only; other platforms stay online).
    static bool LocalSupported();
    void DownloadModel();
    void CancelModelDownload();
    void ReleaseModel();
    bool HasModel() const {return bModelReady;}
    bool IsModelBusy() const {return ModelState==TEXT("downloading") || ModelState==TEXT("verifying");}
    bool HasModelError() const {return ModelState==TEXT("error");}
    FString GetModelState() const {return ModelState;}
    FString ModelMessage() const;
    FString ModelDownloadLabel() const;
    TOptional<float> ModelProgress() const;
    bool IsBusy() const {return DetailRequest.IsValid() || TranslationRequest.IsValid() || Task.IsValid();}
    bool HasError() const {return State==TEXT("error");}
    FString GetState() const {return State;}
    FString GetTitle() const {return Title;}
    FString GetSource() const {return Source;}
    FString GetTranslated() const {return Translated;}
    FString Message() const;
    // Provider that produced the current translation ("local", "microsoft", "tencent") or empty.
    FString GetActiveProvider() const {return ActiveProvider;}
    int32 GetId() const {return Id;}
    static FString Normalize(const FString& Text);
    static FString CacheKey(int32 Id,const FString& Text,const FString& Provider=TEXT("microsoft"));
    // Pure decision used by Translate(): which provider answers first and where a failure falls back.
    static FString FirstProvider(const FString& Selected,bool bLocalReady);
    static bool FallsBackToOnline(const FString& Selected);
private:
    struct FPart {FString Text,JoinAfter;TArray<TPair<FString,FString>> Keep;};
    struct FResult {FString Text,Error,Metrics;bool ModelVerified=false;uint64 Version=0;};
    struct FControl {TAtomic<bool> Cancelled{false};TAtomic<int32> Part{0},Total{0};FCriticalSection StreamMutex;FString Partial;};
    FString Storage,Title,Source,Translated,Status;
    FString Provider=TEXT("auto"),ActiveProvider;
    FString State=TEXT("idle");
    TArray<FPart> Parts;
    int32 Id=0,Attempt=0;
    uint64 Revision=0,TranslationRevision=0;
    bool bClosed=false,bTranslationRequested=false;
    double RequestStarted=0;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> DetailRequest,TranslationRequest;
    // Local model.
    FString ModelPath,ModelPartial,ModelCheckPartial,BackendPath,HtpPath,ModelState=TEXT("missing"),ModelStatus;
    uint64 ModelRevision=0;
    int64 AndroidModelJob=0;
    double ModelPollTime=0,WarmModelDeadline=0,LocalStarted=0;
    bool bModelReady=false,bModelVerified=false;
    TSharedPtr<TAtomic<int64>,ESPMode::ThreadSafe> ModelBytes;
    TSharedPtr<FControl,ESPMode::ThreadSafe> Control,ModelControl;
    TFuture<FResult> Task;
    TFuture<bool> ModelCheck;
    FString TaskCache;
    void* NativeHandle=nullptr;
    using FCancel=int(*)(void*);
    using FProgress=void(*)(void*,int,int);
    using FVerify=int(*)(const char*,FCancel,void*);
    using FStream=void(*)(void*,const char*);
    using FTranslateStream=int(*)(const char*,const char*,const char*,char*,int,char*,int,FCancel,FProgress,FStream,void*);
    using FRelease=void(*)();
    using FStats=int(*)(char*,int);
    using FConfigure=void(*)(const char*,const char*);
    using FTrust=int(*)(const char*);
    FTranslateStream NativeTranslateStream=nullptr;
    FRelease NativeRelease=nullptr;
    FStats NativeStats=nullptr;
    FVerify NativeVerify=nullptr;
    FConfigure NativeConfigure=nullptr;
    FTrust NativeTrust=nullptr;
    bool LoadNative();
    bool LocalReady() const {return LocalSupported() && bModelReady && ModelState==TEXT("ready");}
    void ConfigureModel();
    void VerifyDownloadedModel(const FString& Partial);
    void WriteVerifiedMarker() const;
    bool ReadVerifiedMarker() const;
    void AcceptDescription(const FString& Text);
    bool ReadTranslationCache();
    bool SaveJson(const FString& Path,const FString& Text);
    void InvalidateTranslation();
    void BuildParts();
    void StartLocal();
    void FinishLocal(FResult Result);
    void StartTranslationRequest(uint64 Version);
    void TranslationFailed(uint64 Version);
    void FallBackToOnline(const TCHAR* Reason);
    static FPart Protect(const FString& Text);
    static bool Restore(const FPart& Part,FString& Text);
};
