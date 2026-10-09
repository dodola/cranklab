#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Async/Future.h"
#include "CrankRuntimeComponent.generated.h"
class UTexture2D;
class UCrankAudioWave;
class UAudioComponent;
struct CRANKRUNTIME_API FCrankMenuItem {
    uint32 Id=0;
    int32 Kind=0, Value=0;
    FString Title;
    TArray<FString> Options;
};
UCLASS(ClassGroup=(Crank), meta=(BlueprintSpawnableComponent))
class CRANKRUNTIME_API UCrankRuntimeComponent : public UActorComponent {
    GENERATED_BODY()
public:
    UCrankRuntimeComponent();
    UFUNCTION(BlueprintCallable, Category="Crank") bool StartGame(const FString& GameDirectory, const FString& SaveDirectory);
    UFUNCTION(BlueprintCallable, Category="Crank") void StopGame();
    UFUNCTION(BlueprintCallable, Category="Crank") void SetButtons(int32 Mask);
    UFUNCTION(BlueprintCallable, Category="Crank") void AddCrankDelta(float Degrees, bool bDocked);
    UFUNCTION(BlueprintCallable, Category="Crank") void SetGamePaused(bool Paused);
    bool RequestGame(const FString& GameDirectory,const FString& SaveDirectory,bool Launcher=false);
    bool PollLauncherRequest(FString& Json);
    bool ReplyLauncher(uint32 Session,const FString& Json);
    bool ActivateLauncherDetail(uint32 Action);
    bool ActivateLauncher();
    void RequestStop();
    bool IsSwitching() const {return bSwitching;}
    bool IsRunning() const;
    int32 GetRuntimeStatus() const;
    void SetDeviceLocked(bool Locked);
    void SetSystemMenuOpen(bool Open);
    bool SelectMenuItem(uint32 Id,int32 Value);
    bool RefreshMenu();
    TArray<FCrankMenuItem> MenuItems;
    bool bHasMenuImage=false;
    int32 MenuImageOffset=0;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> MenuImageTexture;
    UFUNCTION(BlueprintPure, Category="Crank") bool IsSuspended() const { return SuspensionReasons!=0; }
    UFUNCTION(BlueprintPure, Category="Crank") int32 GetFrameNumber() const { return FrameNumber; }
    void UpdateScreen();
    UPROPERTY(BlueprintReadOnly, Category="Crank") TObjectPtr<UTexture2D> ScreenTexture;
    UPROPERTY(BlueprintReadOnly, Category="Crank") FString LastError;
    UPROPERTY(BlueprintReadOnly, Category="Crank") FString ExecutionMode;
    UPROPERTY(BlueprintReadOnly, Category="Crank") int64 NativeArmCalls=0;
    UPROPERTY(BlueprintReadOnly, Category="Crank") int64 NativeApiCalls=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crank") bool bEnableAudio = true;
    UPROPERTY(BlueprintReadOnly, Category="Crank") int64 AudioSamplesReceived = 0;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    bool bStarted = false;
    bool bGamePaused=false, bDeviceLocked=false, bApplicationActive=true, bApplicationForeground=true;
    bool bSwitching=false, bPendingLaunch=false;
    TFuture<bool> SwitchTask;
    FString LastMenuJson;
    double NativeInfoUpdatedAt=0;
    uint32 MenuImageNumber=0;
    void PrepareScreenAndAudio();
    void StopAudio();
    uint32 SuspensionReasons=0;
    FDelegateHandle DeactivateHandle, ReactivateHandle, BackgroundHandle, ForegroundHandle;
    void ApplySuspension();
    void OnDeactivate();
    void OnReactivate();
    void OnBackground();
    void OnForeground();
    uint32 FrameNumber = 0;
    TArray<uint8> Pixels;
    UPROPERTY(Transient) TObjectPtr<UCrankAudioWave> AudioWave;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> AudioComponent;
};
