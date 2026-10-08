#include "CrankTranslationVerifyCommandlet.h"
#include "CrankTranslation.h"
#include "CrankLauncher.h"
#include "CrankGameFiles.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
UCrankTranslationVerifyCommandlet::UCrankTranslationVerifyCommandlet(){IsClient=true;IsEditor=true;IsServer=false;LogToConsole=true;}
int32 UCrankTranslationVerifyCommandlet::Main(const FString& Params) {
    auto Launcher=MakeShared<FCrankLauncher>();auto Service=MakeShared<FCrankTranslation>();
    auto Pump=[&]{FHttpModule::Get().GetHttpManager().Tick(.02f);Service->Tick();FPlatformProcess::Sleep(.02f);};
    auto Wait=[&](TFunction<bool()> Pending,double Seconds){double Deadline=FPlatformTime::Seconds()+Seconds;while(Pending() && FPlatformTime::Seconds()<Deadline)Pump();return !Pending();};
    auto Result=MakeShared<FJsonObject>();bool Passed=true;int32 Id=1058;FParse::Value(*Params,TEXT("CatalogId="),Id);
    const double Started=FPlatformTime::Seconds();Service->Open(Id);
    if(FParse::Param(*Params,TEXT("CancelRequest"))) {
        const bool StartedRequest=Wait([&]{return Service->GetState()!=TEXT("translating") && Service->IsBusy();},10) && Service->GetState()==TEXT("translating");
        Service->Cancel();const double Until=FPlatformTime::Seconds()+3;while(FPlatformTime::Seconds()<Until)Pump();
        TArray<FString> Cache;IFileManager::Get().FindFiles(Cache,*(FCrankGameFiles::Root()/TEXT("Translation/Cache/*.json")),true,false);
        Passed=StartedRequest && !Service->IsBusy() && Service->GetTranslated().IsEmpty() && Cache.IsEmpty();Result->SetBoolField(TEXT("cancelIgnoresLateResponse"),Passed);
    }else {
        const bool ImmediateCache=Service->GetState()==TEXT("translated");const bool Completed=Wait([&]{return Service->IsBusy();},35);
        if(FParse::Param(*Params,TEXT("ExpectError"))) {
            Passed=Completed && Service->HasError() && !Service->GetSource().IsEmpty() && Service->GetTranslated().IsEmpty();Result->SetBoolField(TEXT("errorPreservesOriginal"),Passed);
        }else {
            const FString Source=Service->GetSource(),Translation=Service->GetTranslated();bool Chinese=false;for(TCHAR C:Translation)Chinese|=C>=0x4e00 && C<=0x9fff;
            Passed=Completed && Chinese && !Source.IsEmpty() && !Translation.IsEmpty() && Translation!=Source && Service->GetState()==TEXT("translated") && !Translation.Contains(TEXT("ZXQK"));
            Result->SetStringField(TEXT("source"),Source);Result->SetStringField(TEXT("translation"),Translation);Result->SetBoolField(TEXT("translationAvailablePassed"),Passed);Result->SetBoolField(TEXT("immediateCacheUsed"),ImmediateCache);
            if(FParse::Param(*Params,TEXT("ExpectCached")))Passed&=ImmediateCache;
            const FString Key=FCrankTranslation::CacheKey(Id,Source,TEXT("microsoft"));
            const bool Changed=Key!=FCrankTranslation::CacheKey(Id,Source+TEXT(" changed"),TEXT("microsoft"));
            const bool Separate=Key!=FCrankTranslation::CacheKey(Id,Source,TEXT("tencent"));Passed&=Changed && Separate;Result->SetBoolField(TEXT("sourceChangeInvalidatesCache"),Changed);Result->SetBoolField(TEXT("providerCachesSeparate"),Separate);
            Service->Shutdown();Service=MakeShared<FCrankTranslation>();Service->Open(Id);
            const bool Cached=Service->GetTranslated()==Translation && !Translation.IsEmpty();Passed&=Cached;Result->SetBoolField(TEXT("persistentCachePassed"),Cached);Service->Cancel();
        }
    }
    Result->SetNumberField(TEXT("elapsedSeconds"),FPlatformTime::Seconds()-Started);Result->SetStringField(TEXT("message"),Service->Message());Result->SetStringField(TEXT("provider"),Service->GetProvider());
    if(FParse::Param(*Params,TEXT("TestProviderSelection"))) {
        const bool Selected=Service->SelectProvider(TEXT("tencent"));Service->Shutdown();Service=MakeShared<FCrankTranslation>();
        const bool Persisted=Service->GetProvider()==TEXT("tencent");Passed&=Selected && Persisted && !Service->SelectProvider(TEXT("invalid")) && Service->SelectProvider(TEXT("auto"));
        Result->SetBoolField(TEXT("providerSelectionPersists"),Persisted);
    }
    Service->Shutdown();Launcher->Shutdown();Result->SetBoolField(TEXT("passed"),Passed);
    FString Output,Text;FParse::Value(*Params,TEXT("Output="),Output);FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));
    if(!Output.IsEmpty())FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("CRANK TRANSLATION VERIFY: %s"),*Text);return Passed?0:2;
}
