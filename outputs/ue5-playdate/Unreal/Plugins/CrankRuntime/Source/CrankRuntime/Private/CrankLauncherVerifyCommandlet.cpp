#include "CrankLauncherVerifyCommandlet.h"
#include "CrankLauncher.h"
#include "CrankGameFiles.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UCrankLauncherVerifyCommandlet::UCrankLauncherVerifyCommandlet() {IsClient=true;IsEditor=true;IsServer=false;LogToConsole=true;}
int32 UCrankLauncherVerifyCommandlet::Main(const FString& Params) {
    FString Manifest,Output;
    FParse::Value(*Params,TEXT("Output="),Output);
    auto Result=MakeShared<FJsonObject>();bool Passed=true;
    if(FParse::Value(*Params,TEXT("Cases="),Manifest)) {
        FString Text;TSharedPtr<FJsonObject> Cases;
        if(!FFileHelper::LoadFileToString(Text,*Manifest) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Cases)) return 1;
        TArray<TSharedPtr<FJsonValue>> Rows;
        for(const auto& Value:Cases->GetArrayField(TEXT("cases"))) {
            auto Case=Value->AsObject();FString Error;
            TAtomic<bool> Cancelled(Case->GetBoolField(TEXT("cancelled")));
            const FString Directory=FCrankGameFiles::InstallArchive(Case->GetStringField(TEXT("archive")),Case->GetStringField(TEXT("library")),Error,&Cancelled);
            const bool Valid=(!Directory.IsEmpty())==Case->GetBoolField(TEXT("valid"));Passed=Passed && Valid;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Case->GetStringField(TEXT("name")));Row->SetBoolField(TEXT("passed"),Valid);
            Row->SetStringField(TEXT("directory"),Directory);Row->SetStringField(TEXT("error"),Error);Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Result->SetArrayField(TEXT("archiveCases"),Rows);
    }
    int32 Id=0;
    if(FParse::Value(*Params,TEXT("CatalogId="),Id)) {
        auto Launcher=MakeShared<FCrankLauncher>();Launcher->Select(Id);
        const double SelectionDeadline=FPlatformTime::Seconds()+35;
        while(FPlatformTime::Seconds()<SelectionDeadline) {
            TSharedPtr<FJsonObject> Status;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Launcher->Snapshot()),Status);
            if(Status && Status->GetStringField(TEXT("state"))!=TEXT("checking")) break;
            FHttpModule::Get().GetHttpManager().Tick(.02f);FPlatformProcess::Sleep(.02f);
        }
        Launcher->Play(Id);
        const double Deadline=FPlatformTime::Seconds()+90;FString Launch;
        bool Cancelled=false;int32 CancelBytes=0;FParse::Value(*Params,TEXT("CancelAfterBytes="),CancelBytes);
        while(FPlatformTime::Seconds()<Deadline) {
            FHttpModule::Get().GetHttpManager().Tick(.02f);Launcher->Tick();
            if(FParse::Param(*Params,TEXT("CancelDownload")) && !Cancelled && Launcher->IsBusy()) {Launcher->Cancel();Cancelled=true;}
            TSharedPtr<FJsonObject> Status;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Launcher->Snapshot()),Status);
            if(CancelBytes>0 && !Cancelled && Status && Status->GetNumberField(TEXT("receivedBytes"))>=CancelBytes) {Launcher->Cancel();Cancelled=true;}
            Launch=Launcher->TakeLaunch();if(!Launch.IsEmpty() || !Launcher->IsBusy()) break;
            FPlatformProcess::Sleep(.02f);
        }
        Result->SetStringField(TEXT("launch"),Launch);Result->SetStringField(TEXT("snapshot"),Launcher->Snapshot());Result->SetStringField(TEXT("message"),Launcher->Message());
        Result->SetBoolField(TEXT("cancelled"),Cancelled);
        Passed=Passed && (Cancelled ? !Launcher->IsBusy() && Launch.IsEmpty() : !Launch.IsEmpty());Launcher->Shutdown();
    }
    Result->SetBoolField(TEXT("passed"),Passed);FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));
    if(!Output.IsEmpty()) FFileHelper::SaveStringToFile(Text,*Output);
    UE_LOG(LogTemp,Display,TEXT("CRANK LAUNCHER VERIFY: %s"),*Text);return Passed ? 0 : 2;
}
