#include "CrankViewState.h"
#include "CrankImmersiveLayout.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrankImmersiveLayoutTest,"CrankLab.Immersive.LayoutAndInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrankImmersiveLayoutTest::RunTest(const FString&) {
    for(const FVector2D Size:TArray<FVector2D>{{288,608},{320,640},{508,1156},{889,711},{812,358},{844,390},{1248,688},{1280,720},{2376,1080}}) {
        const auto L=CrankImmersive::Arrange(Size);
        TestTrue(TEXT("Toolbar never covers the LCD"),L.Screen.Top>=60.f);
        TestTrue(TEXT("LCD preserves 5:3 without cropping"),FMath::Abs((L.Screen.Right-L.Screen.Left)/(L.Screen.Bottom-L.Screen.Top)-5./3)<.00001);
        for(const auto& R:{L.Screen,L.Dpad,L.A,L.B,L.Dial})
            TestTrue(TEXT("Display and all touch surfaces fit inside safe bounds"),R.Left>=0 && R.Top>=0 && R.Right<=Size.X+.001 && R.Bottom+20<=Size.Y+.001);
        auto Overlaps=[](FSlateRect A,FSlateRect B){return A.Left<B.Right && A.Right>B.Left && A.Top<B.Bottom && A.Bottom>B.Top;};
        TestFalse(TEXT("LCD does not overlap direction pad"),Overlaps(L.Screen,L.Dpad));
        TestFalse(TEXT("LCD does not overlap A/B"),Overlaps(L.Screen,L.A) || Overlaps(L.Screen,L.B));
        TestFalse(TEXT("Direction pad does not overlap A/B"),Overlaps(L.Dpad,L.A) || Overlaps(L.Dpad,L.B));
        TestFalse(TEXT("Crank does not overlap buttons or screen"),Overlaps(L.Dial,L.Screen) || Overlaps(L.Dial,L.A) || Overlaps(L.Dial,L.B) || Overlaps(L.Dial,L.Dpad));
        TestTrue(TEXT("Large A/B targets retain at least 64 units"),L.A.Right-L.A.Left>=64 && L.B.Right-L.B.Left>=64);
        TestTrue(TEXT("Portrait crank retains a generous drag surface"),L.bSideControls || L.Dial.Right-L.Dial.Left>=128);
        const auto ScreenCenter=CrankImmersive::FLayout::Center(L.Screen);
        const FVector2D Quarter(0,(L.Screen.Bottom-L.Screen.Top)*.25);
        TestTrue(TEXT("Upward screen swipe produces clockwise quarter-turn at every viewport size"),FMath::IsNearlyEqual(L.ScreenCrankDelta(ScreenCenter,ScreenCenter-Quarter),90.f,.001f));
        TestTrue(TEXT("Downward screen swipe reverses the crank"),FMath::IsNearlyEqual(L.ScreenCrankDelta(ScreenCenter-Quarter,ScreenCenter),-90.f,.001f));
        TestEqual(TEXT("Horizontal swipe does not turn the crank"),L.ScreenCrankDelta(ScreenCenter,ScreenCenter+FVector2D(20,0)),0.f);
        TestEqual(TEXT("A and B use canonical runtime names"),L.HitControl(CrankImmersive::FLayout::Center(L.A)),FName(TEXT("A")));
        TestEqual(TEXT("B does not activate A"),L.HitControl(CrankImmersive::FLayout::Center(L.B)),FName(TEXT("B")));
        const auto C=CrankImmersive::FLayout::Center(L.Dpad); const float Cell=(L.Dpad.Right-L.Dpad.Left)/3;
        TestEqual(TEXT("Pad center is neutral"),L.HitControl(C),FName(TEXT("dpad")));
        TestEqual(TEXT("Left points left"),L.HitControl(C-FVector2D(Cell,0)),FName(TEXT("left")));
        TestEqual(TEXT("Right points right"),L.HitControl(C+FVector2D(Cell,0)),FName(TEXT("right")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrankCameraFitTest,"CrankLab.StageTwo.BodyFraming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrankCameraFitTest::RunTest(const FString&) {
    const FBox Bounds(FVector(-.5,-3.2,-4.1),FVector(.6,3.2,4.1));
    for(float Aspect:{.5625f,1.f,2.22f}) for(float Yaw:{-32.f,0.f,32.f}) {
        const FVector Eye=FRotator(16,Yaw,0).RotateVector(FVector(1,-.13,.05).GetSafeNormal());
        const float Distance=CrankView::FitDistance(Bounds,Bounds.GetCenter(),Eye,Aspect,42,.88f);
        const FMatrix Basis=FRotationMatrix::MakeFromX(-Eye);
        const FVector Position=Bounds.GetCenter()+Eye*Distance;
        for(int I=0;I<8;I++) {
            const FVector Corner(I&1 ? Bounds.Max.X:Bounds.Min.X,I&2 ? Bounds.Max.Y:Bounds.Min.Y,I&4 ? Bounds.Max.Z:Bounds.Min.Z);
            const FVector Offset=Corner-Position;
            const double Depth=FVector::DotProduct(Offset,-Eye);
            TestTrue(TEXT("Camera keeps every body corner in front"),Depth>0);
            const double X=FVector::DotProduct(Offset,Basis.GetUnitAxis(EAxis::Y))/(Depth*FMath::Tan(FMath::DegreesToRadians(21.)));
            const double Y=FVector::DotProduct(Offset,Basis.GetUnitAxis(EAxis::Z))/(Depth*FMath::Tan(FMath::DegreesToRadians(21.))/Aspect);
            TestTrue(TEXT("Both orientations fit the body with the requested margin"),FMath::Abs(X)<=.88001 && FMath::Abs(Y)<=.88001);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrankDoubleTapTest,"CrankLab.StageTwo.DoubleTapPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrankDoubleTapTest::RunTest(const FString&) {
    CrankView::FDoubleTap Tap;
    TestFalse(TEXT("First tap does not toggle"),Tap.Tap(1,{100,100},1));
    TestTrue(TEXT("A close second tap toggles"),Tap.Tap(1.2,{105,100},1));
    TestFalse(TEXT("Third tap begins a new pair"),Tap.Tap(1.3,{100,100},1));
    Tap.Cancel();
    TestFalse(TEXT("Cancelled touches cannot complete a pair"),Tap.Tap(1.4,{100,100},1));
    TestFalse(TEXT("Slow double tap does not toggle"),Tap.Tap(2,{100,100},1));
    TestFalse(TEXT("Spatially separated taps do not toggle"),Tap.Tap(2.1,{200,100},1));
    Tap.Cancel(); Tap.Tap(3,{200,100},2);
    TestTrue(TEXT("Tap tolerance scales with Android DPI"),Tap.Tap(3.2,{245,100},2));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrankMotionMathTest,"CrankLab.StageTwo.FrameIndependentMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrankMotionMathTest::RunTest(const FString&) {
    const float Expected=1-FMath::Exp(-9.f);
    for(int FPS:{30,60,120}) {
        float Value=0; for(int I=0;I<FPS;I++) Value=FMath::Lerp(Value,1.f,CrankView::SmoothAlpha(9,1.f/FPS));
        TestTrue(TEXT("Follow damping is independent of frame rate"),FMath::Abs(Value-Expected)<.00001);
    }
    TestEqual(TEXT("Sensor jitter stays inside the dead zone"),CrankView::DeadZone(.6f,.7f),0.f);
    TestTrue(TEXT("Negative tilt keeps its direction"),CrankView::DeadZone(-2,.7f)<0);
    return true;
}
#endif
