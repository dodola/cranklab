#pragma once
#include "CoreMinimal.h"
#include "Layout/SlateRect.h"

namespace CrankImmersive {
    struct FLayout {
        FSlateRect Screen, Dpad, A, B, Dial;
        bool bSideControls=false;
        static FSlateRect Rect(float X,float Y,float W,float H) {return FSlateRect(X,Y,X+W,Y+H);}
        static FVector2D Center(const FSlateRect& R) {return {(R.Left+R.Right)*.5,(R.Top+R.Bottom)*.5};}
        static bool Contains(const FSlateRect& R,const FVector2D& P) {return P.X>=R.Left && P.X<=R.Right && P.Y>=R.Top && P.Y<=R.Bottom;}
        float ScreenCrankDelta(const FVector2D& Previous,const FVector2D& Current) const {
            return float((Previous.Y-Current.Y)*360/FMath::Max(1.f,Screen.Bottom-Screen.Top));
        }
        FName HitControl(const FVector2D& P) const {
            if(Contains(A,P)) return TEXT("A");
            if(Contains(B,P)) return TEXT("B");
            if(Contains(Dpad,P)) {
                const FVector2D D=P-Center(Dpad);
                const float Cell=(Dpad.Right-Dpad.Left)/3;
                if(FMath::Abs(D.X)<Cell*.22 && FMath::Abs(D.Y)<Cell*.22) return TEXT("dpad");
                if(FMath::Abs(D.X)<=Cell*.5 || FMath::Abs(D.Y)<=Cell*.5)
                    return FMath::Abs(D.X)>FMath::Abs(D.Y) ? (D.X<0 ? TEXT("left") : TEXT("right")) : (D.Y<0 ? TEXT("up") : TEXT("down"));
            }
            return NAME_None;
        }
    };
    inline FLayout Arrange(FVector2D Size) {
        FLayout L;
        const float W=FMath::Max(1.f,float(Size.X)), H=FMath::Max(1.f,float(Size.Y));
        const float Top=60.f;
        constexpr float Gap=16, Bottom=30;
        L.bSideControls=W/H>1.6f;
        float ScreenW,ScreenH;
        if(L.bSideControls) {
            const float LeftWing=FMath::Clamp(W*.16f,152.f,190.f), RightWing=FMath::Clamp(W*.13f,144.f,180.f);
            const float Available=H-Top-Bottom;
            const float Pad=LeftWing, Key=FMath::Clamp(FMath::Min(RightWing*.76f,(Available-48)*.28f),64.f,106.f);
            const float AreaW=W-LeftWing-RightWing-2*Gap;
            ScreenW=FMath::Min(AreaW,Available*5/3); ScreenH=ScreenW*3/5;
            L.Screen=FLayout::Rect(LeftWing+Gap+(AreaW-ScreenW)*.5,Top+(Available-ScreenH)*.5,ScreenW,ScreenH);
            const float Mid=Top+(H-Top-Bottom)*.5;
            L.Dpad=FLayout::Rect(0,Mid-Pad*.5,Pad,Pad);
            const float X=W-RightWing*.5-Key*.5;
            const float Dial=FMath::Min(160.f,FMath::Min(RightWing,FMath::Max(64.f,Available-2*Key-2*Gap-26)));
            const float Cluster=2*Key+2*Gap+Dial+26, Y=Top+FMath::Max(0.f,(Available-Cluster)*.5f);
            L.B=FLayout::Rect(X,Y,Key,Key);
            L.A=FLayout::Rect(X,Y+Key+Gap,Key,Key);
            L.Dial=FLayout::Rect(W-RightWing*.5-Dial*.5,Y+2*Key+2*Gap,Dial,Dial);
        } else {
            const float Key=FMath::Clamp(W*.195f,W<320 ? 64.f : 72.f,112.f);
            const float Pad=FMath::Min(FMath::Clamp(W*.43f,144.f,220.f),W-2*Key-2*Gap);
            const float Dial=FMath::Clamp(W*.36f,128.f,180.f);
            const float Controls=FMath::Max(Pad,Key+Gap+Dial)+Gap;
            ScreenW=FMath::Min(W,(H-Top-Bottom-Controls-Gap)*5/3);
            ScreenW=FMath::Max(1.f,ScreenW); ScreenH=ScreenW*3/5;
            L.Screen=FLayout::Rect((W-ScreenW)*.5,Top,ScreenW,ScreenH);
            const float Available=H-Bottom-(L.Screen.Bottom+Gap);
            const float Y=L.Screen.Bottom+Gap+FMath::Max(0.f,(Available-Controls)*.38f);
            L.Dpad=FLayout::Rect(0,Y+(Controls-Pad)*.5,Pad,Pad);
            L.B=FLayout::Rect(W-2*Key-Gap,Y,Key,Key);
            L.A=FLayout::Rect(W-Key,Y,Key,Key);
            L.Dial=FLayout::Rect(W-(2*Key+Gap)*.5-Dial*.5,Y+Key+Gap,Dial,Dial);
        }
        return L;
    }
}
