#pragma once
#include "Widgets/SCompoundWidget.h"
#include "CrankImmersiveLayout.h"
class ACrankDevicePawn;
class SConstraintCanvas;

// The virtual gamepad uses the same independent pointer polling as the mesh.
// Slate owns utility buttons and the shared crank dial, without duplicating input.
class SCrankImmersive : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SCrankImmersive) {} SLATE_ARGUMENT(TWeakObjectPtr<ACrankDevicePawn>,Device) SLATE_END_ARGS()
    void Construct(const FArguments&);
    void Tick(const FGeometry&,double,float) override;
    int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
    FName HitControl(const FVector2D& ViewportPoint) const;
    bool ScreenPoint(const FVector2D& ViewportPoint,FVector2D& Pixel) const;
    float ScreenCrankDelta(const FVector2D& PreviousViewportPoint,const FVector2D& CurrentViewportPoint) const;
    FVector2D ViewportPoint(const FVector2D& Local) const;
    const CrankImmersive::FLayout& GetLayout() const {return Layout;}
private:
    FVector2D LocalPoint(const FVector2D& ViewportPoint) const;
    TWeakObjectPtr<ACrankDevicePawn> Device;
    TSharedPtr<SConstraintCanvas> Canvas;
    CrankImmersive::FLayout Layout;
    FVector2D LastSize=FVector2D::ZeroVector;
    mutable FSlateBrush ScreenBrush;
};
