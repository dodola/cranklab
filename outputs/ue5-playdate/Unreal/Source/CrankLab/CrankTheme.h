#pragma once
#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
namespace CrankTheme {
    inline FLinearColor Ink() { return FLinearColor::FromSRGBColor(FColor(35,35,35)); }
    inline FLinearColor Backdrop() { return FLinearColor::FromSRGBColor(FColor(232,232,232)); }
    inline FLinearColor Accent() { return FLinearColor::FromSRGBColor(FColor(255,198,0)); }
    inline FLinearColor Error() { return FLinearColor::FromSRGBColor(FColor(161,38,30)); }
    inline FLinearColor HudSurface() { return FLinearColor::FromSRGBColor(FColor(37,44,49)).CopyWithNewOpacity(.97f); }
    inline FLinearColor HudRaised() { return FLinearColor::FromSRGBColor(FColor(53,62,69)); }
    inline FLinearColor HudText() { return FLinearColor::FromSRGBColor(FColor(244,242,234)); }
    inline FLinearColor HudMuted() { return FLinearColor::FromSRGBColor(FColor(188,195,200)); }
    inline FLinearColor HudFrame() { return FLinearColor::FromSRGBColor(FColor(92,105,113)); }
    inline FLinearColor HudError() { return FLinearColor::FromSRGBColor(FColor(255,163,145)); }
    inline FLinearColor KeyCap() { return FLinearColor::FromSRGBColor(FColor(255,218,104)); }
    inline FLinearColor KeyPressed() { return FLinearColor::FromSRGBColor(FColor(236,177,0)); }
    inline FLinearColor KeyEdge() { return FLinearColor::FromSRGBColor(FColor(171,123,0)); }
    inline FLinearColor LcdFrame() { return FLinearColor::FromSRGBColor(FColor(48,46,41)); }
    inline FLinearColor LcdBackground() { return FLinearColor::FromSRGBColor(FColor(212,210,201)); }
    inline FSlateFontInfo Body() { return FCoreStyle::GetDefaultFontStyle("Regular",16); }
    inline FSlateFontInfo Label() { return FCoreStyle::GetDefaultFontStyle("Bold",16); }
    inline FSlateFontInfo Title() { return FCoreStyle::GetDefaultFontStyle("Bold",22); }
    inline FSlateFontInfo Utility() { return FCoreStyle::GetDefaultFontStyle("Regular",12); }
    inline FSlateFontInfo GameKey() { return FCoreStyle::GetDefaultFontStyle("Bold",40); }
    constexpr float Target = 48, Gap = 12, Edge = 20;
    inline const FButtonStyle& Button() {
        static FButtonStyle Style=FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
            .SetNormal(FSlateRoundedBoxBrush(HudSurface(),9.f,HudFrame(),1.f)).SetHovered(FSlateRoundedBoxBrush(HudRaised(),9.f,Accent().CopyWithNewOpacity(.65f),1.f))
            .SetPressed(FSlateRoundedBoxBrush(Accent(),9.f)).SetDisabled(FSlateRoundedBoxBrush(HudSurface().CopyWithNewOpacity(.45f),9.f));
        return Style;
    }
    inline const FSlateBrush& Panel() { static FSlateRoundedBoxBrush Brush(HudSurface(),18.f,HudFrame(),1.f); return Brush; }
    inline const FProgressBarStyle& Progress() {
        static FProgressBarStyle Style=FProgressBarStyle()
            .SetBackgroundImage(FSlateRoundedBoxBrush(HudRaised(),3.f))
            .SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,3.f))
            .SetMarqueeImage(FSlateRoundedBoxBrush(FLinearColor::White,3.f));
        return Style;
    }
    inline const FSlateBrush& FocusFrame() { static FSlateRoundedBoxBrush Brush(FLinearColor::White,10.f); return Brush; }
    inline const FCheckBoxStyle& Toggle() {
        static FCheckBoxStyle Style=FCheckBoxStyle().SetCheckBoxType(ESlateCheckBoxType::ToggleButton).SetPadding(FMargin(12,10))
            .SetUncheckedImage(FSlateRoundedBoxBrush(HudRaised(),10.f,HudFrame(),1.f))
            .SetUncheckedHoveredImage(FSlateRoundedBoxBrush(HudRaised(),10.f,Accent().CopyWithNewOpacity(.6f),1.f))
            .SetUncheckedPressedImage(FSlateRoundedBoxBrush(HudRaised(),10.f,Accent(),2.f))
            .SetCheckedImage(FSlateRoundedBoxBrush(HudSurface(),10.f,Accent(),1.f))
            .SetCheckedHoveredImage(FSlateRoundedBoxBrush(HudRaised(),10.f,Accent(),1.f))
            .SetCheckedPressedImage(FSlateRoundedBoxBrush(HudRaised(),10.f,Accent(),2.f));
        return Style;
    }
    inline const FSliderStyle& Slider() {
        static FSliderStyle Style=FSliderStyle(FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider"))
            .SetNormalBarImage(FSlateRoundedBoxBrush(FLinearColor::White,2.f,FVector2f(1,3)))
            .SetHoveredBarImage(FSlateRoundedBoxBrush(FLinearColor::White,2.f,FVector2f(1,3)))
            .SetDisabledBarImage(FSlateRoundedBoxBrush(FLinearColor::White.CopyWithNewOpacity(.4f),2.f,FVector2f(1,3)))
            .SetNormalThumbImage(FSlateRoundedBoxBrush(FLinearColor::White,8.f,FVector2f(16,16)))
            .SetHoveredThumbImage(FSlateRoundedBoxBrush(FLinearColor::White,8.f,FVector2f(16,16)))
            .SetBarThickness(3.f);
        return Style;
    }
    inline const FEditableTextBoxStyle& Input() {
        static FEditableTextBoxStyle Style=FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
            .SetBackgroundImageNormal(FSlateRoundedBoxBrush(HudRaised(),8.f)).SetBackgroundImageHovered(FSlateRoundedBoxBrush(HudRaised(),8.f,HudFrame(),1.f))
            .SetBackgroundImageFocused(FSlateRoundedBoxBrush(HudRaised(),8.f,Accent(),1.f)).SetForegroundColor(HudText())
            .SetFocusedForegroundColor(HudText()).SetReadOnlyForegroundColor(HudMuted());
        return Style;
    }
}
