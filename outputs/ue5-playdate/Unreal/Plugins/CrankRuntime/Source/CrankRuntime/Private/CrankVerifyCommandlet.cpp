#include "CrankVerifyCommandlet.h"
#include "CrankRuntimeComponent.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
UCrankVerifyCommandlet::UCrankVerifyCommandlet() { IsClient = true; IsEditor = true; IsServer = false; LogToConsole = true; }
int32 UCrankVerifyCommandlet::Main(const FString& Params) {
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
