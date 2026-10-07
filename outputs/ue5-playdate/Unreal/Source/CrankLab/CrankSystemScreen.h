#pragma once
#include "Widgets/SLeafWidget.h"
class ACrankDevicePawn;
class SCrankSystemScreen : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SCrankSystemScreen) {} SLATE_ARGUMENT(TWeakObjectPtr<ACrankDevicePawn>,Device) SLATE_END_ARGS()
    void Construct(const FArguments& Args) {Device=Args._Device;}
    FVector2D ComputeDesiredSize(float) const override {return FVector2D(400,240);}
    int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
private:
    TWeakObjectPtr<ACrankDevicePawn> Device;
};
