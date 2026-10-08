#include "CrankDevicePawn.h"
#include "CrankRuntimeComponent.h"
#include "CrankLauncher.h"
#include "CrankTranslation.h"
#include "CrankGameFiles.h"
#include "CrankHaptics.h"
#include "CrankOverlay.h"
#include "CrankImmersive.h"
#include "CrankSystemScreen.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Camera/CameraComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "InputCoreTypes.h"

ACrankGameMode::ACrankGameMode() { DefaultPawnClass = ACrankDevicePawn::StaticClass(); }
ACrankDevicePawn::ACrankDevicePawn() {
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DeviceRoot"));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(RootComponent);
    Camera->FieldOfView = 42; Camera->bConstrainAspectRatio = false;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
    // Our material already reproduces the source viewer's lighting. Avoid a
    // second filmic color transform, which changes its yellow and chrome.
    Camera->PostProcessBlendWeight=1;
    auto& PP=Camera->PostProcessSettings;
    PP.bOverride_ToneCurveAmount=true; PP.ToneCurveAmount=0;
    PP.bOverride_ExpandGamut=true; PP.ExpandGamut=0;
    PP.bOverride_BlueCorrection=true; PP.BlueCorrection=0;
    PP.bOverride_VignetteIntensity=true; PP.VignetteIntensity=0;
    PP.bOverride_AutoExposureMethod=true; PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=0;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    PP.AutoExposureApplyPhysicalCameraExposure=false;
    Runtime = CreateDefaultSubobject<UCrankRuntimeComponent>(TEXT("Runtime"));
}

static FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key) {
    const auto& A = Object->GetArrayField(Key);
    return FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
}

bool ACrankDevicePawn::LoadDevice() {
    FString Text; TArray<uint8> Data; TSharedPtr<FJsonObject> Model;
    const FString Folder = FPaths::ProjectContentDir()/TEXT("Device/Data");
    if (!FFileHelper::LoadFileToString(Text, *(Folder/TEXT("device.json"))) ||
        !FFileHelper::LoadFileToArray(Data, *(Folder/TEXT("device.bin"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Model)) return false;
    for (const auto& Value : Model->GetArrayField(TEXT("nodes"))) {
        auto Node = Value->AsObject();
        const FName Name(*Node->GetStringField(TEXT("name")));
        const auto& Sections = Node->GetArrayField(TEXT("sections"));
        USceneComponent* Component;
        UProceduralMeshComponent* Mesh = nullptr;
        if (Sections.Num()) {
            Mesh = NewObject<UProceduralMeshComponent>(this, Name);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetCastShadow(true); Component = Mesh;
        } else Component = NewObject<USceneComponent>(this, Name);
        const int Parent = Node->GetIntegerField(TEXT("parent"));
        Component->SetupAttachment(Parent >= 0 ? Nodes[Parent].Get() : RootComponent.Get());
        const auto& Q = Node->GetArrayField(TEXT("rotation"));
        FTransform Transform(FQuat(Q[0]->AsNumber(), Q[1]->AsNumber(), Q[2]->AsNumber(), Q[3]->AsNumber()),
            ReadVector(Node,TEXT("position")), ReadVector(Node,TEXT("scale")));
        Component->SetRelativeTransform(Transform); Component->RegisterComponent();
        Nodes.Add(Component); NamedNodes.Add(Name,Component); RestTransforms.Add(Name,Transform);
        int SectionNumber = 0;
        for (const auto& S : Sections) {
            auto Section = S->AsObject();
            const int Offset = Section->GetIntegerField(TEXT("offset"));
            const int VertexCount = Section->GetIntegerField(TEXT("vertices"));
            const int IndexCount = Section->GetIntegerField(TEXT("indices"));
            if (Offset < 0 || int64(Offset)+int64(VertexCount)*32+int64(IndexCount)*4 > Data.Num()) return false;
            TArray<FVector> Positions, Normals; TArray<FVector2D> UV;
            TArray<int32> Indices; TArray<FProcMeshTangent> Tangents;
            const float* Values = reinterpret_cast<const float*>(Data.GetData()+Offset);
            for (int V=0;V<VertexCount;V++) {
                const float* P = Values+V*8;
                Positions.Add(FVector(P[0],P[1],P[2])); Normals.Add(FVector(P[3],P[4],P[5])); UV.Add(FVector2D(P[6],P[7]));
            }
            Indices.SetNum(IndexCount);
            FMemory::Memcpy(Indices.GetData(),Data.GetData()+Offset+VertexCount*32,IndexCount*4);
            TArray<FVector> CalculatedNormals;
            UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Positions,Indices,UV,CalculatedNormals,Tangents);
            Mesh->CreateMeshSection_LinearColor(SectionNumber,Positions,Indices,Normals,UV,{},Tangents,false);
            const FString MaterialName = Section->GetStringField(TEXT("material"));
            UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/Device/Materials/M_")+MaterialName));
            if (MaterialName == TEXT("asheville_screen")) {
                ScreenLocalBounds=FBox(Positions);
                ScreenMaterial = UMaterialInstanceDynamic::Create(Material,this);
                Material = ScreenMaterial;
            }
            Mesh->SetMaterial(SectionNumber++,Material);
        }
    }
    // Unfold using the original model's local pivots. Converted local Y -> UE Z;
    // converted local X -> UE Y. Turning the handle against the axle keeps it upright.
    NamedNodes[TEXT("arm_pivot")]->SetRelativeRotation(FQuat(FVector::UpVector,PI));
    ApplyCrankRotation(0);
    UE_LOG(LogTemp,Display,TEXT("Crank device loaded: %d nodes"),Nodes.Num());
    return true;
}

void ACrankDevicePawn::ApplyCrankRotation(float Degrees) {
    if (auto* Arm=NamedNodes.FindRef(TEXT("arm_pivot"))) Arm->SetRelativeRotation(FQuat(FVector::UpVector,bCrankDocked ? 0.f : PI));
    const FQuat Rotation(FVector::RightVector,-FMath::DegreesToRadians(Degrees));
    if (auto* Axle=NamedNodes.FindRef(TEXT("axle"))) Axle->SetRelativeRotation(Rotation);
    // Match the webpage: rotate the handle's pivot, not its offset mesh.
    // The unfolded arm reverses this local axis, keeping the grip upright.
    if (auto* Handle=NamedNodes.FindRef(TEXT("handle_pivot"))) Handle->SetRelativeRotation(Rotation);
}

void ACrankDevicePawn::BeginPlay() {
    Super::BeginPlay();
    FParse::Value(FCommandLine::Get(),TEXT("CrankSceneProbe="),ProbeDirectory);
    ExperienceFile=FCrankGameFiles::Root()/TEXT("experience.json");
    if(!ProbeDirectory.IsEmpty()) ExperienceFile=ProbeDirectory/TEXT("experience.json");
    FParse::Value(FCommandLine::Get(),TEXT("CrankExperience="),ExperienceFile);
    LoadExperience();
    ApplyPerformance();
    if (!ProbeDirectory.IsEmpty()) { bMotionFollow=false; bCrankDocked=false; }
    bEnlarged=FParse::Param(FCommandLine::Get(),TEXT("CrankEnlarged"));
    if(FParse::Param(FCommandLine::Get(),TEXT("CrankImmersive"))) bImmersive=true;
    if(!ProbeDirectory.IsEmpty() && !FParse::Param(FCommandLine::Get(),TEXT("CrankImmersive"))) bImmersive=false;
    EnlargedBlend=bEnlarged ? 1 : 0;
    if (!LoadDevice()) LastStartError = TEXT("设备模型未加载，请重新生成项目资源。");
    // A matte, unlit backdrop keeps the model alone in the frame.
    auto* Backdrop = NewObject<UProceduralMeshComponent>(this,TEXT("Backdrop"));
    Backdrop->SetupAttachment(RootComponent); Backdrop->RegisterComponent();
    Backdrop->CreateMeshSection_LinearColor(0,{{-12,-100,-100},{-12,100,-100},{-12,100,100},{-12,-100,100}},
        {0,2,1,0,3,2},{{1,0,0},{1,0,0},{1,0,0},{1,0,0}},{{0,1},{1,1},{1,0},{0,0}},{},{},false);
    Backdrop->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Device/Materials/M_Backdrop")));
    // The same soft alpha shadow used by the webpage, behind the device.
    auto* Shadow=NewObject<UProceduralMeshComponent>(this,TEXT("GroundShadow"));
    Shadow->SetupAttachment(RootComponent); Shadow->RegisterComponent();
    Shadow->SetCollisionEnabled(ECollisionEnabled::NoCollision); Shadow->SetCastShadow(false);
    Shadow->CreateMeshSection_LinearColor(0,
        {{-4,-4.064,-8.648},{-4,3.936,-8.648},{-4,3.936,5.752},{-4,-4.064,5.752}},
        {0,2,1,0,3,2},{{1,0,0},{1,0,0},{1,0,0},{1,0,0}},
        {{0,1},{1,1},{1,0},{0,0}},{},{},false);
    Shadow->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Device/Materials/M_GroundShadow")));
    if (auto* PC = Cast<APlayerController>(GetController())) {
        PC->bShowMouseCursor = true; PC->bEnableTouchEvents = true;
        PC->bEnableMotionControls=true;
        FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
    }
    FPlatformApplicationMisc::EnableMotionData(bMotionFollow);
    DeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddLambda([this]{ReleaseInputs();if(Translation){Translation->Cancel();Translation->ReleaseModel();}});
    ReactivateHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this,&ACrankDevicePawn::ReleaseInputs);
    if (GEngine && GEngine->GameViewport) {
        ImmersiveWidget=SNew(SCrankImmersive).Device(this)
            .Visibility_Lambda([this]{return bImmersive ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;});
        GEngine->GameViewport->AddViewportWidgetContent(ImmersiveWidget.ToSharedRef(),5);
        Overlay = SNew(SCrankOverlay).Device(this).Visibility(EVisibility::SelfHitTestInvisible);
        GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(),10);
    }
    if(bImmersive) {
        TInlineComponentArray<UPrimitiveComponent*> Meshes; GetComponents(Meshes);
        for(auto* Mesh:Meshes) Mesh->SetVisibility(false);
    }
    UpdateCamera();
    SystemRenderer=new FWidgetRenderer(false,true);
    SystemTexture=FWidgetRenderer::CreateTargetFor({400,240},TF_Nearest,false);
    SystemWidget=SNew(SCrankSystemScreen).Device(this);
    FParse::Value(FCommandLine::Get(),TEXT("CrankSceneProbe="),ProbeDirectory);
    if (!ProbeDirectory.IsEmpty()) IFileManager::Get().MakeDirectory(*ProbeDirectory,true);
    FParse::Value(FCommandLine::Get(),TEXT("CrankInputProbe="),InputProbeDirectory);
    if (!InputProbeDirectory.IsEmpty()) IFileManager::Get().MakeDirectory(*InputProbeDirectory,true);
    FString Directory;
    bDefaultLauncher=ProbeDirectory.IsEmpty() || (FParse::Param(FCommandLine::Get(),TEXT("CrankLauncherProbe")) || FParse::Param(FCommandLine::Get(),TEXT("CrankTranslationProbe")));
    if(bDefaultLauncher) {
        LauncherDirectory=FCrankGameFiles::PrepareLauncher(LastStartError);
        if(!LauncherDirectory.IsEmpty()) Launcher=MakeShared<FCrankLauncher>();
    }
    Translation=MakeShared<FCrankTranslation>();
    if (FParse::Value(FCommandLine::Get(),TEXT("CrankGame="),Directory)) PlayGame(Directory);
    else if(!bDefaultLauncher) {
        const FString Fixture=FCrankGameFiles::PrepareFixture(LastStartError);
        if (!Fixture.IsEmpty() && !ProbeDirectory.IsEmpty()) PlayGame(Fixture);
    }
}

void ACrankDevicePawn::UpdateCamera(float DeltaTime) {
    auto* PC = Cast<APlayerController>(GetController()); if (!PC) return;
    int W,H; PC->GetViewportSize(W,H); if (W<1 || H<1) return;
    if (LastViewport!=FIntPoint(W,H)) { LastViewport=FIntPoint(W,H); ReleaseInputs(); }
    if(bImmersive) return;
    const float Aspect = float(W)/H;
    Camera->AspectRatio=Aspect;
    // Fit both the unfolded crank and the full body with reserved toolbar space.
    const float Distance = FMath::Max(4.5f,4.0f*Aspect)/FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView*.5f))/.86f;
    // Centre the whole unfolded device, including its handle. Portrait needs
    // a little more room on the crank side than the previous body-only target.
    EnlargedBlend=FMath::Lerp(EnlargedBlend,bEnlarged ? 1.f : 0.f,bReducedMotion || DeltaTime<=0 ? 1.f : CrankView::SmoothAlpha(9,DeltaTime));
    FBox Body(ForceInit);
    for(const TCHAR* Name:{TEXT("main"),TEXT("dpad"),TEXT("A"),TEXT("B"),TEXT("menu"),TEXT("sleep")})
        if(auto* Mesh=Cast<UPrimitiveComponent>(NamedNodes.FindRef(Name))) Body+=Mesh->Bounds.GetBox();
    const FVector NormalTarget(0,Aspect<1.f ? -1.3f : -1.1f,.3);
    const FVector Target=Body.IsValid ? FMath::Lerp(NormalTarget,Body.GetCenter(),EnlargedBlend) : NormalTarget;
    const FVector2D Orbit=ManualOrbit+MotionOrbit;
    const FVector Direction=FRotator(Orbit.Y,Orbit.X,0).RotateVector(FVector(1,-.13f,.05f).GetSafeNormal());
    const float BodyDistance=Body.IsValid ? CrankView::FitDistance(Body,Target,Direction,Aspect,Camera->FieldOfView,.88f) : Distance;
    const float FramingDistance=FMath::Lerp(Distance*FVector(1,-.13f,.05f).Size(),BodyDistance,EnlargedBlend)*ViewZoom;
    const FVector Location=Target+Direction*FramingDistance;
    Camera->SetWorldLocation(Location); Camera->SetWorldRotation((Target-Location).Rotation());
}

bool ACrankDevicePawn::HitScreen(const FVector2D& Position) const {
    FVector2D Pixel; return ScreenPoint(Position,Pixel);
}
bool ACrankDevicePawn::ScreenPoint(const FVector2D& Position,FVector2D& Pixel) const {
    if(bImmersive) return ImmersiveWidget && ImmersiveWidget->ScreenPoint(Position,Pixel);
    auto* PC=Cast<APlayerController>(GetController()); auto* Main=NamedNodes.FindRef(TEXT("main"));
    if(!PC || !Main || !ScreenLocalBounds.IsValid) return false;
    FVector Origin,Direction; if(!PC->DeprojectScreenPositionToWorld(Position.X,Position.Y,Origin,Direction)) return false;
    const FTransform& Transform=Main->GetComponentTransform();
    Origin=Transform.InverseTransformPosition(Origin); Direction=Transform.InverseTransformVector(Direction);
    if(FMath::Abs(Direction.Z)<.0001f) return false;
    const float T=(ScreenLocalBounds.Min.Z-Origin.Z)/Direction.Z; if(T<0) return false;
    const FVector Hit=Origin+Direction*T;
    if(Hit.X<ScreenLocalBounds.Min.X || Hit.X>ScreenLocalBounds.Max.X || Hit.Y<ScreenLocalBounds.Min.Y || Hit.Y>ScreenLocalBounds.Max.Y) return false;
    Pixel={400*(ScreenLocalBounds.Max.Y-Hit.Y)/(ScreenLocalBounds.Max.Y-ScreenLocalBounds.Min.Y),240*(Hit.X-ScreenLocalBounds.Min.X)/(ScreenLocalBounds.Max.X-ScreenLocalBounds.Min.X)};
    return true;
}

FVector2D ACrankDevicePawn::ProjectNode(FName Name) const {
    const auto* PC = Cast<APlayerController>(GetController());
    const auto* C = NamedNodes.Find(Name); FVector2D P(-10000,-10000);
    if (PC && C) PC->ProjectWorldLocationToScreen((*C)->GetComponentLocation(),P);
    return P;
}

FName ACrankDevicePawn::HitControl(const FVector2D& P) const {
    if(bImmersive) return ImmersiveWidget ? ImmersiveWidget->HitControl(P) : NAME_None;
    const float PixelScale = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetDPIScale() : 1;
    float BestDistance = MAX_flt; FName Best;
    for (const TCHAR* Name : {TEXT("A"),TEXT("B"),TEXT("menu"),TEXT("sleep"),TEXT("dpad")}) {
        float Radius=28*PixelScale;
        if (auto* Mesh=Cast<UPrimitiveComponent>(NamedNodes.FindRef(Name))) {
            FVector2D Edge; auto* PC=Cast<APlayerController>(GetController());
            if (PC && PC->ProjectWorldLocationToScreen(Mesh->Bounds.Origin+Camera->GetRightVector()*Mesh->Bounds.BoxExtent.GetMax(),Edge))
                Radius=FMath::Max(Radius,FVector2D::Distance(Edge,ProjectNode(Name)));
        }
        const float D = FVector2D::DistSquared(P,ProjectNode(Name));
        if (D < FMath::Square(Radius) && D < BestDistance) { BestDistance = D; Best = Name; }
    }
    if (Best==TEXT("dpad")) {
        const FVector2D Delta = P-ProjectNode(TEXT("dpad"));
        return FMath::Abs(Delta.X)>FMath::Abs(Delta.Y) ? (Delta.X<0 ? TEXT("left") : TEXT("right")) : (Delta.Y<0 ? TEXT("up") : TEXT("down"));
    }
    if (Best.IsNone() && !bCrankDocked) {
        // The handle's geometry has an offset within its pivot, so use its bounds.
        auto* Handle = Cast<UPrimitiveComponent>(NamedNodes.FindRef(TEXT("handle_and_screw")));
        auto* PC = Cast<APlayerController>(GetController()); FVector2D Center;
        if (Handle && PC && PC->ProjectWorldLocationToScreen(Handle->Bounds.Origin,Center) &&
            FVector2D::DistSquared(P,Center)<FMath::Square(38*PixelScale)) Best=TEXT("crank");
    }
    return Best;
}

void ACrankDevicePawn::UpdatePointer(int Id,const FVector2D& P,bool Down) {
    if (!Down || bOverlayOpen) {
        const FName Control=Pointers.FindRef(Id);
        const float Scale=GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetDPIScale() : 1;
        const bool Tap=!bOverlayOpen && PointerStarts.Contains(Id) &&
            FPlatformTime::Seconds()-PointerStartTimes.FindRef(Id)<.24 && FVector2D::DistSquared(PointerStarts[Id],P)<FMath::Square(10*Scale);
        const bool Zoom=!Down && Control==TEXT("screen") && Tap && ScreenDoubleTap.Tap(FPlatformTime::Seconds(),P,Scale);
        const bool OpenMenu=!Down && !bOverlayOpen && Control==TEXT("menu") && Tap;
        const bool Power=!Down && !bOverlayOpen && Control==TEXT("sleep") && Tap;
        Pointers.Remove(Id); PreviousPointers.Remove(Id); PointerStarts.Remove(Id); PointerStartTimes.Remove(Id); ScreenGestureStarts.Remove(Id);
        if(Control==TEXT("screen") && !Tap) ScreenDoubleTap.Cancel();
        if(Zoom) {PendingScreenTapTime=-1; if(bImmersive) SetImmersive(false); else ToggleEnlarged();}
        else if(Control==TEXT("screen") && Tap && (SystemMode!=ECrankSystemMode::Playing || CurrentGameDirectory==LauncherDirectory)) {
            if(ScreenPoint(P,PendingScreenTap)) PendingScreenTapTime=FPlatformTime::Seconds()+.33;
        }
        if(OpenMenu) ToggleSystemMenu();
        if(Power) ToggleLock();
        return;
    }
    if (!Pointers.Contains(Id)) {
        FName Control=HitControl(P);
        if(Control.IsNone()) {
            if(HitScreen(P)) Control=TEXT("screen");
            else if(bImmersive) return; // Utility widgets and blank space never orbit the hidden mesh.
            else Control=TEXT("view");
        }
        const bool Multi=Pointers.Num()>0;
        if(Multi || Control!=TEXT("screen")) {ScreenDoubleTap.Cancel(); PendingScreenTapTime=-1;}
        if(Multi) {PointerStarts.Empty(); PointerStartTimes.Empty();}
        Pointers.Add(Id,Control); PreviousPointers.Add(Id,P);
        if(bImmersive && Control==TEXT("screen")) ScreenGestureStarts.Add(Id,P);
        if(!Multi) { PointerStarts.Add(Id,P); PointerStartTimes.Add(Id,FPlatformTime::Seconds()); }
        if(Control==TEXT("menu") || Control==TEXT("sleep")) PulseHaptic();
        return;
    }
    FName& Control = Pointers[Id];
    if(bImmersive && Control==TEXT("screen") && !bCrankDocked && ScreenGestureStarts.Contains(Id)) {
        const FVector2D Travel=P-ScreenGestureStarts[Id];
        const float Scale=GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetDPIScale() : 1;
        if(FMath::Abs(Travel.Y)>10*Scale && FMath::Abs(Travel.Y)>FMath::Abs(Travel.X)) {
            Control=TEXT("screen-crank"); ScreenDoubleTap.Cancel(); PendingScreenTapTime=-1;
            TurnCrank(ImmersiveWidget->ScreenCrankDelta(ScreenGestureStarts[Id],P));
        }
    } else if(bImmersive && Control==TEXT("screen-crank")) {
        TurnCrank(ImmersiveWidget->ScreenCrankDelta(PreviousPointers[Id],P));
    } else if (Control==TEXT("crank")) {
        const FVector2D Center=ProjectNode(TEXT("axle"));
        const FVector2D A=PreviousPointers[Id]-Center, B=P-Center;
        if (A.SizeSquared()>64 && B.SizeSquared()>64) {
            const float Old=FMath::RadiansToDegrees(FMath::Atan2(A.Y,A.X));
            const float New=FMath::RadiansToDegrees(FMath::Atan2(B.Y,B.X));
            TurnCrank(FMath::FindDeltaAngleDegrees(Old,New));
        }
    } else if(Control==TEXT("view") && Pointers.Num()==1) {
        const FVector2D Delta=P-PreviousPointers[Id];
        ManualOrbit.X=FMath::Clamp(ManualOrbit.X-Delta.X*.065f,-32.f,32.f);
        ManualOrbit.Y=FMath::Clamp(ManualOrbit.Y+Delta.Y*.065f,-20.f,20.f);
    } else if (Control==TEXT("left") || Control==TEXT("right") || Control==TEXT("up") || Control==TEXT("down") || Control==TEXT("dpad")) {
        const FName NewControl=HitControl(P);
        if (NewControl==TEXT("left") || NewControl==TEXT("right") || NewControl==TEXT("up") || NewControl==TEXT("down")) Control=NewControl;
        else if(bImmersive) Control=TEXT("dpad");
    }
    PreviousPointers[Id]=P;
}

void ACrankDevicePawn::Tick(float DeltaTime) {
    Super::Tick(DeltaTime); UpdateSystem(DeltaTime); UpdateMotion(DeltaTime); UpdateCamera(DeltaTime);
    UpdateLauncher();
    if(!ProbeDirectory.IsEmpty() && FParse::Param(FCommandLine::Get(),TEXT("CrankLauncherFlowProbe"))) {
        if(LauncherProbePhase==0 && LauncherDetailId==1058 && ProbeElapsed>LauncherInspectElapsed+.7f && Runtime->IsRunning() && SystemMode==ECrankSystemMode::Playing) {
            Runtime->ActivateLauncher();LauncherProbePhase=1;
        } else if(LauncherProbePhase==1 && CurrentGameDirectory!=LauncherDirectory && Runtime->IsRunning() && Runtime->GetFrameNumber()>30) {
            ReturnHome();LauncherProbePhase=2;
        } else if(LauncherProbePhase==2 && CurrentGameDirectory==LauncherDirectory && LauncherDetailId==1058 && Runtime->IsRunning() && Runtime->GetFrameNumber()>30) {
            bLauncherProbePassed=true;LauncherProbePhase=3;LauncherProbeCompletedAt=ProbeElapsed;
        }
    }
    if (bImportBusy && (ImportPollElapsed+=DeltaTime)>.2f) {
        ImportPollElapsed=0;
        const auto Result=FCrankGameFiles::PollImport();
        if (!Result.Message.IsEmpty()) ImportMessage=Result.Message;
        if (Result.State>=3) {
            bImportBusy=false;
            bCanRetryImport=Result.CanRetry;
            if (Result.State==4) { LastStartError=Result.Message; ImportMessage.Empty(); }
            if(Result.State==3) {LastStartError.Empty(); GameCovers.Empty();}
            if (Overlay) Overlay->RefreshGames();
        }
    }
    auto* PC=Cast<APlayerController>(GetController()); if (!PC) return;
    if(!ProbeDirectory.IsEmpty() && FParse::Param(FCommandLine::Get(),TEXT("CrankSystemFlowProbe"))) UpdateSystemFlowProbe();
    const bool Active=(!Runtime->IsSuspended() || SystemMode!=ECrankSystemMode::Playing) && (!ProbeDirectory.IsEmpty() || (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive()));
    if (!Active && (Pointers.Num() || LastButtons || CrankHolds || CrankVelocity!=0)) ReleaseInputs();
    float X,Y; if (PC->GetMousePosition(X,Y)) UpdatePointer(100,{X,Y},Active && PC->IsInputKeyDown(EKeys::LeftMouseButton));
    for (int I=0;I<10;I++) {
        bool Down; PC->GetInputTouchState(ETouchIndex::Type(I),X,Y,Down); UpdatePointer(I,{X,Y},Active && Down);
    }
    TArray<FVector2D> ViewPointers;
    for(const auto& Pointer:Pointers) if(Pointer.Value==TEXT("view")) ViewPointers.Add(PreviousPointers[Pointer.Key]);
    if(ViewPointers.Num()==2) {
        const float Separation=FVector2D::Distance(ViewPointers[0],ViewPointers[1]);
        if(PinchDistance>20 && Separation>20) ViewZoom=FMath::Clamp(ViewZoom*PinchDistance/Separation,.7f,1.5f);
        PinchDistance=Separation;
    } else PinchDistance=0;
    if (!ProbeDirectory.IsEmpty()) {
        ProbeElapsed+=FMath::Min(DeltaTime,.1f);
        if(FParse::Param(FCommandLine::Get(),TEXT("CrankImmersiveProbe"))) UpdateImmersiveProbe();
        if(!bStageTwoProbeRan && ProbeElapsed>.7f && FParse::Param(FCommandLine::Get(),TEXT("CrankStageTwoProbe"))) {
            bStageTwoProbeRan=true;
            FVector2D ScreenPoint; PC->ProjectWorldLocationToScreen(NamedNodes[TEXT("main")]->GetComponentTransform().TransformPosition(ScreenLocalBounds.GetCenter()),ScreenPoint);
            const bool WasEnlarged=bEnlarged; const uint32 Frame=Runtime->GetFrameNumber();
            const FString Game=CurrentGameDirectory;
            const bool ScreenHit=HitScreen(ScreenPoint);
            for(int I=0;I<2;I++) { UpdatePointer(902,ScreenPoint,true); UpdatePointer(902,ScreenPoint,false); }
            const bool Entered=bEnlarged!=WasEnlarged && Runtime->GetFrameNumber()==Frame && CurrentGameDirectory==Game;
            for(int I=0;I<2;I++) { UpdatePointer(902,ScreenPoint,true); UpdatePointer(902,ScreenPoint,false); }
            const bool Restored=bEnlarged==WasEnlarged;
            UpdatePointer(902,ScreenPoint,true); UpdatePointer(903,ScreenPoint,true);
            UpdatePointer(902,ScreenPoint,false); UpdatePointer(903,ScreenPoint,false);
            const bool MultiCancelled=ScreenDoubleTap.Time<0 && bEnlarged==WasEnlarged;
            UpdatePointer(902,ScreenPoint,true); UpdatePointer(902,ScreenPoint+FVector2D(100,0),true); UpdatePointer(902,ScreenPoint+FVector2D(100,0),false);
            const bool DragCancelled=ScreenDoubleTap.Time<0 && bEnlarged==WasEnlarged;
            SetMotionFollow(false); SetHaptics(false); SetMotionSensitivity(.65f); SetHapticStrength(.4f); SetReducedMotion(true); SetFrameRate(30); SetResolutionScale(.75f);
            bMotionFollow=bHaptics=true; MotionSensitivity=.35f; HapticStrength=.6f; bReducedMotion=false;
            FrameRate=60; ResolutionScale=1;
            LoadExperience();
            const bool Saved=!bMotionFollow && !bHaptics && FMath::IsNearlyEqual(MotionSensitivity,.65f) && FMath::IsNearlyEqual(HapticStrength,.4f) && bReducedMotion && FrameRate==30 && FMath::IsNearlyEqual(ResolutionScale,.75f) && ExperienceError.IsEmpty();
            bStageTwoProbePassed=ScreenHit && Entered && Restored && MultiCancelled && DragCancelled && Saved;
            UE_LOG(LogTemp,Display,TEXT("Crank stage two: screen=%d enter=%d restore=%d multi=%d drag=%d settings=%d"),ScreenHit,Entered,Restored,MultiCancelled,DragCancelled,Saved);
        }
        HoldCrank(1,!FParse::Param(FCommandLine::Get(),TEXT("CrankTranslationProbe")) && ProbeElapsed>1 && ProbeElapsed<3);
        const FVector2D ProbeA=bImmersive && ImmersiveWidget ? ImmersiveWidget->ViewportPoint(CrankImmersive::FLayout::Center(ImmersiveWidget->GetLayout().A)) : ProjectNode(TEXT("A"));
        UpdatePointer(201,ProbeA,!FParse::Param(FCommandLine::Get(),TEXT("CrankTranslationProbe")) && ProbeElapsed>2 && ProbeElapsed<2.3);
    }
    int Buttons=0;
    if(!ProbeDirectory.IsEmpty() && FParse::Param(FCommandLine::Get(),TEXT("CrankTranslationProbe")) && bTranslationProbeDone && ProbeElapsed<TranslationProbeCompletedAt+.8f)Buttons|=8;
    const TPair<FKey,int> Keys[]={{EKeys::Left,1},{EKeys::Right,2},{EKeys::Up,4},{EKeys::Down,8},{EKeys::X,16},{EKeys::Z,32}};
    if (Active && !bOverlayOpen) for (auto K:Keys) if (PC->IsInputKeyDown(K.Key)) Buttons|=K.Value;
    const TMap<FName,int> Bits={{TEXT("left"),1},{TEXT("right"),2},{TEXT("up"),4},{TEXT("down"),8},{TEXT("B"),16},{TEXT("A"),32}};
    for (auto P:Pointers) if (const auto* Bit=Bits.Find(P.Value)) Buttons|=*Bit;
    if(Buttons & ~LastButtons) PulseHaptic();
    const int PressedButtons=Buttons & ~LastButtons;
    const bool WasSystem=SystemMode!=ECrankSystemMode::Playing;
    if(Active && !bOverlayOpen) {
        if(PC->WasInputKeyJustPressed(EKeys::M)) ToggleSystemMenu();
        if(PC->WasInputKeyJustPressed(EKeys::P)) ToggleLock();
        if(SystemMode==ECrankSystemMode::Home || SystemMode==ECrankSystemMode::Menu) {
            if(PressedButtons&4) SystemNavigate(-1); if(PressedButtons&8) SystemNavigate(1);
            if(PressedButtons&1) SystemActivate(-1); if(PressedButtons&2) SystemActivate(1); if(PressedButtons&32) SystemActivate();
            if(PressedButtons&16) {if(SystemMode==ECrankSystemMode::Menu) CloseSystemMenu();}
        } else if(SystemMode==ECrankSystemMode::Boot && PressedButtons) {SystemMode=ECrankSystemMode::Home; ApplySystemPause();}
        else if(SystemMode==ECrankSystemMode::Locked && PressedButtons) ToggleLock();
    }
    if(WasSystem) SuppressedButtons|=Buttons;
    SuppressedButtons&=Buttons;
    Runtime->SetButtons(SystemMode==ECrankSystemMode::Playing ? Buttons & ~SuppressedButtons : 0); LastButtons=Buttons;
    if(!ProbeDirectory.IsEmpty() && FParse::Param(FCommandLine::Get(),TEXT("CrankImmersiveProbe"))) UpdateImmersiveTouchProbe();
    if (!ProbeDirectory.IsEmpty()) {
        if (ProbeElapsed>2 && ProbeElapsed<2.3 && (Buttons&32)) bProbePressed=true;
        if (ProbeElapsed>2.4 && !(Buttons&32)) bProbeReleased=true;
    }
    for (auto Pair:NamedNodes) {
        if (!RestTransforms.Contains(Pair.Key)) continue;
        int Mask=0;
        if (Pair.Key==TEXT("A")) Mask=32; if (Pair.Key==TEXT("B")) Mask=16; if (Pair.Key==TEXT("dpad")) Mask=15;
        const bool SmallButton=Pair.Key==TEXT("menu") || Pair.Key==TEXT("sleep");
        if (!Mask && !SmallButton) continue;
        const FVector Rest=RestTransforms[Pair.Key].GetLocation();
        bool Pressed=(Buttons&Mask)!=0;
        if (SmallButton) for (const auto& P:Pointers) if (P.Value==Pair.Key) Pressed=true;
        const FVector Offset=Pair.Key==TEXT("sleep") ? FVector(.032,0,0) : FVector(0,0,-.072);
        const FVector Target=Rest+(Pressed ? Offset : FVector::ZeroVector);
        Pair.Value->SetRelativeLocation(FMath::VInterpTo(Pair.Value->GetRelativeLocation(),Target,DeltaTime,32));
    }
    const float Dt=FMath::Min(DeltaTime,.05f);
    int Direction=(CrankHolds&2 ? 1 : 0)-(CrankHolds&1 ? 1 : 0);
    if (Active && !bOverlayOpen && !bCrankDocked) Direction+=int(PC->IsInputKeyDown(EKeys::E))-int(PC->IsInputKeyDown(EKeys::Q));
    if(bCrankDocked) Direction=0;
    CrankVelocity=FMath::Lerp(CrankVelocity,FMath::Clamp(Direction,-1,1)*300.f,1-FMath::Exp(-12*Dt));
    CrankTarget+=CrankVelocity*Dt;
    const float Step=(CrankTarget-CrankAngle)*(1-FMath::Exp(-22*Dt)); CrankAngle+=Step;
    if (FMath::Abs(Step)>.0001f) {
        if(SystemMode==ECrankSystemMode::Playing) Runtime->AddCrankDelta(Step,bCrankDocked);
        else if(SystemMode==ECrankSystemMode::Home || SystemMode==ECrankSystemMode::Menu) {
            SystemCrankTravel+=Step;
            if(FMath::Abs(SystemCrankTravel)>=24) {SystemNavigate(SystemCrankTravel>0 ? 1 : -1); SystemCrankTravel=FMath::Fmod(SystemCrankTravel,24.f);}
        }
        CrankHapticTravel+=FMath::Abs(Step);
        if(CrankHapticTravel>=18) { PulseHaptic(true); CrankHapticTravel=FMath::Fmod(CrankHapticTravel,18.f); }
    }
    float VisualAngle=CrankAngle;
    if (!ProbeDirectory.IsEmpty()) FParse::Value(FCommandLine::Get(),TEXT("CrankViewAngle="),VisualAngle);
    ApplyCrankRotation(VisualAngle);
    if(ScreenMaterial) {
        if(SystemMode!=ECrankSystemMode::Playing && SystemTexture) ScreenMaterial->SetTextureParameterValue(TEXT("GameScreen"),SystemTexture);
        else if(Runtime->ScreenTexture) ScreenMaterial->SetTextureParameterValue(TEXT("GameScreen"),Runtime->ScreenTexture);
    }
    // Development telemetry observes real OS input without injecting pointers.
    if (!InputProbeDirectory.IsEmpty() && (InputProbeElapsed+=DeltaTime)>.25f) {
        InputProbeElapsed=0;
        auto Result=MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("buttons"),LastButtons);
        Result->SetNumberField(TEXT("frames"),Runtime->GetFrameNumber());
        Result->SetNumberField(TEXT("audioSamples"),Runtime->AudioSamplesReceived);
        Result->SetNumberField(TEXT("crankDegrees"),CrankAngle);
        Result->SetBoolField(TEXT("enlarged"),bEnlarged);
        Result->SetBoolField(TEXT("immersive"),bImmersive);
        Result->SetNumberField(TEXT("enlargedBlend"),EnlargedBlend);
        Result->SetBoolField(TEXT("motionFollow"),bMotionFollow);
        Result->SetBoolField(TEXT("motionSample"),bHasMotionSample);
        Result->SetBoolField(TEXT("haptics"),bHaptics);
        Result->SetBoolField(TEXT("crankDocked"),bCrankDocked);
        Result->SetBoolField(TEXT("suspended"),Runtime->IsSuspended());
        Result->SetBoolField(TEXT("slateActive"),FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive());
        Result->SetBoolField(TEXT("zDown"),PC->IsInputKeyDown(EKeys::Z));
        Result->SetNumberField(TEXT("pointers"),Pointers.Num());
        Result->SetNumberField(TEXT("width"),LastViewport.X); Result->SetNumberField(TEXT("height"),LastViewport.Y);
        auto Controls=MakeShared<FJsonObject>();
        for (const TCHAR* Name:{TEXT("A"),TEXT("B"),TEXT("dpad"),TEXT("menu"),TEXT("axle")}) {
            const FVector2D Point=ProjectNode(Name);
            TArray<TSharedPtr<FJsonValue>> XY={MakeShared<FJsonValueNumber>(Point.X),MakeShared<FJsonValueNumber>(Point.Y)};
            Controls->SetArrayField(Name,XY);
        }
        Result->SetObjectField(TEXT("controls"),Controls);
        FString Json; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json,*(InputProbeDirectory/TEXT("input.json")));
    }
    if (!ProbeDirectory.IsEmpty() && ProbeElapsed>3.5f && !bOverlayOpen && FParse::Param(FCommandLine::Get(),TEXT("CrankSettingsProbe"))) Overlay->ShowSettings(FParse::Param(FCommandLine::Get(),TEXT("CrankModelSettingsProbe")));
    if(!ProbeDirectory.IsEmpty() && ProbeElapsed>3.5f && ProbeElapsed<3.7f) {
        if(FParse::Param(FCommandLine::Get(),TEXT("CrankSystemMenuProbe")) && SystemMode==ECrankSystemMode::Playing) ToggleSystemMenu();
        if(FParse::Param(FCommandLine::Get(),TEXT("CrankHomeProbe")) && SystemMode==ECrankSystemMode::Playing) ReturnHome();
        if(FParse::Param(FCommandLine::Get(),TEXT("CrankLockProbe")) && SystemMode==ECrankSystemMode::Playing) ToggleLock();
    }
    if (!ProbeDirectory.IsEmpty() && ProbeElapsed>3.5f && !bOverlayOpen && FParse::Param(FCommandLine::Get(),TEXT("CrankUIProbe"))) {
        Overlay->ShowLibrary(); PlayGame(FPaths::ProjectSavedDir()/TEXT("missing-scene-test.pdx"));
    }
    const bool TranslationProbe=FParse::Param(FCommandLine::Get(),TEXT("CrankTranslationProbe"));
    const bool OriginalProbe=FParse::Param(FCommandLine::Get(),TEXT("CrankTranslationOriginalProbe"));
    if(TranslationProbe && Translation && ProbeElapsed>3.3f && LauncherDetailId==1058 && !bTranslationProbeOpened) {
        if(Translation->GetId()==1058) {bTranslationProbeOpened=bTranslationEntryPassed=true;}
        else if(!Runtime->IsSuspended()) ScreenTap({150,224});
    }
    if(TranslationProbe && bTranslationProbeOpened && !bTranslationProbeDone && !Translation->IsBusy()) {
        if(OriginalProbe && bInlineChinese && !bTranslationOriginalTapped) {ScreenTap({150,224});bTranslationOriginalTapped=true;}
        const bool Applied=Translation->HasModel() ? (OriginalProbe?bInlineOriginal:bInlineChinese) : !Translation->GetSource().IsEmpty();
        if(Applied) {
            bTranslationOriginalPassed=OriginalProbe && bInlineOriginal;
            bTranslationProbeDone=true;TranslationProbeCompletedAt=ProbeElapsed;
        }
    }
    const bool TranslationReady=!TranslationProbe || (bTranslationProbeDone && ProbeElapsed>TranslationProbeCompletedAt+1.2f);
    const bool LauncherProbeReady=TranslationReady && (!FParse::Param(FCommandLine::Get(),TEXT("CrankLauncherFlowProbe")) || (bLauncherProbePassed && ProbeElapsed>LauncherProbeCompletedAt+.5f));
    if (!ProbeDirectory.IsEmpty() && ProbeElapsed>4 && Runtime->GetFrameNumber()>30 && !bProbeScreenshot && LauncherProbeReady && (!FParse::Param(FCommandLine::Get(),TEXT("CrankSystemFlowProbe")) || (bSystemFlowPassed && ProbeElapsed>SystemFlowCompletedAt+.5f))) {
        FScreenshotRequest::RequestScreenshot(ProbeDirectory/TEXT("device.png"),true,false);
        bProbeScreenshot=true;
    }
    if (!ProbeDirectory.IsEmpty() && bProbeScreenshot && ProbeElapsed>6 && FPaths::FileExists(ProbeDirectory/TEXT("device.png"))) {
        const bool UIProbe=FParse::Param(FCommandLine::Get(),TEXT("CrankUIProbe"));
        const bool ExpectedState=UIProbe ? (HasError() && bOverlayOpen && LastButtons==0) : LastStartError.IsEmpty();
        bool FitsViewport=true; int ViewWidth,ViewHeight; PC->GetViewportSize(ViewWidth,ViewHeight);
        double AttachmentError=0;
        auto* Grip=NamedNodes.FindRef(TEXT("handle_and_screw"));
        auto* GripPivot=NamedNodes.FindRef(TEXT("handle_pivot"));
        for (int Angle=0;Angle<360;Angle+=15) {
            ApplyCrankRotation(Angle);
            // The source mesh origin is offset from its physical attachment.
            // This invariant detects rotating the mesh around the wrong origin.
            const FVector Anchor=Grip->GetComponentTransform().TransformPosition(-RestTransforms[TEXT("handle_and_screw")].GetLocation());
            AttachmentError=FMath::Max(AttachmentError,FVector::Distance(Anchor,GripPivot->GetComponentLocation()));
            for (const auto& Node:Nodes) if (auto* Mesh=Cast<UPrimitiveComponent>(Node.Get())) {
                for (int Corner=0;Corner<8;Corner++) {
                    const FVector Sign(Corner&1 ? 1:-1,Corner&2 ? 1:-1,Corner&4 ? 1:-1);
                    FVector2D Projected;
                    if (!PC->ProjectWorldLocationToScreen(Mesh->Bounds.Origin+Sign*Mesh->Bounds.BoxExtent,Projected) ||
                        Projected.X<20 || Projected.X>ViewWidth-20 || Projected.Y<20 || Projected.Y>ViewHeight-20)
                        if(!bEnlarged || (Node->GetFName()!=TEXT("axle") && Node->GetFName()!=TEXT("arm") && Node->GetFName()!=TEXT("handle_and_screw"))) FitsViewport=false;
                }
            }
        }
        ApplyCrankRotation(VisualAngle);
        const bool Passed=Nodes.Num()==14 && (bImmersive || FitsViewport) && AttachmentError<.001 && Runtime->GetFrameNumber()>30 && (TranslationProbe || (CrankAngle>100 && bProbePressed && bProbeReleased)) && ExpectedState && Runtime->LastError.IsEmpty() && (!bStageTwoProbeRan || bStageTwoProbePassed) && (!FParse::Param(FCommandLine::Get(),TEXT("CrankImmersiveProbe")) || (bImmersiveProbePassed && bImmersiveTouchProbePassed)) && (!FParse::Param(FCommandLine::Get(),TEXT("CrankSystemFlowProbe")) || bSystemFlowPassed);
        auto Result=MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("launcherFlowPassed"),bLauncherProbePassed);
        if(TranslationProbe && Translation){Result->SetBoolField(TEXT("translationEntryPassed"),bTranslationEntryPassed);Result->SetBoolField(TEXT("translationOriginalPassed"),bTranslationOriginalPassed);Result->SetBoolField(TEXT("inlineChineseApplied"),bInlineChinese);Result->SetBoolField(TEXT("dialogAbsent"),!bOverlayOpen);Result->SetStringField(TEXT("translationState"),Translation->GetState());Result->SetStringField(TEXT("translated"),Translation->GetTranslated());Result->SetBoolField(TEXT("translationPassed"),!bOverlayOpen && !Translation->HasError() && !Translation->GetSource().IsEmpty() && (!Translation->HasModel() || !Translation->GetTranslated().IsEmpty()) && bTranslationEntryPassed && (!OriginalProbe || bTranslationOriginalPassed));}
        Result->SetBoolField(TEXT("immersive"),bImmersive);
        Result->SetBoolField(TEXT("immersiveInputPassed"),bImmersiveProbePassed);
        Result->SetBoolField(TEXT("immersiveTouchProbeRan"),bImmersiveTouchProbeRan);
        Result->SetBoolField(TEXT("immersiveTouchPassed"),bImmersiveTouchProbePassed);
        Result->SetBoolField(TEXT("immersiveDialPassed"),bImmersiveDialPassed);
        Result->SetBoolField(TEXT("immersiveScreenSwipePassed"),bImmersiveSwipePassed);
        Result->SetBoolField(TEXT("immersiveSwipeMultiPassed"),bImmersiveSwipeMultiPassed);
        if(bImmersive && ImmersiveWidget) {
            const auto& R=ImmersiveWidget->GetLayout().Screen;
            const FVector2D Start=ImmersiveWidget->ViewportPoint({R.Left,R.Top}), End=ImmersiveWidget->ViewportPoint({R.Right,R.Bottom});
            Result->SetArrayField(TEXT("screenRect"),{MakeShared<FJsonValueNumber>(Start.X),MakeShared<FJsonValueNumber>(Start.Y),MakeShared<FJsonValueNumber>(End.X-Start.X),MakeShared<FJsonValueNumber>(End.Y-Start.Y)});
            bool Hidden=true; TInlineComponentArray<UPrimitiveComponent*> Meshes; GetComponents(Meshes);
            for(auto* Mesh:Meshes) Hidden&=!Mesh->IsVisible();
            Result->SetBoolField(TEXT("modelHidden"),Hidden);
        }
        Result->SetBoolField(TEXT("passed"),Passed); Result->SetNumberField(TEXT("nodes"),Nodes.Num());
        Result->SetBoolField(TEXT("stageTwoProbeRan"),bStageTwoProbeRan); Result->SetBoolField(TEXT("stageTwoProbePassed"),bStageTwoProbePassed);
        Result->SetBoolField(TEXT("systemFlowProbePassed"),bSystemFlowPassed);
        Result->SetNumberField(TEXT("frames"),Runtime->GetFrameNumber()); Result->SetNumberField(TEXT("crankDegrees"),CrankAngle);
        Result->SetStringField(TEXT("executionMode"),Runtime->ExecutionMode);
        Result->SetNumberField(TEXT("nativeArmCalls"),Runtime->NativeArmCalls);
        Result->SetNumberField(TEXT("nativeApiCalls"),Runtime->NativeApiCalls);
        Result->SetBoolField(TEXT("pointerPressedA"),bProbePressed); Result->SetBoolField(TEXT("pointerReleasedA"),bProbeReleased);
        Result->SetBoolField(TEXT("deviceFitsViewport"),FitsViewport);
        Result->SetNumberField(TEXT("crankSweepAngles"),24);
        Result->SetNumberField(TEXT("crankAttachmentErrorCm"),AttachmentError);
        Result->SetNumberField(TEXT("visualCrankDegrees"),VisualAngle);
        Result->SetStringField(TEXT("runtimeError"),Runtime->LastError); Result->SetStringField(TEXT("startError"),LastStartError);
        Result->SetBoolField(TEXT("intentionalInvalidPath"),UIProbe);
        FString Json; FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json,*(ProbeDirectory/TEXT("results.json")));
        UE_LOG(LogTemp,Display,TEXT("Crank scene verification: %s"),Passed ? TEXT("passed") : TEXT("failed"));
        FPlatformMisc::RequestExitWithStatus(false,Passed ? 0 : 1);
    }
}

void ACrankDevicePawn::ReleaseInputs() {
    Pointers.Empty(); PreviousPointers.Empty(); PointerStarts.Empty(); PointerStartTimes.Empty(); ScreenGestureStarts.Empty(); ScreenDoubleTap.Cancel(); PinchDistance=0;
    CrankHolds=0; CrankVelocity=0; CrankTarget=CrankAngle; LastButtons=0; CrankHapticTravel=0;
    PendingScreenTapTime=-1;
    CalibrateMotion();
    if (auto* PC=Cast<APlayerController>(GetController())) PC->FlushPressedKeys();
    FCrankHaptics::Cancel();
    Runtime->SetButtons(0);
}
void ACrankDevicePawn::SetOverlayOpen(bool Open) { if(Open && Translation)Translation->Cancel(); ReleaseInputs(); bOverlayOpen=Open; ApplySystemPause(); }
void ACrankDevicePawn::HoldCrank(int Direction,bool Pressed) { const int Bit=Direction<0 ? 1 : 2; if (Pressed) CrankHolds|=Bit; else CrankHolds&=~Bit; }
FString ACrankDevicePawn::GetStatusText() const {
    if (!LastStartError.IsEmpty()) return LastStartError;
    if (!Runtime->LastError.IsEmpty()) return TEXT("游戏运行遇到错误。请重新启动或选择其他游戏。");
    if(Launcher && CurrentGameDirectory==LauncherDirectory && !Launcher->Message().IsEmpty()) return Launcher->Message();
    return GameTitle;
}
bool ACrankDevicePawn::IsImportBusy() const {return bImportBusy || (Launcher && Launcher->IsBusy());}
bool ACrankDevicePawn::HasError() const { return !LastStartError.IsEmpty() || !Runtime->LastError.IsEmpty() || (CurrentGameDirectory==LauncherDirectory && Launcher && Launcher->HasError()); }
FString ACrankDevicePawn::GetLauncherMessage() const {return Launcher && CurrentGameDirectory==LauncherDirectory ? Launcher->Message() : FString();}
FString ACrankDevicePawn::GetImportMessage() const {return Launcher && (Launcher->IsBusy() || Launcher->HasError()) ? Launcher->Message() : ImportMessage;}
bool ACrankDevicePawn::CanRetryImport() const {return bCanRetryImport || (Launcher && Launcher->HasError());}
bool ACrankDevicePawn::IsLauncherDownloadBusy() const {return Launcher && Launcher->IsBusy();}
void ACrankDevicePawn::CancelLauncherDownload() {if(Launcher) Launcher->Cancel();}
void ACrankDevicePawn::BeginGameImport(bool Folder) {
    if (IsImportBusy()) return;
    ReleaseInputs(); LastStartError.Empty(); ImportMessage.Empty(); bCanRetryImport=false;
    bImportBusy=FCrankGameFiles::BeginImport(Folder); ImportPollElapsed=0;
    if (bImportBusy) ImportMessage=TEXT("正在打开文件选择器…");
    else LastStartError=TEXT("无法打开文件选择器，请稍后重试。");
}
void ACrankDevicePawn::BeginUrlGameImport(const FString& Url) {
    if(IsImportBusy()) return;
    ReleaseInputs(); LastStartError.Empty(); ImportMessage.Empty(); bCanRetryImport=false;
    bImportBusy=FCrankGameFiles::BeginUrlImport(Url.TrimStartAndEnd()); ImportPollElapsed=0;
    if(bImportBusy) ImportMessage=TEXT("正在连接下载链接…");
    else {const auto Result=FCrankGameFiles::PollImport(); LastStartError=Result.Message.IsEmpty() ? TEXT("无法开始下载，请检查 ZIP 游戏直链。"):Result.Message; bCanRetryImport=Result.CanRetry;}
}
void ACrankDevicePawn::RetryGameImport() {
    if(Launcher && Launcher->HasError() && !IsImportBusy()) {Launcher->Play(Launcher->GetSelectedId());return;}
    if(IsImportBusy() || !bCanRetryImport) return;
    bImportBusy=FCrankGameFiles::RetryImport(); ImportPollElapsed=0;
    if(bImportBusy) {LastStartError.Empty(); ImportMessage=TEXT("正在重试…"); bCanRetryImport=false;}
}
FString ACrankDevicePawn::GetTitleForGame(const FString& Directory) const { return FCrankGameFiles::Title(Directory); }
bool ACrankDevicePawn::PlayGame(const FString& Directory) {
    if (IsImportBusy() || Runtime->IsSwitching()) return false;
    FString Normal=FPaths::ConvertRelativePathToFull(Directory); FPaths::NormalizeDirectoryName(Normal);
    if(Normal==CurrentGameDirectory && Runtime->IsRunning() && Runtime->LastError.IsEmpty()) {
        if(SystemMode==ECrankSystemMode::Menu) CloseSystemMenu();
        SystemMode=ECrankSystemMode::Playing; ApplySystemPause(); return true;
    }
    FString Info;
    if (!FFileHelper::LoadFileToString(Info,*(Directory/TEXT("pdxinfo")))) { LastStartError=TEXT("未找到游戏。请选择包含 pdxinfo 的 PDX 文件夹。"); return false; }
    if (!FPaths::FileExists(Directory/TEXT("main.pdz")) && !FPaths::FileExists(Directory/TEXT("main.lua")) && !FPaths::FileExists(Directory/TEXT("main.luac")) && !FPaths::FileExists(Directory/TEXT("pdex.bin"))) {
        LastStartError=TEXT("这个游戏缺少 Lua 入口或 pdex.bin，请选择完整 PDX。"); return false;
    }
    const FString Save=FCrankGameFiles::SaveDirectory(Directory);
    if (Save.IsEmpty()) { LastStartError=TEXT("无法访问游戏存档目录。"); return false; }
    ReleaseInputs(); Runtime->SetSystemMenuOpen(false);
    if (!Runtime->RequestGame(Directory,Save,Normal==LauncherDirectory)) { LastStartError=TEXT("游戏未能启动，请检查目录后重试。"); return false; }
    LauncherSession=0;LauncherDetailId=0;LastLauncherReply.Empty();bInlineChinese=bInlineOriginal=false;if(Translation){Translation->Cancel();Translation->ReleaseModel();}
    SystemMode=ECrankSystemMode::Loading; bLaunchRecorded=false; SystemElapsed=0; ApplySystemPause();
    LastStartError.Empty(); ImportMessage.Empty(); GameTitle=FPaths::GetBaseFilename(Directory);
    CurrentGameDirectory=Normal;
    Runtime->AddCrankDelta(0,bCrankDocked);
    TArray<FString> Lines; Info.ParseIntoArrayLines(Lines);
    for (const FString& Line:Lines) if (Line.StartsWith(TEXT("name="))) GameTitle=Line.Mid(5).TrimStartAndEnd();
    UE_LOG(LogTemp,Display,TEXT("Crank playing: %s"),*GameTitle); return true;
}
TArray<FString> ACrankDevicePawn::GetGameDirectories() const {
    TArray<FString> Result;
    const FString Storage=FCrankGameFiles::Root(); if (Storage.IsEmpty()) return Result;
    for (const FString& Folder:{Storage/TEXT("Games"),FPaths::ProjectSavedDir()/TEXT("Games")}) {
        TArray<FString> Names; IFileManager::Get().FindFiles(Names,*(Folder/TEXT("*.pdx")),false,true);
        for (const auto& Name:Names) {
            const FString Directory=Folder/Name;
            if ((Name.StartsWith(TEXT("fixture_")) || Name.StartsWith(TEXT("launcher_"))) && !FPaths::FileExists(Directory/TEXT(".ready"))) continue;
            if (FPaths::FileExists(Directory/TEXT("pdxinfo"))) Result.Add(Directory);
        }
    }
    for(const auto& Path:RecentGames) if(FPaths::FileExists(Path/TEXT("pdxinfo"))) Result.AddUnique(Path);
    if(!CurrentGameDirectory.IsEmpty() && FPaths::FileExists(CurrentGameDirectory/TEXT("pdxinfo"))) Result.AddUnique(CurrentGameDirectory);
    Result.Sort([this](const FString& A,const FString& B) {
        const int RA=RecentGames.IndexOfByKey(A), RB=RecentGames.IndexOfByKey(B);
        if(RA!=RB) return (RA<0 ? MAX_int32 : RA)<(RB<0 ? MAX_int32 : RB);
        return GetTitleForGame(A)<GetTitleForGame(B);
    });
    return Result;
}
void ACrankDevicePawn::EndPlay(const EEndPlayReason::Type Reason) {
    if(Translation){Translation->Shutdown();Translation.Reset();}
    if(Launcher) {Launcher->Shutdown();Launcher.Reset();}
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
    FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivateHandle);
    if (Overlay && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    if (ImmersiveWidget && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(ImmersiveWidget.ToSharedRef());
    ImmersiveWidget.Reset();
    Overlay.Reset(); ReleaseInputs(); FPlatformApplicationMisc::EnableMotionData(false);
    SystemWidget.Reset(); if(SystemRenderer) {BeginCleanup(SystemRenderer); SystemRenderer=nullptr;}
    Super::EndPlay(Reason);
}

void ACrankDevicePawn::ToggleEnlarged() { ReleaseInputs(); bEnlarged=!bEnlarged; PulseHaptic(); }
void ACrankDevicePawn::SetImmersive(bool Enabled) {
    if(bImmersive==Enabled) return;
    ReleaseInputs(); bImmersive=Enabled;
    TInlineComponentArray<UPrimitiveComponent*> Meshes; GetComponents(Meshes);
    for(auto* Mesh:Meshes) Mesh->SetVisibility(!Enabled);
    if(!Enabled) UpdateCamera();
    PulseHaptic(); SaveExperience();
}
bool ACrankDevicePawn::IsControlPressed(FName Control) const {
    static const TMap<FName,int32> Bits={{TEXT("left"),1},{TEXT("right"),2},{TEXT("up"),4},{TEXT("down"),8},{TEXT("B"),16},{TEXT("A"),32}};
    if(const int32* Bit=Bits.Find(Control)) return (LastButtons & *Bit)!=0;
    return false;
}
UTexture* ACrankDevicePawn::GetDisplayTexture() const {
    return SystemMode==ECrankSystemMode::Playing ? static_cast<UTexture*>(Runtime->ScreenTexture.Get()) : static_cast<UTexture*>(SystemTexture.Get());
}
void ACrankDevicePawn::UpdateImmersiveProbe() {
    if(!bImmersive || !ImmersiveWidget || !Runtime->IsRunning() || SystemMode!=ECrankSystemMode::Playing || ProbeElapsed<.75f || ImmersiveProbePhase==5) return;
    const auto& L=ImmersiveWidget->GetLayout();
    if(L.Screen.Right-L.Screen.Left<100) return;
    const FVector2D A=ImmersiveWidget->ViewportPoint(CrankImmersive::FLayout::Center(L.A));
    const FVector2D B=ImmersiveWidget->ViewportPoint(CrankImmersive::FLayout::Center(L.B));
    const FVector2D Pad=CrankImmersive::FLayout::Center(L.Dpad);
    const float Cell=(L.Dpad.Right-L.Dpad.Left)/3;
    const FVector2D Left=ImmersiveWidget->ViewportPoint(Pad-FVector2D(Cell,0)), Right=ImmersiveWidget->ViewportPoint(Pad+FVector2D(Cell,0));
    if(ImmersiveProbePhase==0) {
        bImmersiveProbeRan=true;
        UpdatePointer(911,A,true);
    } else if(ImmersiveProbePhase==1) {
        bImmersiveHeldA=LastButtons==32;
        UpdatePointer(912,B,true); UpdatePointer(913,Left,true);
    } else if(ImmersiveProbePhase==2) {
        bImmersiveHeldBoth=LastButtons==(32|16|1);
        UpdatePointer(913,Right,true);
    } else if(ImmersiveProbePhase==3) {
        bImmersiveHeldBoth&=LastButtons==(32|16|2);
        UpdatePointer(912,B,false); UpdatePointer(913,Right,false);
    } else if(ImmersiveProbePhase==4) {
        bImmersiveReleasedB=LastButtons==32;
        const FString Game=CurrentGameDirectory; UTexture* Texture=GetDisplayTexture();
        const uint32 Frame=Runtime->GetFrameNumber(); const FVector2D Orbit=ManualOrbit;
        const float Zoom=ViewZoom; const bool Enlarged=bEnlarged;
        FVector2D Pixel;
        const FVector2D Screen=ImmersiveWidget->ViewportPoint(CrankImmersive::FLayout::Center(L.Screen));
        const bool Mapping=ScreenPoint(Screen,Pixel) && FVector2D::Distance(Pixel,{200,120})<.01;
        UpdatePointer(911,A,false);
        for(int I=0;I<2;I++) {UpdatePointer(914,Screen,true);UpdatePointer(914,Screen,false);}
        const bool Returned=!bImmersive && LastButtons==0 && Pointers.IsEmpty();
        SetImmersive(true);
        const bool Preserved=CurrentGameDirectory==Game && Runtime->GetFrameNumber()==Frame && GetDisplayTexture()==Texture && ManualOrbit==Orbit && ViewZoom==Zoom && bEnlarged==Enlarged;
        UpdatePointer(915,A,true); SetOverlayOpen(true);
        const bool Cleared=Pointers.IsEmpty() && LastButtons==0; SetOverlayOpen(false);
        bImmersive=false; LoadExperience();
        const bool Persisted=bImmersive && ExperienceError.IsEmpty();
        bImmersiveProbePassed=bImmersiveHeldA && bImmersiveHeldBoth && bImmersiveReleasedB && Mapping && Returned && Preserved && Cleared && Persisted;
        UE_LOG(LogTemp,Display,TEXT("Crank immersive: holdA=%d multi=%d releaseB=%d mapping=%d return3D=%d session=%d clear=%d persist=%d"),bImmersiveHeldA,bImmersiveHeldBoth,bImmersiveReleasedB,Mapping,Returned,Preserved,Cleared,Persisted);
    }
    ImmersiveProbePhase++;
}
void ACrankDevicePawn::ResetView() { ManualOrbit=FVector2D::ZeroVector; ViewZoom=1; CalibrateMotion(); PulseHaptic(); }
void ACrankDevicePawn::UpdateImmersiveTouchProbe() {
    if(ImmersiveTouchProbePhase==7 || ImmersiveProbePhase!=5 || ProbeElapsed<3.3f || !bImmersive || !ImmersiveWidget || !FSlateApplication::IsInitialized()) return;
    bImmersiveTouchProbeRan=true;
    auto& App=FSlateApplication::Get();
    const auto& L=ImmersiveWidget->GetLayout();
    const auto G=GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
    const FVector2D Pixels(GEngine->GameViewport->Viewport->GetSizeXY());
    auto Absolute=[&](FVector2D Local){return G.LocalToAbsolute(ImmersiveWidget->ViewportPoint(Local)*FVector2D(G.GetLocalSize())/Pixels);};
    auto Begin=[&](uint32 Id,FVector2D P){App.ProcessTouchStartedEvent(nullptr,FPointerEvent(0u,Id,P,P,1.f,true));};
    auto Move=[&](uint32 Id,FVector2D P,FVector2D Last){App.ProcessTouchMovedEvent(FPointerEvent(0u,Id,P,Last,1.f,true));};
    auto End=[&](uint32 Id,FVector2D P){App.ProcessTouchEndedEvent(FPointerEvent(0u,Id,P,P,0.f,false));};
    const FVector2D ScreenCenter=CrankImmersive::FLayout::Center(L.Screen);
    const FVector2D ScreenUp=ScreenCenter-FVector2D(0,(L.Screen.Bottom-L.Screen.Top)*.2f);
    const FVector2D ScreenSmall=ScreenCenter-FVector2D(0,2);
    const FVector2D Screen=Absolute(ScreenCenter), Up=Absolute(ScreenUp), Small=Absolute(ScreenSmall);
    const FVector2D B=Absolute(CrankImmersive::FLayout::Center(L.B));
    const FVector2D A=Absolute(CrankImmersive::FLayout::Center(L.A));
    // FSceneViewport rounds touch positions to integer pixels before polling.
    const float PixelTolerance=360.f/float(ImmersiveWidget->ViewportPoint({L.Screen.Left,L.Screen.Bottom}).Y-ImmersiveWidget->ViewportPoint({L.Screen.Left,L.Screen.Top}).Y)+.01f;
    if(ImmersiveTouchProbePhase==0) {
        CrankVelocity=0; // The synthetic continuous-crank probe has finished.
        const FVector2D Center=CrankImmersive::FLayout::Center(L.Dial);
        const float Radius=(L.Dial.Right-L.Dial.Left)*.32f;
        const FVector2D Start=Absolute(Center-FVector2D(0,Radius)), Right=Absolute(Center+FVector2D(Radius,0));
        const FWidgetPath Hit=App.LocateWindowUnderMouse(Start,App.GetInteractiveTopLevelWindows(),false,0);
        FString Path;
        for(int32 I=0;I<Hit.Widgets.Num();I++) Path+=Hit.Widgets[I].Widget->GetTypeAsString()+TEXT("/");
        const float Before=CrankTarget;
        Begin(3,Start); Move(3,Right,Start);
        const float Delta=CrankTarget-Before;
        const bool Captured=Hit.IsValid() && Hit.Widgets[Hit.Widgets.Num()-1].Widget->HasMouseCaptureByUser(0,3);
        Move(3,Start,Right); End(3,Start);
        const bool Released=Hit.IsValid() && !Hit.Widgets[Hit.Widgets.Num()-1].Widget->HasMouseCaptureByUser(0,3);
        bImmersiveDialPassed=Captured && Released && FMath::IsNearlyEqual(Delta,90.f,.1f) && FMath::IsNearlyEqual(CrankTarget,Before,.1f);
        UE_LOG(LogTemp,Display,TEXT("Crank Slate dial: path=%s clockwise=%.2f reverse/release=%d"),*Path,Delta,bImmersiveDialPassed);
        App.SetAllUserFocusToGameViewport();
        Begin(2,A);
        ImmersiveTouchStartTarget=CrankTarget;
        const FWidgetPath ScreenHit=App.LocateWindowUnderMouse(Screen,App.GetInteractiveTopLevelWindows(),false,0);
        Path.Empty();for(int32 I=0;I<ScreenHit.Widgets.Num();I++) Path+=ScreenHit.Widgets[I].Widget->GetTypeAsString()+TEXT("/");
        UE_LOG(LogTemp,Display,TEXT("Crank Slate screen path=%s"),*Path);
        Begin(4,Screen);
    } else if(ImmersiveTouchProbePhase==1) {
        Move(4,Small,Screen);
    } else if(ImmersiveTouchProbePhase==2) {
        bImmersiveSwipePassed=FMath::IsNearlyEqual(CrankTarget,ImmersiveTouchStartTarget,.1f);
        Move(4,Up,Small);
    } else if(ImmersiveTouchProbePhase==3) {
        bImmersiveSwipePassed&=FMath::IsNearlyEqual(CrankTarget-ImmersiveTouchStartTarget,72.f,PixelTolerance);
        bImmersiveSwipeMultiPassed=LastButtons==32;
        Begin(5,B);
    } else if(ImmersiveTouchProbePhase==4) {
        bImmersiveSwipeMultiPassed&=LastButtons==(32|16);
        Move(4,Screen,Up);
    } else if(ImmersiveTouchProbePhase==5) {
        bImmersiveSwipePassed&=FMath::IsNearlyEqual(CrankTarget,ImmersiveTouchStartTarget,.1f);
        End(4,Screen); End(5,B);
    } else if(ImmersiveTouchProbePhase==6) {
        bImmersiveSwipeMultiPassed&=LastButtons==32;
        const bool TapSafe=bImmersive && PendingScreenTapTime<0 && !Pointers.Contains(4) && !ScreenGestureStarts.Contains(4);
        End(2,A);
        ToggleCrankDocked(); const float DockedTarget=CrankTarget;
        const FVector2D P=ImmersiveWidget->ViewportPoint(ScreenCenter), Q=ImmersiveWidget->ViewportPoint(ScreenUp);
        UpdatePointer(918,P,true);UpdatePointer(918,Q,true);
        const bool DockedBlocked=CrankTarget==DockedTarget && Pointers.FindRef(918)==TEXT("screen");
        UpdatePointer(918,Q,false); ToggleCrankDocked(); SetOverlayOpen(true);
        const float OverlayTarget=CrankTarget; UpdatePointer(918,P,true);UpdatePointer(918,Q,true);
        const bool OverlayBlocked=CrankTarget==OverlayTarget && Pointers.IsEmpty() && ScreenGestureStarts.IsEmpty();
        SetOverlayOpen(false);
        bImmersiveTouchProbePassed=bImmersiveDialPassed && bImmersiveSwipePassed && bImmersiveSwipeMultiPassed && TapSafe && DockedBlocked && OverlayBlocked;
        UE_LOG(LogTemp,Display,TEXT("Crank Slate swipe: up/down=%d heldButtons=%d tapSafe=%d docked=%d overlay=%d passed=%d"),bImmersiveSwipePassed,bImmersiveSwipeMultiPassed,TapSafe,DockedBlocked,OverlayBlocked,bImmersiveTouchProbePassed);
    }
    ImmersiveTouchProbePhase++;
}
void ACrankDevicePawn::CalibrateMotion() { bMotionCalibrated=false; IntegratedMotion=FVector2D::ZeroVector; MotionOrbit=FVector2D::ZeroVector; }
void ACrankDevicePawn::SetMotionFollow(bool Enabled) {
    bMotionFollow=Enabled; CalibrateMotion(); FPlatformApplicationMisc::EnableMotionData(Enabled); SaveExperience();
}
void ACrankDevicePawn::SetHaptics(bool Enabled) {
    bHaptics=Enabled; SaveExperience();
    if(!Enabled) {FCrankHaptics::Cancel(); LastHapticTime=-1;}
}
void ACrankDevicePawn::SetMotionSensitivity(float Value) { MotionSensitivity=FMath::Clamp(Value,.1f,.8f); SaveExperience(); }
void ACrankDevicePawn::SetHapticStrength(float Value) { HapticStrength=FMath::Clamp(Value,.1f,1.f); SaveExperience(); }
void ACrankDevicePawn::SetReducedMotion(bool Enabled) { bReducedMotion=Enabled; SaveExperience(); }
void ACrankDevicePawn::ToggleCrankDocked() {
    ReleaseInputs(); bCrankDocked=!bCrankDocked; ApplyCrankRotation(CrankAngle); Runtime->AddCrankDelta(0,bCrankDocked); PulseHaptic(); SaveExperience();
}
void ACrankDevicePawn::TurnCrank(float Degrees) { if(!bCrankDocked && !bOverlayOpen && (!Runtime->IsSuspended() || SystemMode==ECrankSystemMode::Home || SystemMode==ECrankSystemMode::Menu)) CrankTarget+=FMath::Clamp(Degrees,-90.f,90.f); }
void ACrankDevicePawn::PulseHaptic(bool CrankTick) {
    const double Now=FPlatformTime::Seconds();
    if(!bHaptics || Now-LastHapticTime<(CrankTick ? .085 : .035)) return;
    LastHapticTime=Now;
    FCrankHaptics::Pulse(CrankTick,HapticStrength);
}
void ACrankDevicePawn::UpdateMotion(float DeltaTime) {
    if(!bMotionFollow || bImmersive || bOverlayOpen || Pointers.Num()) return;
    auto* PC=Cast<APlayerController>(GetController()); if(!PC) return;
    FVector Tilt,Rate,Gravity,Acceleration; PC->GetInputMotionState(Tilt,Rate,Gravity,Acceleration);
    if(Gravity.ContainsNaN() || Gravity.SizeSquared()<.25 || Tilt.ContainsNaN() || Rate.ContainsNaN()) return;
    bHasMotionSample=true;
    const auto Orientation=FPlatformMisc::GetDeviceOrientation();
    if(LastSensorOrientation!=int32(Orientation)) { LastSensorOrientation=int32(Orientation); CalibrateMotion(); }
    FVector2D Reading(FMath::RadiansToDegrees(Tilt.Y),-FMath::RadiansToDegrees(Tilt.X));
    if(!bMotionCalibrated) { MotionReference=Reading; bMotionCalibrated=true; }
    FVector2D Relative(FMath::FindDeltaAngleDegrees(MotionReference.X,Reading.X),FMath::FindDeltaAngleDegrees(MotionReference.Y,Reading.Y));
    // UE's legacy Android motion space flips these gyro axes for landscape.
    if(Orientation==EDeviceScreenOrientation::LandscapeLeft) Rate.X=-Rate.X;
    if(Orientation==EDeviceScreenOrientation::LandscapeRight) Rate.Y=-Rate.Y;
    IntegratedMotion+=FVector2D(FMath::RadiansToDegrees(Rate.Y),-FMath::RadiansToDegrees(Rate.X))*FMath::Min(DeltaTime,.05f);
    IntegratedMotion=FMath::Lerp(IntegratedMotion,Relative,CrankView::SmoothAlpha(5,DeltaTime));
    if(Orientation==EDeviceScreenOrientation::LandscapeLeft) Relative=FVector2D(-IntegratedMotion.Y,IntegratedMotion.X);
    else if(Orientation==EDeviceScreenOrientation::LandscapeRight) Relative=FVector2D(IntegratedMotion.Y,-IntegratedMotion.X);
    else if(Orientation==EDeviceScreenOrientation::PortraitUpsideDown) Relative=-IntegratedMotion;
    else Relative=IntegratedMotion;
    const float Limit=bEnlarged ? 5.f : 10.f;
    const FVector2D Target(FMath::Clamp(CrankView::DeadZone(Relative.X,.7f)*MotionSensitivity,-Limit,Limit),
        FMath::Clamp(CrankView::DeadZone(Relative.Y,.7f)*MotionSensitivity,-Limit,Limit));
    MotionOrbit=FMath::Lerp(MotionOrbit,Target,CrankView::SmoothAlpha(9,DeltaTime));
}
void ACrankDevicePawn::LoadExperience() {
    FString Text; TSharedPtr<FJsonObject> Object;
    if(!FFileHelper::LoadFileToString(Text,*ExperienceFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Object)) return;
    Object->TryGetBoolField(TEXT("motionFollow"),bMotionFollow); Object->TryGetBoolField(TEXT("haptics"),bHaptics);
    Object->TryGetBoolField(TEXT("reducedMotion"),bReducedMotion); Object->TryGetBoolField(TEXT("crankDocked"),bCrankDocked);
    Object->TryGetBoolField(TEXT("immersive"),bImmersive);
    double Number;
    if(Object->TryGetNumberField(TEXT("motionSensitivity"),Number) && FMath::IsFinite(Number)) MotionSensitivity=FMath::Clamp(float(Number),.1f,.8f);
    if(Object->TryGetNumberField(TEXT("hapticStrength"),Number) && FMath::IsFinite(Number)) HapticStrength=FMath::Clamp(float(Number),.1f,1.f);
    if(Object->TryGetNumberField(TEXT("frameRate"),Number)) FrameRate=Number<45 ? 30 : 60;
    if(Object->TryGetNumberField(TEXT("resolutionScale"),Number) && FMath::IsFinite(Number)) ResolutionScale=FMath::Clamp(float(Number),.75f,1.f);
    const TArray<TSharedPtr<FJsonValue>>* Recent;
    if(Object->TryGetArrayField(TEXT("recentGames"),Recent)) for(const auto& Value:*Recent) if(Value->Type==EJson::String) RecentGames.AddUnique(Value->AsString());
}
void ACrankDevicePawn::SaveExperience() {
    auto Object=MakeShared<FJsonObject>();
    Object->SetBoolField(TEXT("motionFollow"),bMotionFollow); Object->SetBoolField(TEXT("haptics"),bHaptics);
    Object->SetBoolField(TEXT("reducedMotion"),bReducedMotion); Object->SetBoolField(TEXT("crankDocked"),bCrankDocked);
    Object->SetBoolField(TEXT("immersive"),bImmersive);
    Object->SetNumberField(TEXT("motionSensitivity"),MotionSensitivity); Object->SetNumberField(TEXT("hapticStrength"),HapticStrength);
    Object->SetNumberField(TEXT("frameRate"),FrameRate); Object->SetNumberField(TEXT("resolutionScale"),ResolutionScale);
    TArray<TSharedPtr<FJsonValue>> Recent; for(const auto& Path:RecentGames) Recent.Add(MakeShared<FJsonValueString>(Path));
    Object->SetArrayField(TEXT("recentGames"),Recent);
    FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ExperienceFile),true);
    const FString Temporary=ExperienceFile+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(Text,*Temporary) || !IFileManager::Get().Move(*ExperienceFile,*Temporary,true,true)) {
        ExperienceError=TEXT("设置无法保存，请检查可用存储空间。");
        UE_LOG(LogTemp,Warning,TEXT("Crank experience settings could not be committed"));
    } else ExperienceError.Empty();
}

bool ACrankDevicePawn::IsGameRunning() const {return Runtime->IsRunning();}
bool ACrankDevicePawn::IsGameSwitching() const {return Runtime->IsSwitching() || SystemMode==ECrankSystemMode::Loading;}
FString ACrankDevicePawn::GetGameCapability(const FString& Directory) const {
    if(Directory==CurrentGameDirectory && IsGameRunning() && !Runtime->ExecutionMode.IsEmpty())return TEXT("正在运行 · ")+Runtime->ExecutionMode;
    const TCHAR* State=Directory==CurrentGameDirectory && IsGameRunning() ? TEXT("正在运行 · ") :
        RecentGames.Contains(Directory) ? TEXT("曾启动 · ") : TEXT("未验证 · ");
    return FString(State)+FCrankGameFiles::Capability(Directory);
}
UTexture2D* ACrankDevicePawn::GetGameCover(const FString& Directory) {
    if(const auto* Cached=GameCovers.Find(Directory)) return Cached->Get();
    FIntPoint Size; TArray<uint8> Data; UTexture2D* Texture=nullptr;
    if(FCrankGameFiles::CoverPixels(Directory,Size,Data)) {
        Texture=UTexture2D::CreateTransient(Size.X,Size.Y,PF_B8G8R8A8); Texture->SRGB=false; Texture->Filter=TF_Nearest; Texture->NeverStream=true;
        auto& Mip=Texture->GetPlatformData()->Mips[0]; void* Dest=Mip.BulkData.Lock(LOCK_READ_WRITE); FMemory::Memcpy(Dest,Data.GetData(),Data.Num()); Mip.BulkData.Unlock(); Texture->UpdateResource();
    }
    GameCovers.Add(Directory,Texture); return Texture;
}
void ACrankDevicePawn::ApplySystemPause() {
    Runtime->SetGamePaused(bOverlayOpen || (SystemMode!=ECrankSystemMode::Playing && SystemMode!=ECrankSystemMode::Loading));
}
TArray<FString> ACrankDevicePawn::GetSystemRows() const {
    TArray<FString> Rows;
    if(SystemMode==ECrankSystemMode::Menu) {
        Rows.Add(TEXT("继续游戏"));
        for(const auto& Item:Runtime->MenuItems) {
            FString Value;
            if(Item.Kind==1) Value=Item.Value ? TEXT("  [开启]") : TEXT("  [关闭]");
            if(Item.Kind==2 && Item.Options.IsValidIndex(Item.Value-1)) Value=TEXT("  ")+Item.Options[Item.Value-1];
            Rows.Add(Item.Title+Value);
        }
        Rows.Add(TEXT("重新启动游戏")); Rows.Add(bDefaultLauncher ? (CurrentGameDirectory==LauncherDirectory ? TEXT("本地游戏库") : TEXT("返回 PDGames")) : TEXT("返回桌面")); Rows.Add(TEXT("体验设置"));
    } else if(SystemMode==ECrankSystemMode::Home) {
        for(const auto& Game:GetGameDirectories()) Rows.Add(GetTitleForGame(Game));
        Rows.Add(TEXT("游戏库 / 导入")); Rows.Add(TEXT("体验设置"));
    }
    return Rows;
}
void ACrankDevicePawn::ToggleSystemMenu() {
    if(bOverlayOpen || Runtime->IsSwitching()) return;
    if(SystemMode==ECrankSystemMode::Menu) {CloseSystemMenu(); return;}
    if(SystemMode==ECrankSystemMode::Playing && Runtime->IsRunning()) {
        ReleaseInputs(); SystemMode=ECrankSystemMode::Menu; SystemSelection=0;
        ApplySystemPause(); Runtime->SetSystemMenuOpen(true); Runtime->RefreshMenu(); SystemPollElapsed=1;
    } else if(SystemMode==ECrankSystemMode::Home && Overlay) Overlay->ShowLibrary();
}
void ACrankDevicePawn::CloseSystemMenu() {
    Runtime->SetSystemMenuOpen(false); ReleaseInputs(); SystemMode=ECrankSystemMode::Playing; ApplySystemPause();
    Runtime->AddCrankDelta(0,bCrankDocked);
}
void ACrankDevicePawn::ReturnHome() {
    if(Runtime->IsSwitching()) return;
    if(bDefaultLauncher && !LauncherDirectory.IsEmpty()) {
        if(CurrentGameDirectory==LauncherDirectory && Runtime->IsRunning()) {CloseSystemMenu();if(Overlay) Overlay->ShowLibrary();return;}
        if(Launcher && Launcher->IsBusy()) {Launcher->Cancel();return;}
        if(PlayGame(LauncherDirectory)) return;
    }
    ReleaseInputs(); Runtime->SetSystemMenuOpen(false); Runtime->RequestStop();
    SystemMode=ECrankSystemMode::Home; SystemSelection=0; ApplySystemPause();
    if(Overlay) Overlay->RefreshGames();
}
void ACrankDevicePawn::ToggleLock() {
    if(bOverlayOpen || SystemMode==ECrankSystemMode::Loading || SystemMode==ECrankSystemMode::Boot) return;
    ReleaseInputs();
    if(SystemMode==ECrankSystemMode::Locked) {SystemMode=BeforeLock; Runtime->SetDeviceLocked(false);}
    else {BeforeLock=SystemMode; SystemMode=ECrankSystemMode::Locked; Runtime->SetDeviceLocked(true);}
    ApplySystemPause(); PulseHaptic(); SystemPollElapsed=1;
}
void ACrankDevicePawn::SystemNavigate(int32 Direction) {
    const int Count=GetSystemRows().Num();
    if(Count) {SystemSelection=(SystemSelection+Direction%Count+Count)%Count; PulseHaptic(); SystemPollElapsed=1;}
}
void ACrankDevicePawn::SystemActivate(int32 Direction) {
    if(SystemMode==ECrankSystemMode::Home) {
        if(Direction || Runtime->IsSwitching()) return;
        const auto Games=GetGameDirectories();
        if(Games.IsValidIndex(SystemSelection)) PlayGame(Games[SystemSelection]);
        else if(Overlay) {if(SystemSelection==Games.Num()) Overlay->ShowLibrary(); else Overlay->ShowSettings();}
        return;
    }
    if(SystemMode!=ECrankSystemMode::Menu) return;
    if(SystemSelection==0) {if(!Direction) CloseSystemMenu(); return;}
    const int ItemIndex=SystemSelection-1;
    if(Runtime->MenuItems.IsValidIndex(ItemIndex)) {
        const auto Item=Runtime->MenuItems[ItemIndex];
        if(Item.Kind==0) {
            if(Direction) return;
            /* The screen hides first; callback remains ahead of gameWillResume. */
            SystemMode=ECrankSystemMode::Playing;
            if(!Runtime->SelectMenuItem(Item.Id,0)) LastStartError=TEXT("菜单操作未能提交，请重新打开菜单后重试。");
            CloseSystemMenu();
        } else {
            const int Value=Item.Kind==1 ? (Direction ? int(Direction>0) : int(!Item.Value)) :
                (Item.Options.Num() ? (Item.Value-1+(Direction<0 ? Item.Options.Num()-1 : 1))%Item.Options.Num()+1 : 0);
            if(!Runtime->SelectMenuItem(Item.Id,Value)) LastStartError=TEXT("菜单操作未能提交，请重新打开菜单后重试。");
            SystemPollElapsed=1;
        }
    } else if(!Direction) {
        const int Action=ItemIndex-Runtime->MenuItems.Num();
        if(Action==0) {const FString Game=CurrentGameDirectory; Runtime->SetSystemMenuOpen(false); CurrentGameDirectory.Empty(); PlayGame(Game);}
        else if(Action==1) ReturnHome();
        else if(Overlay) Overlay->ShowSettings();
    }
}
void ACrankDevicePawn::ScreenTap(const FVector2D& Pixel) {
    if(SystemMode==ECrankSystemMode::Locked) {ToggleLock(); return;}
    if(SystemMode==ECrankSystemMode::Playing && CurrentGameDirectory==LauncherDirectory && LauncherDetailId>0 && Pixel.Y>=208 && Pixel.Y<=240 && Pixel.X>=0 && Pixel.X<=400) {
        if(Pixel.X>=220) Runtime->ActivateLauncher();else Runtime->ActivateLauncherDetail(Pixel.X>=100?2:3);return;
    }
    if(SystemMode!=ECrankSystemMode::Home && SystemMode!=ECrankSystemMode::Menu) return;
    const bool Menu=SystemMode==ECrankSystemMode::Menu;
    if(!Menu && Pixel.Y>=40 && Pixel.Y<214) {
        const auto Games=GetGameDirectories();
        if(Games.IsValidIndex(SystemSelection) && GetGameCover(Games[SystemSelection])) {SystemActivate(); return;}
    }
    if(Menu && Pixel.X<200) return;
    const auto Rows=GetSystemRows(); const int Visible=Menu ? 7 : 5, Height=Menu ? 26 : 33;
    const int First=FMath::Clamp(SystemSelection-(Visible-1)/2,0,FMath::Max(0,Rows.Num()-Visible));
    const int Row=FMath::FloorToInt((Pixel.Y-38)/Height)+First;
    if(Pixel.Y>=38 && Pixel.Y<38+Visible*Height && Rows.IsValidIndex(Row)) {SystemSelection=Row; SystemActivate();}
}
void ACrankDevicePawn::UpdateSystem(float DeltaTime) {
    SystemElapsed+=DeltaTime;
    if(SystemMode==ECrankSystemMode::Boot && SystemElapsed>.85f) {
        if(!bDefaultLauncher || LauncherDirectory.IsEmpty() || !PlayGame(LauncherDirectory)) {SystemMode=ECrankSystemMode::Home; ApplySystemPause();}
    }
    if(SystemMode==ECrankSystemMode::Loading && !Runtime->IsSwitching()) {
        if(!Runtime->LastError.IsEmpty()) {LastStartError=TEXT("游戏载入失败。可在游戏库重新启动。"); SystemMode=ECrankSystemMode::Home; ApplySystemPause();}
        else if(Runtime->GetFrameNumber()>0 && Runtime->IsRunning()) {
            SystemMode=ECrankSystemMode::Playing; Runtime->AddCrankDelta(0,bCrankDocked);
            if(!bLaunchRecorded) {
                if(CurrentGameDirectory!=LauncherDirectory) {RecentGames.Remove(CurrentGameDirectory); RecentGames.Insert(CurrentGameDirectory,0); RecentGames.SetNum(FMath::Min(RecentGames.Num(),12)); SaveExperience();}
                bLaunchRecorded=true;
            }
        }
    }
    if(SystemMode==ECrankSystemMode::Playing && !Runtime->IsSwitching() && Runtime->GetRuntimeStatus()==0 && bLaunchRecorded) ReturnHome();
    if(PendingScreenTapTime>=0 && FPlatformTime::Seconds()>=PendingScreenTapTime) {
        PendingScreenTapTime=-1; if(!bOverlayOpen) ScreenTap(PendingScreenTap);
    }
    if((SystemPollElapsed+=DeltaTime)>.1f) {
        SystemPollElapsed=0;
        if(SystemMode==ECrankSystemMode::Menu) {Runtime->RefreshMenu(); SystemSelection=FMath::Clamp(SystemSelection,0,FMath::Max(0,GetSystemRows().Num()-1));}
        if(SystemMode!=ECrankSystemMode::Playing && SystemRenderer && SystemWidget && SystemTexture)
            SystemRenderer->DrawWidget(SystemTexture,SystemWidget.ToSharedRef(),{400,240},DeltaTime);
    }
}
void ACrankDevicePawn::UpdateLauncher() {
    if(Translation)Translation->Tick();
    if(!Launcher) return;
    Launcher->Tick();
    FString Request;
    while(Runtime->PollLauncherRequest(Request)) {
        TSharedPtr<FJsonObject> Object;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Request),Object)) continue;
        LauncherSession=Object->GetIntegerField(TEXT("session"));const int32 Id=Object->GetIntegerField(TEXT("id"));
        const FString Action=Object->GetStringField(TEXT("action"));
        if(Action==TEXT("inspect")) {LauncherDetailId=Id;bInlineChinese=bInlineOriginal=false;LauncherInspectElapsed=ProbeElapsed;Launcher->Select(Id);}
        else if(Action==TEXT("leave")) {LauncherDetailId=0;if(Translation)Translation->Cancel();}
        else if(Translation && Id==LauncherDetailId && CurrentGameDirectory==LauncherDirectory) {
            if(Action==TEXT("translate")) Translation->Open(Id);
            else if(Action==TEXT("original") || Action==TEXT("trans_cancel")) Translation->Cancel();
            else if(Action==TEXT("model")) Translation->DownloadModel();
            else if(Action==TEXT("model_cancel")) Translation->CancelModelDownload();
            else if(Action==TEXT("lang_zh")) {bInlineChinese=true;bInlineOriginal=false;}
            else if(Action==TEXT("lang_en")) {bInlineOriginal=true;bInlineChinese=false;}
            else if(Action==TEXT("play")) Launcher->Play(Id);
            else if(Action==TEXT("cancel")) Launcher->Cancel();
            else if(Action==TEXT("publisher")) Launcher->OpenPublisher();
            else if(Action==TEXT("library") && Overlay) Overlay->ShowLibrary();
        }

    }
    if(LauncherSession) {
        TSharedPtr<FJsonObject> Snapshot;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Launcher->Snapshot()),Snapshot)) {
            auto T=MakeShared<FJsonObject>();
            T->SetNumberField(TEXT("id"),Translation->GetId());T->SetStringField(TEXT("state"),Translation->GetState());
            T->SetStringField(TEXT("text"),Translation->GetTranslated());T->SetBoolField(TEXT("modelReady"),Translation->HasModel());
            T->SetBoolField(TEXT("modelBusy"),Translation->IsModelBusy());T->SetBoolField(TEXT("modelError"),Translation->HasModelError());
            T->SetStringField(TEXT("modelState"),Translation->GetModelState());
            T->SetStringField(TEXT("modelDownloadLabel"),Translation->ModelDownloadLabel());
            if(auto Progress=Translation->ModelProgress();Progress.IsSet())T->SetNumberField(TEXT("modelProgress"),Progress.GetValue());
            Snapshot->SetObjectField(TEXT("translation"),T);
            FString Json;FJsonSerializer::Serialize(Snapshot.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
            if(Json!=LastLauncherReply && Runtime->ReplyLauncher(LauncherSession,Json))LastLauncherReply=Json;
        }
    }
    const FString Game=Launcher->TakeLaunch();
    if(!Game.IsEmpty()) {
        if(CurrentGameDirectory==LauncherDirectory && LauncherDetailId==Launcher->GetLaunchId() && SystemMode==ECrankSystemMode::Playing && !bOverlayOpen && !Runtime->IsSuspended()) {
            if(!PlayGame(Game)) LastStartError=TEXT("游戏已安装，但启动失败，可从游戏库中重试。");
            if(Overlay) Overlay->RefreshGames();
        } else {ImportMessage=TEXT("游戏已安装，可从游戏库打开。");if(Overlay) Overlay->RefreshGames();}
    }
}
void ACrankDevicePawn::ApplyPerformance() {
    if(GEngine) if(auto* Settings=GEngine->GetGameUserSettings()) {
        Settings->SetFrameRateLimit(FrameRate); Settings->SetResolutionScaleValueEx(ResolutionScale*100.f);
        Settings->ApplyNonResolutionSettings();
    }
}
void ACrankDevicePawn::SetFrameRate(int32 Value) {FrameRate=Value<=30 ? 30 : 60; ApplyPerformance(); SaveExperience();}
void ACrankDevicePawn::SetResolutionScale(float Value) {ResolutionScale=FMath::Clamp(Value,.75f,1.f); ApplyPerformance(); SaveExperience();}
void ACrankDevicePawn::UpdateSystemFlowProbe() {
    if(ProbeElapsed<3.1f || !Runtime->LastError.IsEmpty()) return;
    if(SystemFlowPhase==0 && SystemMode==ECrankSystemMode::Playing) {
        SystemFlowGame=CurrentGameDirectory; ToggleSystemMenu(); SystemFlowPhase=1;
    } else if(SystemFlowPhase==1 && Runtime->GetRuntimeStatus()==4) {
        SystemFlowFrame=Runtime->GetFrameNumber(); Runtime->RefreshMenu();
        if(Runtime->MenuItems.Num()!=3) {LastStartError=TEXT("System flow fixture needs three SDK menu items"); return;}
        SystemSelection=2; SystemActivate(); SystemFlowPhase=2;
    } else if(SystemFlowPhase==2) {
        Runtime->RefreshMenu();
        if(Runtime->MenuItems[1].Value!=1) return;
        if(Runtime->GetFrameNumber()!=SystemFlowFrame) {LastStartError=TEXT("Game advanced while SDK menu was paused"); return;}
        CloseSystemMenu(); SystemFlowPhase=3;
    } else if(SystemFlowPhase==3 && Runtime->GetRuntimeStatus()==2) {
        if(CurrentGameDirectory!=SystemFlowGame || Runtime->GetFrameNumber()<SystemFlowFrame) {LastStartError=TEXT("Menu resume changed the game session"); return;}
        ToggleLock(); SystemFlowPhase=4;
    } else if(SystemFlowPhase==4 && Runtime->GetRuntimeStatus()==4) {ToggleLock(); SystemFlowPhase=5;}
    else if(SystemFlowPhase==5 && Runtime->GetRuntimeStatus()==2) {ReturnHome(); SystemFlowPhase=6;}
    else if(SystemFlowPhase==6 && !Runtime->IsSwitching() && SystemMode==ECrankSystemMode::Home) {
        if(PlayGame(SystemFlowGame)) SystemFlowPhase=7;
    } else if(SystemFlowPhase==7 && SystemMode==ECrankSystemMode::Playing && Runtime->GetFrameNumber()>35) {
        const int32 Frame=Runtime->GetFrameNumber();
        const bool Continued=PlayGame(SystemFlowGame) && Runtime->GetFrameNumber()==Frame && !Runtime->IsSwitching();
        bSystemFlowPassed=Continued; SystemFlowPhase=8; SystemFlowCompletedAt=ProbeElapsed;
        UE_LOG(LogTemp,Display,TEXT("Crank system flow: menu values/pause/resume, lock/unlock, home/relaunch, continue=%d"),Continued);
        ToggleSystemMenu(); SystemSelection=2;
    }
}
