#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "CrankViewState.h"
#include "CrankDevicePawn.generated.h"

class UCameraComponent;
class UCrankRuntimeComponent;
class UMaterialInstanceDynamic;
class SCrankOverlay;
class SCrankImmersive;
class SCrankSystemScreen;
class FWidgetRenderer;
class UTextureRenderTarget2D;
class UTexture2D;
class UTexture;
class FCrankLauncher;
class FCrankTranslation;
enum class ECrankSystemMode : uint8 {Boot,Home,Playing,Menu,Locked,Loading};

UCLASS()
class CRANKLAB_API ACrankDevicePawn : public APawn {
    GENERATED_BODY()
public:
    ACrankDevicePawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool PlayGame(const FString& Directory);
    void ReleaseInputs();
    void SetOverlayOpen(bool Open);
    void HoldCrank(int Direction, bool Pressed);
    FString GetStatusText() const;
    bool HasError() const;
    void BeginGameImport(bool Folder);
    void BeginUrlGameImport(const FString& Url);
    void RetryGameImport();
    bool CanRetryImport() const;
    bool IsImportBusy() const;
    FString GetImportMessage() const;
    FString GetLauncherMessage() const;
    bool IsLauncherDownloadBusy() const;
    void CancelLauncherDownload();
    TSharedPtr<FCrankTranslation> GetTranslation() const {return Translation;}
    FString GetTitleForGame(const FString& Directory) const;
    FString GetGameTitle() const { return GameTitle; }
    TArray<FString> GetGameDirectories() const;
    void ToggleEnlarged();
    bool IsEnlarged() const { return bEnlarged; }
    float GetEnlargedBlend() const { return EnlargedBlend; }
    void SetImmersive(bool Enabled);
    bool IsImmersive() const {return bImmersive;}
    bool IsControlPressed(FName Control) const;
    UTexture* GetDisplayTexture() const;
    void ResetView();
    void CalibrateMotion();
    void SetMotionFollow(bool Enabled);
    bool IsMotionFollowEnabled() const { return bMotionFollow; }
    bool HasMotionSample() const { return bHasMotionSample; }
    void SetHaptics(bool Enabled);
    bool IsHapticsEnabled() const { return bHaptics; }
    void SetMotionSensitivity(float Value);
    float GetMotionSensitivity() const { return MotionSensitivity; }
    void SetHapticStrength(float Value);
    float GetHapticStrength() const { return HapticStrength; }
    void SetReducedMotion(bool Enabled);
    bool IsReducedMotion() const { return bReducedMotion; }
    void PulseHaptic(bool CrankTick=false);
    void TurnCrank(float Degrees);
    float GetCrankAngle() const { return CrankAngle; }
    void ToggleCrankDocked();
    bool IsCrankDocked() const { return bCrankDocked; }
    FString GetCurrentGameDirectory() const { return CurrentGameDirectory; }
    bool IsOverlayOpen() const { return bOverlayOpen; }
    FString GetExperienceError() const { return ExperienceError; }
    ECrankSystemMode GetSystemMode() const {return SystemMode;}
    int32 GetSystemSelection() const {return SystemSelection;}
    TArray<FString> GetSystemRows() const;
    void ToggleSystemMenu();
    void ReturnHome();
    void ToggleLock();
    bool IsGameRunning() const;
    bool IsGameSwitching() const;
    UTexture2D* GetGameCover(const FString& Directory);
    FString GetGameCapability(const FString& Directory) const;
    void SetFrameRate(int32 Value);
    int32 GetFrameRate() const {return FrameRate;}
    void SetResolutionScale(float Value);
    float GetResolutionScale() const {return ResolutionScale;}
    void SystemNavigate(int32 Direction);
    void SystemActivate(int32 Direction=0);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCrankRuntimeComponent> Runtime;
private:
    bool LoadDevice();
    void UpdateCamera(float DeltaTime=0);
    void UpdateMotion(float DeltaTime);
    void SaveExperience();
    void LoadExperience();
    bool HitScreen(const FVector2D& Position) const;
    bool ScreenPoint(const FVector2D& Position,FVector2D& Pixel) const;
    void UpdateSystem(float DeltaTime);
    void CloseSystemMenu();
    void ScreenTap(const FVector2D& Pixel);
    void ApplySystemPause();
    void ApplyCrankRotation(float Degrees);
    void UpdatePointer(int Id, const FVector2D& Position, bool Down);
    FName HitControl(const FVector2D& Position) const;
    FVector2D ProjectNode(FName Name) const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Nodes;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;
    TMap<FName,USceneComponent*> NamedNodes;
    TMap<FName,FTransform> RestTransforms;
    TMap<int,FName> Pointers;
    TMap<int,FVector2D> PreviousPointers;
    TMap<int,FVector2D> PointerStarts;
    TMap<int,FVector2D> ScreenGestureStarts;
    TMap<int,double> PointerStartTimes;
    TSharedPtr<SCrankOverlay> Overlay;
    TSharedPtr<SCrankImmersive> ImmersiveWidget;
    TSharedPtr<SCrankSystemScreen> SystemWidget;
    FWidgetRenderer* SystemRenderer=nullptr;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> SystemTexture;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UTexture2D>> GameCovers;
    ECrankSystemMode SystemMode=ECrankSystemMode::Boot, BeforeLock=ECrankSystemMode::Playing;
    int32 SystemSelection=0;
    float SystemElapsed=0, SystemPollElapsed=0, SystemCrankTravel=0;
    double PendingScreenTapTime=-1;
    FVector2D PendingScreenTap;
    TArray<FString> RecentGames;
    bool bLaunchRecorded=false;
    FString GameTitle, LastStartError;
    FString CurrentGameDirectory, ExperienceFile, ExperienceError;
    FString LauncherDirectory;
    TSharedPtr<FCrankLauncher> Launcher;
    TSharedPtr<FCrankTranslation> Translation;
    uint32 LauncherSession=0;
    int32 LauncherDetailId=0;
    bool bDefaultLauncher=false;
    void UpdateLauncher();
    int32 LauncherProbePhase=0;
    bool bLauncherProbePassed=false;
    float LauncherProbeCompletedAt=0;
    float LauncherInspectElapsed=0;
    bool bInlineChinese=false,bInlineOriginal=false,bTranslationOriginalTapped=false;
    FString LastLauncherReply;
    bool bTranslationProbeOpened=false,bTranslationProbeDone=false,bTranslationEntryPassed=false,bTranslationOriginalPassed=false;
    float TranslationProbeCompletedAt=0;
    FString ImportMessage;
    bool bImportBusy=false;
    bool bCanRetryImport=false;
    int32 FrameRate=60;
    float ResolutionScale=1;
    void ApplyPerformance();
    float ImportPollElapsed=0;
    float CrankAngle = 0, CrankTarget = 0, CrankVelocity = 0;
    int CrankHolds = 0;
    bool bOverlayOpen = false;
    bool bEnlarged=false, bMotionFollow=true, bHaptics=true, bReducedMotion=false, bCrankDocked=false;
    bool bImmersive=false, bImmersiveProbeRan=false, bImmersiveProbePassed=false;
    int32 ImmersiveProbePhase=0;
    bool bImmersiveHeldA=false, bImmersiveHeldBoth=false, bImmersiveReleasedB=false;
    void UpdateImmersiveProbe();
    void UpdateImmersiveTouchProbe();
    bool bImmersiveTouchProbeRan=false, bImmersiveTouchProbePassed=false;
    int32 ImmersiveTouchProbePhase=0;
    float ImmersiveTouchStartTarget=0;
    bool bImmersiveDialPassed=false, bImmersiveSwipePassed=false, bImmersiveSwipeMultiPassed=false;
    bool bMotionCalibrated=false, bHasMotionSample=false;
    float EnlargedBlend=0, ViewZoom=1, PinchDistance=0;
    float MotionSensitivity=.35f, HapticStrength=.6f, CrankHapticTravel=0;
    double LastHapticTime=-1;
    FVector2D ManualOrbit=FVector2D::ZeroVector, MotionOrbit=FVector2D::ZeroVector, MotionReference=FVector2D::ZeroVector;
    FVector2D IntegratedMotion=FVector2D::ZeroVector;
    FBox ScreenLocalBounds;
    int32 LastSensorOrientation=-1;
    bool bStageTwoProbeRan=false, bStageTwoProbePassed=false;
    int32 SystemFlowPhase=0;
    bool bSystemFlowPassed=false;
    float SystemFlowCompletedAt=0;
    FString SystemFlowGame;
    int32 SystemFlowFrame=0;
    void UpdateSystemFlowProbe();
    CrankView::FDoubleTap ScreenDoubleTap;
    int32 LastButtons = 0;
    int32 SuppressedButtons=0;
    FIntPoint LastViewport;
    FDelegateHandle DeactivateHandle, ReactivateHandle;
    FString ProbeDirectory;
    FString InputProbeDirectory;
    float InputProbeElapsed=0;
    float ProbeElapsed = 0;
    bool bProbeScreenshot = false, bProbePressed = false, bProbeReleased = false;
};

UCLASS()
class CRANKLAB_API ACrankGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    ACrankGameMode();
};
