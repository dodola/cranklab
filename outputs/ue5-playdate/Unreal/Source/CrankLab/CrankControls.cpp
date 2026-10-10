#include "CrankControls.h"
#include "CrankTheme.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "InputCoreTypes.h"

int32 SCrankGlyph::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const {
    const FLinearColor Color=Tint.Get()*Style.GetColorAndOpacityTint();
    const FVector2D Scale=FVector2D(G.GetLocalSize())/22;
    auto Line=[&](std::initializer_list<FVector2D> Points) { TArray<FVector2D> P; for(auto V:Points) P.Add(V*Scale); FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,1.5f); };
    switch(Icon) {
    case ECrankGlyph::Library: Line({{3,5},{19,5},{19,17},{3,17},{3,5}}); Line({{7,9},{7,13}}); Line({{5,11},{9,11}}); Line({{14,10},{14,12}}); Line({{17,10},{17,12}}); break;
    case ECrankGlyph::Expand: Line({{8,3},{3,3},{3,8}}); Line({{14,3},{19,3},{19,8}}); Line({{3,14},{3,19},{8,19}}); Line({{14,19},{19,19},{19,14}}); break;
    case ECrankGlyph::Reset: Line({{5,8},{8,4},{15,4},{19,9},{17,16},{11,19},{5,15}}); Line({{5,3},{5,8},{10,8}}); break;
    case ECrankGlyph::Settings:
        for(int I=0;I<8;I++) { const float A=I*PI/4; const FVector2D V(FMath::Cos(A),FMath::Sin(A)); Line({FVector2D(11,11)+V*7,FVector2D(11,11)+V*10}); }
        Line({{8,7},{14,7},{17,10},{17,13},{14,16},{8,16},{5,13},{5,10},{8,7}}); Line({{9,9},{13,9},{13,13},{9,13},{9,9}}); break;
    case ECrankGlyph::Crank: Line({{5,15},{11,15},{11,6},{17,6}}); Line({{15,3},{19,3},{19,9},{15,9},{15,3}}); break;
    case ECrankGlyph::Screen: Line({{2,4},{20,4},{20,16},{2,16},{2,4}}); Line({{7,20},{15,20}}); Line({{11,16},{11,20}}); break;
    case ECrankGlyph::Menu: Line({{4,6},{18,6}}); Line({{4,11},{18,11}}); Line({{4,16},{18,16}}); break;
    case ECrankGlyph::Lock: Line({{6,10},{6,6},{8,3},{14,3},{16,6},{16,10}}); Line({{4,10},{18,10},{18,19},{4,19},{4,10}}); Line({{11,13},{11,16}}); break;
    }
    return Layer;
}
void SCrankAction::Construct(const FArguments& Args) {
    Selected=Args._Selected; Device=Args._Device;
    auto Content=SNew(SHorizontalBox);
    if(Args._Cover) {
        CoverBrush.SetResourceObject(Args._Cover); CoverBrush.ImageSize=FVector2D(Args._Cover->GetSizeX(),Args._Cover->GetSizeY()); CoverBrush.DrawAs=ESlateBrushDrawType::Image;
        Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
            [SNew(SBox).WidthOverride(80).HeightOverride(48)[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(&CoverBrush)]]];
    }
    else if(Args._ShowIcon) Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
        [SNew(SCrankGlyph).Icon(Args._Icon).Tint_Lambda([this]{return Button.IsValid() && Button->IsPressed() ? CrankTheme::Ink() : CrankTheme::Accent();})];
    auto Labels=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(Args._Label).Font(CrankTheme::Body()).AutoWrapText(Args._WrapLabel)
         .ColorAndOpacity_Lambda([this]{return Button.IsValid() && Button->IsPressed() ? CrankTheme::Ink() : CrankTheme::HudText();})];
    if(!Args._Subtitle.IsEmpty()) Labels->AddSlot().AutoHeight().Padding(0,4,0,0)
        [SNew(STextBlock).Text(Args._Subtitle).Font(CrankTheme::Utility()).AutoWrapText(true)
         .ColorAndOpacity_Lambda([this]{return Button.IsValid() && Button->IsPressed() ? CrankTheme::Ink() : CrankTheme::HudMuted();})];
    Content->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Labels];
    Content->SetVisibility(TAttribute<EVisibility>::CreateLambda([Compact=Args._Compact]{return Compact.Get() ? EVisibility::Collapsed : EVisibility::Visible;}));
    // Compact actions have their own centered icon slot: the text row's padding
    // and fill-width allocation must not squeeze or offset the glyph.
    auto ButtonContent=SNew(SOverlay)+SOverlay::Slot()[Content]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(22).HeightOverride(22)
         .Visibility_Lambda([Compact=Args._Compact]{return Compact.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;})
         [SNew(SCrankGlyph).Icon(Args._Icon).Tint_Lambda([this]{return Button.IsValid() && Button->IsPressed() ? CrankTheme::Ink() : CrankTheme::Accent();})]];
    ChildSlot
    [SNew(SBorder).BorderImage(&CrankTheme::FocusFrame()).Padding(1)
     .BorderBackgroundColor_Lambda([this]{return CrankTheme::Accent().CopyWithNewOpacity(Selected.Get() ? .95f : Highlight*.7f);})
     [SNew(SBox).MinDesiredHeight(CrankTheme::Target)
      .WidthOverride_Lambda([Compact=Args._Compact]{return Compact.Get() ? TOptional<float>(CrankTheme::Target) : TOptional<float>();})
      .HeightOverride_Lambda([Compact=Args._Compact]{return Compact.Get() ? TOptional<float>(CrankTheme::Target) : TOptional<float>();})
      [SAssignNew(Button,SButton).ButtonStyle(&CrankTheme::Button()).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
       .ContentPadding_Lambda([Compact=Args._Compact]{return Compact.Get() ? FMargin(0) : FMargin(12,8);})
       .ToolTipText(Args._Label).OnClicked(Args._OnClicked)
       .OnPressed_Lambda([this]{if(Device.IsValid()) Device->PulseHaptic();})[ButtonContent]]]];
#if WITH_ACCESSIBILITY
    Button->SetAccessibleBehavior(EAccessibleBehavior::Custom,Args._Label);
#endif
}
FReply SCrankAction::OnFocusReceived(const FGeometry&,const FFocusEvent& Event) {
    return FReply::Handled().SetUserFocus(Button.ToSharedRef(),Event.GetCause());
}
void SCrankAction::Tick(const FGeometry& G,double Time,float Dt) {
    SCompoundWidget::Tick(G,Time,Dt);
    const float A=Device.IsValid() && Device->IsReducedMotion() ? 1.f : CrankView::SmoothAlpha(20,Dt);
    Highlight=FMath::Lerp(Highlight,Button->IsHovered() || Button->HasKeyboardFocus() || Selected.Get() ? 1.f : 0.f,A);
    PressOffset=FMath::Lerp(PressOffset,Button->IsPressed() ? 1.5f : 0.f,A);
    Button->SetRenderTransform(FSlateRenderTransform(FVector2D(0,PressOffset)));
}
int32 SCrankAction::OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const {
    const int32 Top=SCompoundWidget::OnPaint(Args,G,Clip,Out,Layer,Style,Enabled);
    if(Highlight>.02f) {
        const FVector2D Size=G.GetLocalSize();
        FSlateDrawElement::MakeLines(Out,Top+1,G.ToPaintGeometry(),TArray<FVector2D>{{10,Size.Y-3},{Size.X-10,Size.Y-3}},ESlateDrawEffect::None,
            CrankTheme::Accent().CopyWithNewOpacity(Highlight)*Style.GetColorAndOpacityTint(),true,2.f);
    }
    return Top+1;
}
void SCrankDial::Construct(const FArguments& Args) {
    Device=Args._Device; SetCursor(EMouseCursor::Hand);
    SetToolTipText(FText::FromString(TEXT("拖动圆盘转动摇柄；方向键每次转动 15°，Q/E 可持续转动")));
}
int32 SCrankDial::OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const {
    const FVector2D Size=G.GetLocalSize(), Center=Size*.5;
    const float R=FMath::Min(Size.X,Size.Y)*.46f;
    const bool Docked=!Device.IsValid() || Device->IsCrankDocked();
    const FLinearColor Accent=(Docked ? CrankTheme::HudMuted() : CrankTheme::Accent())*Style.GetColorAndOpacityTint();
    auto Ring=[&](float Radius,float Start,float End,FLinearColor Color,float Thickness) {
        TArray<FVector2D> P;
        for(int I=0;I<=72;I++) { const float A=FMath::Lerp(Start,End,I/72.f); P.Add(Center+FVector2D(FMath::Sin(A),-FMath::Cos(A))*Radius); }
        FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,Thickness);
    };
    Ring(R,0,2*PI,CrankTheme::HudFrame()*Style.GetColorAndOpacityTint(),1.f);
    Ring(R*.76f,0,2*PI,CrankTheme::HudRaised()*Style.GetColorAndOpacityTint(),1.f);
    for(int I=0;I<36;I++) {
        const float A=I*PI/18; const FVector2D D(FMath::Sin(A),-FMath::Cos(A));
        FSlateDrawElement::MakeLines(Out,Layer,G.ToPaintGeometry(),{Center+D*R*.84f,Center+D*R*(I%3 ? .89f : .95f)},ESlateDrawEffect::None,
            I%3 ? CrankTheme::HudFrame() : Accent,true,1.f);
    }
    const float Angle=Device.IsValid() ? FMath::Fmod(Device->GetCrankAngle(),360.f) : 0;
    const float A=FMath::DegreesToRadians(Angle);
    Ring(R*.99f,A-.5f,A,Accent.CopyWithNewOpacity(.16f),7.f);
    Ring(R*.99f,A-.5f,A,Accent,2.f);
    const FVector2D D(FMath::Sin(A),-FMath::Cos(A));
    FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),{Center+D*R*.3f,Center+D*R*.72f},ESlateDrawEffect::None,Accent,true,2.5f);
    Ring(4,0,2*PI,Accent,2.f);
    if(HasKeyboardFocus() || HasMouseCapture()) Ring(R+3,0,2*PI,Accent,1.5f);
    return Layer+1;
}
FReply SCrankDial::OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) {
    if(!Device.IsValid() || Device->IsCrankDocked() || Device->IsOverlayOpen() || Pointer!=-1 || (!E.IsTouchEvent() && E.GetEffectingButton()!=EKeys::LeftMouseButton)) return FReply::Unhandled();
    Pointer=E.GetPointerIndex(); Previous=G.AbsoluteToLocal(E.GetScreenSpacePosition());
    return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this),EFocusCause::Mouse);
}
FReply SCrankDial::OnMouseMove(const FGeometry& G,const FPointerEvent& E) {
    if(!HasMouseCapture() || E.GetPointerIndex()!=Pointer || !Device.IsValid()) return FReply::Unhandled();
    const FVector2D Current=G.AbsoluteToLocal(E.GetScreenSpacePosition()), Center=FVector2D(G.GetLocalSize())*.5;
    const FVector2D A=Previous-Center, B=Current-Center;
    if(A.SizeSquared()>100 && B.SizeSquared()>100) Device->TurnCrank(FMath::FindDeltaAngleDegrees(
        FMath::RadiansToDegrees(FMath::Atan2(A.X,-A.Y)),FMath::RadiansToDegrees(FMath::Atan2(B.X,-B.Y))));
    Previous=Current; return FReply::Handled();
}
FReply SCrankDial::OnMouseButtonUp(const FGeometry&,const FPointerEvent& E) {
    if(E.GetPointerIndex()!=Pointer) return FReply::Unhandled(); Pointer=-1;
    return FReply::Handled().ReleaseMouseCapture();
}
FReply SCrankDial::OnKeyDown(const FGeometry&,const FKeyEvent& E) {
    if(Device.IsValid() && (E.GetKey()==EKeys::Left || E.GetKey()==EKeys::Right)) {
        Device->TurnCrank(E.GetKey()==EKeys::Right ? 15 : -15); return FReply::Handled();
    }
    return FReply::Unhandled();
}
