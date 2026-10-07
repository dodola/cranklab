#pragma once
#include "CoreMinimal.h"

// Shared camera/gesture math; independent of the renderer and platform input.
namespace CrankView {
    inline float FitDistance(const FBox& Bounds,const FVector& Target,const FVector& EyeDirection,float Aspect,float Fov,float Occupancy) {
        const FVector Direction=EyeDirection.GetSafeNormal();
        const FMatrix Basis=FRotationMatrix::MakeFromX(-Direction);
        const FVector Right=Basis.GetUnitAxis(EAxis::Y), Up=Basis.GetUnitAxis(EAxis::Z);
        const float Horizontal=FMath::Tan(FMath::DegreesToRadians(Fov*.5f))*Occupancy;
        const float Vertical=Horizontal/FMath::Max(Aspect,.1f);
        float Distance=1;
        for(int Corner=0;Corner<8;Corner++) {
            const FVector P(FMath::Lerp(Bounds.Min.X,Bounds.Max.X,float(Corner&1)),
                FMath::Lerp(Bounds.Min.Y,Bounds.Max.Y,float((Corner>>1)&1)),
                FMath::Lerp(Bounds.Min.Z,Bounds.Max.Z,float((Corner>>2)&1)));
            const FVector Offset=P-Target;
            const float Depth=FVector::DotProduct(Offset,Direction);
            Distance=FMath::Max(Distance,Depth+FMath::Max(FMath::Abs(FVector::DotProduct(Offset,Right))/Horizontal,
                FMath::Abs(FVector::DotProduct(Offset,Up))/Vertical));
        }
        return Distance;
    }
    inline float SmoothAlpha(float Speed,float Dt) { return 1-FMath::Exp(-Speed*FMath::Clamp(Dt,0.f,.1f)); }
    inline float DeadZone(float Value,float Threshold) {
        return FMath::Abs(Value)<=Threshold ? 0.f : FMath::Sign(Value)*(FMath::Abs(Value)-Threshold);
    }
    struct FDoubleTap {
        double Time=-1;
        FVector2D Position=FVector2D::ZeroVector;
        void Cancel() { Time=-1; }
        bool Tap(double Now,const FVector2D& Point,float Scale) {
            const bool Matched=Time>=0 && Now-Time<=.32 && FVector2D::DistSquared(Point,Position)<=FMath::Square(24*Scale);
            Time=Matched ? -1 : Now; Position=Point; return Matched;
        }
    };
}
