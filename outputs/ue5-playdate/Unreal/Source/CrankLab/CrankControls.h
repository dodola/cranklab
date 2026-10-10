#pragma once
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "CrankDevicePawn.h"
class SButton;
enum class ECrankGlyph { Library, Settings, Expand, Reset, Crank, Screen, Menu, Lock };

class SCrankGlyph : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SCrankGlyph) : _Icon(ECrankGlyph::Library), _Tint(FLinearColor::White) {}
        SLATE_ARGUMENT(ECrankGlyph,Icon) SLATE_ATTRIBUTE(FLinearColor,Tint)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Icon=Args._Icon; Tint=Args._Tint; }
    FVector2D ComputeDesiredSize(float) const override { return {22,22}; }
    int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
private:
    ECrankGlyph Icon;
    TAttribute<FLinearColor> Tint;
};
class SCrankAction : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SCrankAction) : _Selected(false), _Compact(false), _Cover(nullptr), _Icon(ECrankGlyph::Library), _ShowIcon(true), _WrapLabel(true) {}
        SLATE_ATTRIBUTE(FText,Label) SLATE_ATTRIBUTE(bool,Selected) SLATE_ATTRIBUTE(bool,Compact)
        SLATE_ARGUMENT(FText,Subtitle) SLATE_ARGUMENT(UTexture2D*,Cover)
        SLATE_ARGUMENT(ECrankGlyph,Icon) SLATE_ARGUMENT(bool,ShowIcon) SLATE_ARGUMENT(bool,WrapLabel)
        SLATE_ARGUMENT(TWeakObjectPtr<ACrankDevicePawn>,Device)
        SLATE_EVENT(FOnClicked,OnClicked)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnFocusReceived(const FGeometry&,const FFocusEvent&) override;
    void Tick(const FGeometry&,double,float) override;
    int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
private:
    TAttribute<bool> Selected;
    TSharedPtr<SButton> Button;
    TWeakObjectPtr<ACrankDevicePawn> Device;
    float Highlight=0, PressOffset=0;
    FSlateBrush CoverBrush;
};
class SCrankDial : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SCrankDial) {} SLATE_ARGUMENT(TWeakObjectPtr<ACrankDevicePawn>,Device) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    bool SupportsKeyboardFocus() const override { return true; }
    FVector2D ComputeDesiredSize(float) const override { return {112,112}; }
    int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
    FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    FReply OnMouseMove(const FGeometry&,const FPointerEvent&) override;
    FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override;
    FReply OnTouchStarted(const FGeometry& G,const FPointerEvent& E) override { return OnMouseButtonDown(G,E); }
    FReply OnTouchMoved(const FGeometry& G,const FPointerEvent& E) override { return OnMouseMove(G,E); }
    FReply OnTouchEnded(const FGeometry& G,const FPointerEvent& E) override { return OnMouseButtonUp(G,E); }
    FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override;
    void OnMouseCaptureLost(const FCaptureLostEvent&) override { Pointer=-1; }
private:
    TWeakObjectPtr<ACrankDevicePawn> Device;
    FVector2D Previous;
    int32 Pointer=-1;
};
