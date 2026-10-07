#include "CrankImmersive.h"
#include "CrankDevicePawn.h"
#include "CrankControls.h"
#include "CrankTheme.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Widgets/SViewport.h"
#include "UnrealClient.h"
#include "Engine/Texture.h"

void SCrankImmersive::Construct(const FArguments& Args) {
    Device=Args._Device;
    ScreenBrush.DrawAs=ESlateBrushDrawType::Image;
    ScreenBrush.ImageSize=FVector2D(400,240);
    ChildSlot
    [SNew(SSafeZone).Padding(FMargin(16)).Visibility(EVisibility::SelfHitTestInvisible)
     [SAssignNew(Canvas,SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible)
      +SConstraintCanvas::Slot().AutoSize(false).Alignment(FVector2D::ZeroVector)
      .Offset_Lambda([this]{return FMargin(Layout.Dial.Left,Layout.Dial.Top,Layout.Dial.Right-Layout.Dial.Left,Layout.Dial.Bottom-Layout.Dial.Top);})
      [SNew(SBorder).BorderImage(&CrankTheme::Panel()).Padding(3)
       [SNew(SCrankDial).Device(Device).IsEnabled_Lambda([this]{return Device.IsValid() && !Device->IsOverlayOpen();})]]]];
}
void SCrankImmersive::Tick(const FGeometry& G,double Time,float Dt) {
    SCompoundWidget::Tick(G,Time,Dt);
    const FVector2D Size=Canvas->GetCachedGeometry().GetLocalSize();
    if(Size.X<1 || Size.Y<1) return;
    if(LastSize!=Size) {
        if(LastSize.X>0 && Device.IsValid()) Device->ReleaseInputs();
        LastSize=Size;Layout=CrankImmersive::Arrange(Size);
    }
}
static FGeometry ViewGeometry() {return GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();}
FVector2D SCrankImmersive::LocalPoint(const FVector2D& P) const {
    if(!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return {-10000,-10000};
    const FGeometry V=ViewGeometry(); const FIntPoint Pixels=GEngine->GameViewport->Viewport->GetSizeXY();
    if(Pixels.X<1 || Pixels.Y<1) return {-10000,-10000};
    return Canvas->GetCachedGeometry().AbsoluteToLocal(V.LocalToAbsolute(P*FVector2D(V.GetLocalSize())/FVector2D(Pixels)));
}
FVector2D SCrankImmersive::ViewportPoint(const FVector2D& P) const {
    if(!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return {-10000,-10000};
    const FGeometry V=ViewGeometry(); const FVector2D Size=V.GetLocalSize();
    if(Size.X<1 || Size.Y<1) return {-10000,-10000};
    return V.AbsoluteToLocal(Canvas->GetCachedGeometry().LocalToAbsolute(P))*FVector2D(GEngine->GameViewport->Viewport->GetSizeXY())/Size;
}
FName SCrankImmersive::HitControl(const FVector2D& P) const {return Layout.HitControl(LocalPoint(P));}
float SCrankImmersive::ScreenCrankDelta(const FVector2D& Previous,const FVector2D& Current) const {
    return Layout.ScreenCrankDelta(LocalPoint(Previous),LocalPoint(Current));
}
bool SCrankImmersive::ScreenPoint(const FVector2D& P,FVector2D& Pixel) const {
    const FVector2D Local=LocalPoint(P); const auto& R=Layout.Screen;
    if(!CrankImmersive::FLayout::Contains(R,Local)) return false;
    Pixel={(Local.X-R.Left)*400/(R.Right-R.Left),(Local.Y-R.Top)*240/(R.Bottom-R.Top)};
    return true;
}
int32 SCrankImmersive::OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const {
    static const FSlateColorBrush Fill(FLinearColor::White);
    FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),&Fill,ESlateDrawEffect::None,CrankTheme::Accent());
    const FGeometry Surface=Canvas->GetCachedGeometry();
    const float Alpha=Device.IsValid() && Device->IsOverlayOpen() ? .42f : 1.f;
    auto Box=[&](FSlateRect R,const FSlateBrush& Brush,FLinearColor Tint,int Offset=0) {
        FSlateDrawElement::MakeBox(Out,Layer+1+Offset,Surface.ToPaintGeometry(FVector2f(R.Right-R.Left,R.Bottom-R.Top),FSlateLayoutTransform(FVector2f(R.Left,R.Top))),&Brush,ESlateDrawEffect::None,Tint.CopyWithNewOpacity(Tint.A*Alpha));
    };
    static const FSlateRoundedBoxBrush Frame(CrankTheme::LcdFrame(),12.f);
    auto R=Layout.Screen;
    Box(FSlateRect(R.Left-7,R.Top-7,R.Right+7,R.Bottom+7),Frame,CrankTheme::LcdFrame());
    Box(R,Fill,CrankTheme::LcdBackground());
    if(Device.IsValid() && Device->GetDisplayTexture()) {
        ScreenBrush.SetResourceObject(Device->GetDisplayTexture());
        Box(R,ScreenBrush,CrankTheme::LcdBackground(),1);
    }
    static const FSlateRoundedBoxBrush Key(FLinearColor::White,CrankTheme::KeyEdge(),1.5f);
    static const FSlateRoundedBoxBrush Face(FLinearColor::White);
    static const FSlateRoundedBoxBrush Pad(FLinearColor::White,18.f,CrankTheme::KeyEdge(),1.5f);
    auto Text=[&](const FString& Label,FVector2D Center,const FSlateFontInfo& Font,FLinearColor Color) {
        const FVector2D Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label,Font);
        FSlateDrawElement::MakeText(Out,Layer+4,Surface.ToPaintGeometry(FVector2f(Measure),FSlateLayoutTransform(FVector2f(Center-Measure*.5))),Label,Font,ESlateDrawEffect::None,Color.CopyWithNewOpacity(Alpha));
    };
    auto Button=[&](const FSlateRect& Rect,const TCHAR* Name) {
        const bool Held=Device.IsValid() && Device->IsControlPressed(Name);
        const float Offset=Held ? 3 : 0;
        Box(FSlateRect(Rect.Left,Rect.Top+3,Rect.Right,Rect.Bottom+3),Key,CrankTheme::KeyEdge());
        Box(FSlateRect(Rect.Left,Rect.Top+Offset,Rect.Right,Rect.Bottom+Offset),Key,Held ? CrankTheme::KeyPressed() : CrankTheme::KeyCap(),1);
        const float Inset=(Rect.Right-Rect.Left)*.18f;
        Box(FSlateRect(Rect.Left+Inset,Rect.Top+Inset+Offset,Rect.Right-Inset,Rect.Bottom-Inset+Offset),Face,CrankTheme::HudText(),2);
        Text(Name,CrankImmersive::FLayout::Center(Rect)+FVector2D(0,Offset),CrankTheme::GameKey(),CrankTheme::Ink());
    };
    Button(Layout.B,TEXT("B")); Button(Layout.A,TEXT("A"));
    const auto& D=Layout.Dpad; const float Cell=(D.Right-D.Left)/3;
    Box(FSlateRect(D.Left+Cell,D.Top+3,D.Right-Cell,D.Bottom+3),Pad,CrankTheme::KeyEdge());
    Box(FSlateRect(D.Left,D.Top+Cell+3,D.Right,D.Bottom-Cell+3),Pad,CrankTheme::KeyEdge());
    Box(FSlateRect(D.Left+Cell,D.Top,D.Right-Cell,D.Bottom),Pad,CrankTheme::KeyCap(),1);
    Box(FSlateRect(D.Left,D.Top+Cell,D.Right,D.Bottom-Cell),Pad,CrankTheme::KeyCap(),1);
    // Fill the intersection to remove the two overlapping outline seams.
    Box(FSlateRect(D.Left+Cell+2,D.Top+Cell-1,D.Right-Cell-2,D.Bottom-Cell+1),Fill,CrankTheme::KeyCap(),2);
    struct FDirection {const TCHAR* Name; FVector2D Center,Vector;};
    const FVector2D C=CrankImmersive::FLayout::Center(D);
    for(const auto& Dir:TArray<FDirection>{{TEXT("left"),C+FVector2D(-Cell,0),{-1,0}},{TEXT("right"),C+FVector2D(Cell,0),{1,0}},{TEXT("up"),C+FVector2D(0,-Cell),{0,-1}},{TEXT("down"),C+FVector2D(0,Cell),{0,1}}}) {
        const bool Held=Device.IsValid() && Device->IsControlPressed(Dir.Name);
        if(Held) Box(CrankImmersive::FLayout::Rect(Dir.Center.X-Cell*.42,Dir.Center.Y-Cell*.42,Cell*.84,Cell*.84),Pad,CrankTheme::KeyPressed(),2);
        const FVector2D V=Dir.Vector*8, Side(-Dir.Vector.Y*7,Dir.Vector.X*7);
        FSlateDrawElement::MakeLines(Out,Layer+4,Surface.ToPaintGeometry(),{Dir.Center-V+Side,Dir.Center+V,Dir.Center-V-Side},ESlateDrawEffect::None,CrankTheme::Ink().CopyWithNewOpacity(Alpha),true,3.f);
    }
    const FString Dock=Device.IsValid() && Device->IsCrankDocked() ? TEXT("摇柄已收起") : TEXT("摇柄");
    Text(Dock,{(Layout.Dial.Left+Layout.Dial.Right)*.5,Layout.Dial.Bottom+13},CrankTheme::Utility(),CrankTheme::Ink());
    return SCompoundWidget::OnPaint(Args,G,Clip,Out,Layer+5,Style,Enabled);
}
