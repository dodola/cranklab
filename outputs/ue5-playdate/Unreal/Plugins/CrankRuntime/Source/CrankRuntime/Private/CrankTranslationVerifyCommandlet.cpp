#include "CrankTranslationVerifyCommandlet.h"
#include "CrankTranslation.h"
#include "CrankLauncher.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
UCrankTranslationVerifyCommandlet::UCrankTranslationVerifyCommandlet(){IsClient=true;IsEditor=true;IsServer=false;LogToConsole=true;}
int32 UCrankTranslationVerifyCommandlet::Main(const FString& Params) {
    // The launcher also supplies the established Windows system-proxy setup.
    auto Launcher=MakeShared<FCrankLauncher>();auto Service=MakeShared<FCrankTranslation>();
    auto Pump=[&]{FHttpModule::Get().GetHttpManager().Tick(.02f);Service->Tick();FPlatformProcess::Sleep(.02f);};
    auto Wait=[&](TFunction<bool()> Pending,double Seconds){double Deadline=FPlatformTime::Seconds()+Seconds;while(Pending() && FPlatformTime::Seconds()<Deadline)Pump();return !Pending();};
    auto Result=MakeShared<FJsonObject>();bool Passed=true;
    Result->SetStringField(TEXT("modelProfile"),Service->IsLightModel()?TEXT("stq1"):TEXT("q4"));
    if(FParse::Param(*Params,TEXT("ProfilesOnly"))) {
        const bool BothReady=Service->IsLightModel() && Service->HasModel() && Service->SelectModel(false) && Service->HasModel();
        Service->Shutdown();Service=MakeShared<FCrankTranslation>();
        const bool Persisted=!Service->IsLightModel() && Service->HasModel();
        const bool Separate=FCrankTranslation::CacheKey(1058,TEXT("test"),true)!=FCrankTranslation::CacheKey(1058,TEXT("test"),false);
        Passed=BothReady && Persisted && Separate && Service->SelectModel(true) && Service->HasModel();
        Result->SetBoolField(TEXT("bothModelsRetained"),BothReady);Result->SetBoolField(TEXT("profileSelectionPersists"),Persisted);Result->SetBoolField(TEXT("modelCachesSeparate"),Separate);
    }
    if(FParse::Param(*Params,TEXT("DownloadModel"))) {
        Service->DownloadModel();const bool Guarded=!Service->IsModelBusy() || !Service->SelectModel(!Service->IsLightModel());
        Wait([&]{return Service->IsModelBusy();},120);Passed=Guarded && (FParse::Param(*Params,TEXT("ExpectModelFailure"))?(!Service->HasModel() && Service->HasModelError()):Service->HasModel());
        Result->SetBoolField(TEXT("selectionBlockedDuringDownload"),Guarded);
        Result->SetBoolField(TEXT("modelDownloadPassed"),Passed);Result->SetStringField(TEXT("modelMessage"),Service->ModelMessage());
    }
    if(FParse::Param(*Params,TEXT("CancelDownload"))) {
        Service->DownloadModel();Wait([&]{return Service->IsModelBusy() && (!Service->ModelProgress().IsSet() || Service->ModelProgress().GetValue()<.001f);},10);
        const bool Received=Service->ModelProgress().IsSet() && Service->ModelProgress().GetValue()>0;
        Service->CancelModelDownload();Wait([&]{return Service->IsModelBusy();},10);
        Passed=!Service->HasModel() && !Service->IsModelBusy();Result->SetBoolField(TEXT("modelCancelPassed"),Passed);
        Result->SetBoolField(TEXT("bytesReceivedBeforeCancel"),Received);Passed&=Received;
    }
    if(!FParse::Param(*Params,TEXT("ModelOnly")) && !FParse::Param(*Params,TEXT("ProfilesOnly"))) {
        Service->Open(1058);bool Completed=Wait([&]{return Service->IsBusy();},180);
        const FString Source=Service->GetSource(),Translation=Service->GetTranslated();
        Passed=Passed && Completed && !Source.IsEmpty() && !Translation.IsEmpty() && Translation!=Source && Service->GetState()==TEXT("translated");
        bool Chinese=false;for(TCHAR C:Translation)Chinese|=C>=0x4e00 && C<=0x9fff;Passed&=Chinese;
        Result->SetStringField(TEXT("source"),Source);Result->SetStringField(TEXT("translation"),Translation);Result->SetStringField(TEXT("message"),Service->Message());
        Result->SetBoolField(TEXT("translationAvailablePassed"),Passed);
        Result->SetBoolField(TEXT("realInferencePerformed"),Service->Message().Contains(TEXT("本地翻译完成")));
        const bool Light=Service->IsLightModel();const FString Key=FCrankTranslation::CacheKey(1058,Source,Light);
        Result->SetBoolField(TEXT("sourceChangeInvalidatesCache"),Key!=FCrankTranslation::CacheKey(1058,Source+TEXT(" changed"),Light));
        Passed&=Key!=FCrankTranslation::CacheKey(1058,Source+TEXT(" changed"),Light);
        const bool Separate=Key!=FCrankTranslation::CacheKey(1058,Source,!Light);Passed&=Separate;Result->SetBoolField(TEXT("modelCachesSeparate"),Separate);
        Service->Shutdown();Service=MakeShared<FCrankTranslation>();Service->Open(1058);Wait([&]{return Service->IsBusy();},40);
        const bool Cached=Service->GetTranslated()==Translation && !Translation.IsEmpty();Passed&=Cached;Result->SetBoolField(TEXT("persistentCachePassed"),Cached);
        Result->SetStringField(TEXT("cacheMessage"),Service->Message());
        Result->SetBoolField(TEXT("offlineCacheUsed"),Service->Message().Contains(TEXT("网络不可用")));
        Service->Open(1058);Service->Cancel();Wait([&]{return Service->IsBusy();},10);bool Cancelled=!Service->IsBusy();Passed&=Cancelled;Result->SetBoolField(TEXT("cancelPassed"),Cancelled);
    }
    Service->Shutdown();Launcher->Shutdown();Result->SetBoolField(TEXT("passed"),Passed);
    FString Output,Text;FParse::Value(*Params,TEXT("Output="),Output);FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));
    if(!Output.IsEmpty())FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("CRANK TRANSLATION VERIFY: %s"),*Text);return Passed?0:2;
}
