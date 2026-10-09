#include "CrankRuntimeComponent.h"
#include "CrankNative.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "CrankAudioWave.h"
#include "Components/AudioComponent.h"
#include "Async/Async.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
UCrankRuntimeComponent::UCrankRuntimeComponent() { PrimaryComponentTick.bCanEverTick = true; }
void UCrankRuntimeComponent::BeginPlay() {
    Super::BeginPlay();
    DeactivateHandle=FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this,&UCrankRuntimeComponent::OnDeactivate);
    ReactivateHandle=FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this,&UCrankRuntimeComponent::OnReactivate);
    BackgroundHandle=FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this,&UCrankRuntimeComponent::OnBackground);
    ForegroundHandle=FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this,&UCrankRuntimeComponent::OnForeground);
}
void UCrankRuntimeComponent::OnDeactivate() { bApplicationActive=false; ApplySuspension(); }
void UCrankRuntimeComponent::OnReactivate() { bApplicationActive=true; ApplySuspension(); }
void UCrankRuntimeComponent::OnBackground() { bApplicationForeground=false; ApplySuspension(); }
void UCrankRuntimeComponent::OnForeground() { bApplicationForeground=true; ApplySuspension(); }
void UCrankRuntimeComponent::SetGamePaused(bool Paused) { bGamePaused=Paused; ApplySuspension(); }
void UCrankRuntimeComponent::SetDeviceLocked(bool Locked) {bDeviceLocked=Locked; ApplySuspension();}
void UCrankRuntimeComponent::ApplySuspension() {
    const uint32 Reasons=((bGamePaused || !bApplicationActive) ? 1u : 0u) | ((!bApplicationForeground || bDeviceLocked) ? 2u : 0u);
    const bool Changed=Reasons!=SuspensionReasons;
    SuspensionReasons=Reasons;
    if (bStarted) FCrankNative::Get().Suspend(Reasons);
    if (Changed) {
        if (AudioComponent) AudioComponent->SetPaused(Reasons!=0);
        if (AudioWave) AudioWave->FlushPCM();
        UE_LOG(LogTemp,Display,TEXT("Crank suspension requested: %u"),Reasons);
    }
}
bool UCrankRuntimeComponent::StartGame(const FString& GameDirectory, const FString& SaveDirectory) {
    if (bStarted) { LastError = TEXT("A game is already running"); return false; }
    auto& Native = FCrankNative::Get();
    if (!Native.Load()) { LastError = TEXT("Runtime library is unavailable for this platform"); return false; }
    FString Game = FPaths::ConvertRelativePathToFull(GameDirectory), Save = FPaths::ConvertRelativePathToFull(SaveDirectory);
    FPaths::NormalizeDirectoryName(Game); FPaths::NormalizeDirectoryName(Save);
    if (!FPaths::FileExists(FPaths::Combine(Game,TEXT("pdxinfo")))) { LastError = TEXT("PDX metadata was not found"); return false; }
    IFileManager::Get().MakeDirectory(*Save,true);
    if (!Native.Start(TCHAR_TO_UTF8(*Game),TCHAR_TO_UTF8(*Save))) { LastError = TEXT("Runtime could not start the game"); return false; }
    PrepareScreenAndAudio();
    return true;
}
void UCrankRuntimeComponent::PrepareScreenAndAudio() {
    ScreenTexture = UTexture2D::CreateTransient(400,240,PF_B8G8R8A8);
    ScreenTexture->SRGB = false; ScreenTexture->Filter = TF_Nearest; ScreenTexture->NeverStream = true;
    ScreenTexture->UpdateResource();
    Pixels.SetNumUninitialized(400*240*4); FrameNumber = 0; LastError.Empty(); bStarted = true;
    AudioSamplesReceived = 0;
    ExecutionMode.Empty();NativeArmCalls=NativeApiCalls=0;NativeInfoUpdatedAt=0;
    if (bEnableAudio && GetWorld() && !IsRunningCommandlet()) {
        AudioWave = NewObject<UCrankAudioWave>(this);
        AudioWave->SetSampleRate(44100); AudioWave->NumChannels = 2;
        AudioWave->Duration = INDEFINITELY_LOOPING_DURATION;
        AudioWave->bLooping = false; AudioWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
        AudioComponent = NewObject<UAudioComponent>(this);
        AudioComponent->bAutoActivate = false; AudioComponent->bIsUISound = true;
        AudioComponent->RegisterComponentWithWorld(GetWorld());
        AudioComponent->SetSound(AudioWave); AudioComponent->Play();
    }
    ApplySuspension();
    if (AudioComponent && SuspensionReasons) AudioComponent->SetPaused(true);
}
void UCrankRuntimeComponent::StopAudio() {
    if (AudioComponent) { AudioComponent->Stop(); AudioComponent->DestroyComponent(); AudioComponent=nullptr; }
    AudioWave=nullptr;
}
void UCrankRuntimeComponent::StopGame() {
    if(bSwitching) {SwitchTask.Wait(); const bool Started=SwitchTask.Get(); bStarted=bPendingLaunch && Started; bSwitching=false;}
    StopAudio();
    if (bStarted) { FCrankNative::Get().Stop(); bStarted = false; }
}
bool UCrankRuntimeComponent::RequestGame(const FString& GameDirectory,const FString& SaveDirectory,bool Launcher) {
    if(bSwitching) return false;
    auto& Native=FCrankNative::Get();
    if(!Native.Load()) {LastError=TEXT("Runtime library is unavailable for this platform"); return false;}
    FString Game=FPaths::ConvertRelativePathToFull(GameDirectory), Save=FPaths::ConvertRelativePathToFull(SaveDirectory);
    FPaths::NormalizeDirectoryName(Game); FPaths::NormalizeDirectoryName(Save);
    if(!FPaths::FileExists(Game/TEXT("pdxinfo")) || !IFileManager::Get().MakeDirectory(*Save,true)) {LastError=TEXT("Game or save directory is unavailable"); return false;}
    Native.Suspend(1); StopAudio(); bStarted=false; bSwitching=true; bPendingLaunch=true;
    LastError.Empty(); FrameNumber=0; LastMenuJson.Empty(); MenuItems.Empty(); bHasMenuImage=false; MenuImageNumber=0;
    SwitchTask=Async(EAsyncExecution::Thread,[Game,Save,Launcher]() {
        auto& Core=FCrankNative::Get(); Core.Stop();
        return (Launcher ? Core.StartLauncher : Core.Start)(TCHAR_TO_UTF8(*Game),TCHAR_TO_UTF8(*Save))!=0;
    });
    return true;
}
bool UCrankRuntimeComponent::PollLauncherRequest(FString& Json) {
    if(!bStarted || bSwitching) return false;
    ANSICHAR Buffer[256]{};const uint32 Size=FCrankNative::Get().LauncherRequest(Buffer,UE_ARRAY_COUNT(Buffer));
    if(!Size || Size>UE_ARRAY_COUNT(Buffer)) return false;
    Json=UTF8_TO_TCHAR(Buffer);return true;
}
bool UCrankRuntimeComponent::ActivateLauncherDetail(uint32 Action) {return bStarted && !bSwitching && FCrankNative::Get().LauncherDetailAction(Action)!=0;}
bool UCrankRuntimeComponent::ReplyLauncher(uint32 Session,const FString& Json) {
    return bStarted && !bSwitching && FCrankNative::Get().LauncherReply(Session,TCHAR_TO_UTF8(*Json))!=0;
}
bool UCrankRuntimeComponent::ActivateLauncher() {
    return bStarted && !bSwitching && FCrankNative::Get().LauncherActivate()!=0;
}
void UCrankRuntimeComponent::RequestStop() {
    if(bSwitching) return;
    StopAudio(); if(!bStarted) return;
    auto& Native=FCrankNative::Get(); Native.MenuOpen(0); Native.Suspend(1);
    bStarted=false; bSwitching=true; bPendingLaunch=false;
    SwitchTask=Async(EAsyncExecution::Thread,[](){FCrankNative::Get().Stop(); return true;});
}
bool UCrankRuntimeComponent::IsRunning() const {return bStarted && (GetRuntimeStatus()==2 || GetRuntimeStatus()==4);}
int32 UCrankRuntimeComponent::GetRuntimeStatus() const {return bStarted ? FCrankNative::Get().Status() : 0;}
void UCrankRuntimeComponent::SetSystemMenuOpen(bool Open) {if(bStarted) FCrankNative::Get().MenuOpen(Open ? 1 : 0);}
bool UCrankRuntimeComponent::SelectMenuItem(uint32 Id,int32 Value) {return bStarted && FCrankNative::Get().MenuSelect(Id,Value)!=0;}
bool UCrankRuntimeComponent::RefreshMenu() {
    if(!bStarted) return false;
    auto& Native=FCrankNative::Get(); FString Text;
    for(int Retry=0;Retry<3;Retry++) {
        const uint32 Needed=Native.Menu(nullptr,0); if(!Needed || Needed>1024*1024) return false;
        TArray<ANSICHAR> Buffer; Buffer.SetNumZeroed(Needed);
        if(Native.Menu(Buffer.GetData(),Buffer.Num())>Needed) continue;
        Text=UTF8_TO_TCHAR(Buffer.GetData()); break;
    }
    bool Changed=false;
    if(!Text.IsEmpty() && Text!=LastMenuJson) {
        TSharedPtr<FJsonObject> Root;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)) return false;
        MenuItems.Empty(); const TArray<TSharedPtr<FJsonValue>>* Rows;
        if(Root->TryGetArrayField(TEXT("items"),Rows)) for(const auto& Row:*Rows) {
            const auto Object=Row->AsObject(); if(!Object) continue;
            FCrankMenuItem Item; Item.Id=Object->GetIntegerField(TEXT("id")); Item.Kind=Object->GetIntegerField(TEXT("kind"));
            Item.Value=Object->GetIntegerField(TEXT("value")); Item.Title=Object->GetStringField(TEXT("title"));
            const TArray<TSharedPtr<FJsonValue>>* Options;
            if(Object->TryGetArrayField(TEXT("options"),Options)) for(const auto& Option:*Options) Item.Options.Add(Option->AsString());
            MenuItems.Add(MoveTemp(Item));
        }
        Root->TryGetBoolField(TEXT("hasImage"),bHasMenuImage); double Offset=0; Root->TryGetNumberField(TEXT("imageOffset"),Offset); MenuImageOffset=Offset;
        LastMenuJson=Text; Changed=true;
    }
    if(bHasMenuImage) {
        TArray<uint8> Image; Image.SetNumUninitialized(400*240*4); const uint32 Number=Native.MenuImage(Image.GetData(),Image.Num());
        if(Number && Number!=MenuImageNumber) {
            if(!MenuImageTexture) {MenuImageTexture=UTexture2D::CreateTransient(400,240,PF_B8G8R8A8); MenuImageTexture->SRGB=false; MenuImageTexture->Filter=TF_Nearest; MenuImageTexture->NeverStream=true; MenuImageTexture->UpdateResource();}
            auto* Copy=new uint8[Image.Num()]; FMemory::Memcpy(Copy,Image.GetData(),Image.Num());
            auto* Region=new FUpdateTextureRegion2D(0,0,0,0,400,240);
            MenuImageTexture->UpdateTextureRegions(0,1,Region,1600,4,Copy,[](uint8* Data,const FUpdateTextureRegion2D* Regions){delete[] Data; delete Regions;});
            MenuImageNumber=Number; Changed=true;
        }
    }
    return Changed;
}
void UCrankRuntimeComponent::SetButtons(int32 Mask) { if (bStarted) FCrankNative::Get().Buttons(SuspensionReasons ? 0 : Mask); }
void UCrankRuntimeComponent::AddCrankDelta(float Degrees,bool bDocked) { if (bStarted && !SuspensionReasons) FCrankNative::Get().Crank(Degrees,bDocked?1:0); }
void UCrankRuntimeComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) {
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(bSwitching && SwitchTask.IsReady()) {
        const bool Succeeded=SwitchTask.Get(); bSwitching=false;
        if(bPendingLaunch && Succeeded) PrepareScreenAndAudio();
        else if(bPendingLaunch) LastError=TEXT("Runtime could not start the game");
    }
    UpdateScreen();
}
void UCrankRuntimeComponent::UpdateScreen() {
    if (!bStarted) return;
    auto& Native = FCrankNative::Get();
    const double Now=FPlatformTime::Seconds();
    if(Now-NativeInfoUpdatedAt>=1.0) {
        NativeInfoUpdatedAt=Now;const uint32 Needed=Native.NativeInfo(nullptr,0);
        if(Needed>1 && Needed<=65536) {
            TArray<ANSICHAR> Buffer;Buffer.SetNumZeroed(Needed);
            if(Native.NativeInfo(Buffer.GetData(),Needed)<=Needed) {
                TSharedPtr<FJsonObject> Info;
                if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(UTF8_TO_TCHAR(Buffer.GetData()))),Info)) {
                    Info->TryGetStringField(TEXT("mode"),ExecutionMode);double Calls=0;
                    if(Info->TryGetNumberField(TEXT("armCalls"),Calls))NativeArmCalls=static_cast<int64>(Calls);
                    if(Info->TryGetNumberField(TEXT("apiCalls"),Calls))NativeApiCalls=static_cast<int64>(Calls);
                }
            }
        }
    }
    if (Native.Status()==3) { char Error[2048]{}; Native.Error(Error,sizeof(Error)); LastError = UTF8_TO_TCHAR(Error); return; }
    if(SuspensionReasons) return;
    int16 Samples[8192];
    uint32 Count = Native.Audio(Samples,UE_ARRAY_COUNT(Samples));
    AudioSamplesReceived += Count;
    if (Count && bEnableAudio && AudioWave) {
        AudioWave->PushPCM(reinterpret_cast<const uint8*>(Samples),Count*sizeof(int16));
    }
    uint32 Serial = Native.Frame(Pixels.GetData(),Pixels.Num());
    if (!Serial || Serial==FrameNumber) return;
    FrameNumber = Serial;
    auto* Copy = new uint8[Pixels.Num()]; FMemory::Memcpy(Copy,Pixels.GetData(),Pixels.Num());
    auto* Region = new FUpdateTextureRegion2D(0,0,0,0,400,240);
    ScreenTexture->UpdateTextureRegions(0,1,Region,400*4,4,Copy,
        [](uint8* Data,const FUpdateTextureRegion2D* Regions){ delete[] Data; delete Regions; });
}
void UCrankRuntimeComponent::EndPlay(const EEndPlayReason::Type Reason) {
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
    FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivateHandle);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
    StopGame(); Super::EndPlay(Reason);
}
