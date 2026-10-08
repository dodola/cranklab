#pragma once
#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

// Online translation of public game descriptions. Completed translations are
// cached privately; the PDGames adapter continues to own Chinese/original UI.
class CRANKRUNTIME_API FCrankTranslation : public TSharedFromThis<FCrankTranslation> {
public:
    FCrankTranslation();
    ~FCrankTranslation();
    void Open(int32 CatalogId);
    void Translate();
    void Cancel();
    void Tick() {}
    void Shutdown();
    bool SelectProvider(const FString& Provider);
    FString GetProvider() const {return Provider;}
    FString ProviderMessage() const;
    bool IsBusy() const {return DetailRequest.IsValid() || TranslationRequest.IsValid();}
    bool HasError() const {return State==TEXT("error");}
    FString GetState() const {return State;}
    FString GetTitle() const {return Title;}
    FString GetSource() const {return Source;}
    FString GetTranslated() const {return Translated;}
    FString Message() const {return Status;}
    int32 GetId() const {return Id;}
    static FString Normalize(const FString& Text);
    static FString CacheKey(int32 Id,const FString& Text,const FString& Provider=TEXT("microsoft"));
private:
    struct FPart {FString Text,JoinAfter;TArray<TPair<FString,FString>> Keep;};
    FString Storage,Title,Source,Translated,Status;
    FString Provider=TEXT("auto"),ActiveProvider;
    FString State=TEXT("idle");
    TArray<FPart> Parts;
    int32 Id=0,Attempt=0;
    uint64 Revision=0,TranslationRevision=0;
    bool bClosed=false,bTranslationRequested=false;
    double RequestStarted=0;
    TSharedPtr<IHttpRequest,ESPMode::ThreadSafe> DetailRequest,TranslationRequest;
    void AcceptDescription(const FString& Text);
    bool ReadTranslationCache();
    bool SaveJson(const FString& Path,const FString& Text);
    void StartTranslationRequest(uint64 Version);
    void TranslationFailed(uint64 Version);
    static FPart Protect(const FString& Text);
    static bool Restore(const FPart& Part,FString& Text);
};
