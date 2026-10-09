#include "CrankVerifyCommandlet.h"
#include "CrankRuntimeComponent.h"
#include "CrankAudioWave.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
UCrankVerifyCommandlet::UCrankVerifyCommandlet() { IsClient = true; IsEditor = true; IsServer = false; LogToConsole = true; }
int32 UCrankVerifyCommandlet::Main(const FString& Params) {
    if(FParse::Param(*Params,TEXT("AudioBufferOnly"))) {
        auto* Wave=NewObject<UCrankAudioWave>();Wave->AddToRoot();
        TArray<int16> Source;Source.Init(0x1234,8192);
        TArray<int16> Output;Output.Init(-1,8192);
        Wave->PushPCM(reinterpret_cast<const uint8*>(Source.GetData()),Source.Num()*sizeof(int16));
        bool Passed=Wave->GeneratePCMData(reinterpret_cast<uint8*>(Output.GetData()),Output.Num())==Output.Num()*sizeof(int16) && Output==Source;
        Wave->FlushPCM();Wave->GeneratePCMData(reinterpret_cast<uint8*>(Output.GetData()),Output.Num());
        for(int16 Value:Output)Passed&=Value==0;
        auto Producer=Async(EAsyncExecution::Thread,[Wave,Source] {
            for(int32 I=0;I<20000;++I) {
                Wave->PushPCM(reinterpret_cast<const uint8*>(Source.GetData()),Source.Num()*sizeof(int16));
                if(I%7==0)Wave->FlushPCM();
            }
        });
        for(int32 I=0;I<40000;++I) {
            Passed&=Wave->GeneratePCMData(reinterpret_cast<uint8*>(Output.GetData()),Output.Num())==Output.Num()*sizeof(int16);
            for(int16 Value:Output)Passed&=Value==0 || Value==0x1234;
        }
        Producer.Wait();Wave->RemoveFromRoot();
        UE_LOG(LogTemp,Display,TEXT("CRANK AUDIO VERIFY: exact PCM, silence, concurrent push/flush/render passed=%d"),Passed);
        return Passed?0:4;
    }
    FString Game,Save;
    if (!FParse::Value(*Params,TEXT("Game="),Game)) return 1;
    if (!FParse::Value(*Params,TEXT("Save="),Save)) Save=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("CrankSaves"));
    UCrankRuntimeComponent* Runtime = NewObject<UCrankRuntimeComponent>(); Runtime->AddToRoot();
    if (!Runtime->StartGame(Game,Save)) { UE_LOG(LogTemp,Error,TEXT("CRANK VERIFY: %s"),*Runtime->LastError); Runtime->RemoveFromRoot(); return 2; }
    for (int32 i=0;i<150;i++) {
        Runtime->UpdateScreen();
        if (i==70) Runtime->SetButtons(32);
        if (i==76) Runtime->SetButtons(0);
        if (i>=90 && i<130) Runtime->AddCrankDelta(3.f,false);
        if (!Runtime->LastError.IsEmpty()) break;
        FPlatformProcess::Sleep(1.f/60.f);
    }
    const bool bSuccess = Runtime->LastError.IsEmpty() && Runtime->GetFrameNumber()>20 && Runtime->ScreenTexture && Runtime->ScreenTexture->GetSizeX()==400;
    UE_LOG(LogTemp,Display,TEXT("CRANK VERIFY: frames=%d texture=%dx%d error=%s"),Runtime->GetFrameNumber(),Runtime->ScreenTexture->GetSizeX(),Runtime->ScreenTexture->GetSizeY(),*Runtime->LastError);
    Runtime->StopGame(); Runtime->RemoveFromRoot();
    return bSuccess ? 0 : 3;
}
