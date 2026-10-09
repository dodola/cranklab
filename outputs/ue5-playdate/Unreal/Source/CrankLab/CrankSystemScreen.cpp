#include "CrankSystemScreen.h"
#include "CrankDevicePawn.h"
#include "CrankRuntimeComponent.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"

int32 SCrankSystemScreen::OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,FSlateWindowElementList& E,int32 L,const FWidgetStyle& Style,bool Enabled) const {
    if(!Device.IsValid()) return L;
    static FSlateColorBrush White(FLinearColor::White);
    const int32 ImageLayer=L+1, PanelLayer=L+2, TextLayer=L+3;
    int32 ShapeLayer=L;
    auto Rect=[&](FVector2D P,FVector2D S,bool Black) {
        FSlateDrawElement::MakeBox(E,ShapeLayer,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),&White,ESlateDrawEffect::None,Black ? FLinearColor::Black : FLinearColor::White);
    };
    auto Text=[&](const FString& T,FVector2D P,int Size=16,bool Black=true,bool Bold=false) {
        FSlateDrawElement::MakeText(E,TextLayer,G.ToPaintGeometry(FVector2D(400,30),FSlateLayoutTransform(P)),T,
            FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular",Size),ESlateDrawEffect::None,Black ? FLinearColor::Black : FLinearColor::White);
    };
    Rect({0,0},{400,240},false);
    // Resource batching may reorder boxes within a layer. Keep images below opaque panels and text.
    ShapeLayer=PanelLayer;
    const auto Mode=Device->GetSystemMode();
    if(Mode==ECrankSystemMode::Boot || Mode==ECrankSystemMode::Loading) {
        Text(TEXT("Playdate"),{136,72},28,true,true);
        Text(Mode==ECrankSystemMode::Boot ? TEXT("正在启动掌机") : TEXT("正在载入游戏"),{130,116},16);
        if(Mode==ECrankSystemMode::Loading) Text(Device->GetGameTitle().Left(24),{24,163},14);
        for(int I=0;I<5;I++) Rect({float(163+I*16),144},{8,8},I<=int(FPlatformTime::Seconds()*5)%5);
        return TextLayer;
    }
    if(Mode==ECrankSystemMode::Locked) {
        Text(FDateTime::Now().ToString(TEXT("%H:%M")),{102,62},48,true,true);
        Text(TEXT("已锁定"),{173,132},16);
        Text(TEXT("按电源键或点按屏幕继续"),{101,190},14);
        return TextLayer;
    }
    const bool Menu=Mode==ECrankSystemMode::Menu;
    const int X=Menu ? 200 : 0, Width=400-X;
    if(!Menu) {
        const auto Games=Device->GetGameDirectories(); const int SelectedGame=Device->GetSystemSelection();
        UTexture2D* Cover=Games.IsValidIndex(SelectedGame) ? Device->GetGameCover(Games[SelectedGame]) : nullptr;
        if(Cover) {
            Text(Device->GetTitleForGame(Games[SelectedGame]).Left(34),{12,8},16,true,true);
            Rect({12,32},{376,1},true);
            const FVector2D DrawSize=FVector2D(Cover->GetSizeX(),Cover->GetSizeY())*FMath::Min(350.f/Cover->GetSizeX(),166.f/Cover->GetSizeY());
            FSlateBrush Card; Card.SetResourceObject(Cover); Card.ImageSize=FVector2D(Cover->GetSizeX(),Cover->GetSizeY()); Card.DrawAs=ESlateBrushDrawType::Image;
            FSlateDrawElement::MakeBox(E,ImageLayer,G.ToPaintGeometry(DrawSize,FSlateLayoutTransform((FVector2D(400,166)-DrawSize)*.5+FVector2D(0,40))),&Card);
            Rect({12,219},{376,1},true);
            Text(FString::Printf(TEXT("上下切换  %d/%d     A 启动"),SelectedGame+1,Games.Num()),{12,224},11);
            return TextLayer;
        }
    }
    if(Menu) {
        UTexture2D* Texture=Device->Runtime->bHasMenuImage ? Device->Runtime->MenuImageTexture.Get() : Device->Runtime->ScreenTexture.Get();
        if(Texture) {
            FSlateBrush Image; Image.SetResourceObject(Texture); Image.ImageSize=FVector2D(400,240); Image.DrawAs=ESlateBrushDrawType::Image;
            const int Offset=Device->Runtime->bHasMenuImage ? Device->Runtime->MenuImageOffset : 0;
            E.PushClip(FSlateClippingZone(G.ToPaintGeometry(FVector2D(200,240),FSlateLayoutTransform())));
            FSlateDrawElement::MakeBox(E,ImageLayer,G.ToPaintGeometry(FVector2D(400,240),FSlateLayoutTransform(FVector2D(-Offset,0))),&Image);
            E.PopClip();
        }
        Rect({200,0},{200,240},false); Rect({199,0},{1,240},true);
    }
    Text(Menu ? TEXT("系统菜单") : TEXT("我的掌机"),{float(X+12),8},18,true,true);
    Rect({float(X+10),32},{float(Width-20),1},true);
    const auto Rows=Device->GetSystemRows();
    const int Selected=Device->GetSystemSelection(), Visible=Menu ? 7 : 5;
    const int First=FMath::Clamp(Selected-(Visible-1)/2,0,FMath::Max(0,Rows.Num()-Visible));
    const int Height=Menu ? 26 : 33;
    E.PushClip(FSlateClippingZone(G.ToPaintGeometry(FVector2D(Width-16,183),FSlateLayoutTransform(FVector2D(X+8,35)))));
    for(int I=First;I<Rows.Num() && I<First+Visible;I++) {
        const int Y=38+(I-First)*Height; const bool Focus=I==Selected;
        if(Focus) Rect({float(X+8),float(Y)},{float(Width-16),float(Height-2)},true);
        Text(Rows[I],{float(X+14),float(Y+3)},Menu ? 14 : 16,!Focus,Focus);
    }
    E.PopClip();
    Rect({float(X+10),222},{float(Width-20),1},true);
    Text(Menu ? TEXT("A 选择  B 返回") : TEXT("A 启动   MENU 游戏库"),{float(X+12),225},11);
    return TextLayer;
}
