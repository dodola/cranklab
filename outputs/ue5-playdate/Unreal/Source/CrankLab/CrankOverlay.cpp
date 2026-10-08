#include "CrankOverlay.h"
#include "CrankTheme.h"
#include "CrankTranslation.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"

TSharedRef<SWidget> SCrankOverlay::Action(const FString& Label,TFunction<FReply()> Callback,bool AllowBusy,ECrankGlyph Icon,TFunction<bool()> Selected,bool ShowIcon,bool Toolbar) {
    return SNew(SCrankAction).Device(Device).Label(FText::FromString(Label)).Icon(Icon).ShowIcon(ShowIcon)
        .Compact_Lambda([this,Toolbar]{return Toolbar && GetCachedGeometry().GetLocalSize().X<740;})
        .Selected_Lambda([Selected=MoveTemp(Selected)]{return Selected && Selected();})
        .OnClicked_Lambda(MoveTemp(Callback))
        .IsEnabled_Lambda([this,AllowBusy]{return AllowBusy || !Device.IsValid() || (!Device->IsImportBusy() && !Device->IsGameSwitching());});
}
static TSharedRef<STextBlock> HudText(const FString& Text,bool Title=false) {
    return SNew(STextBlock).Text(FText::FromString(Text)).Font(Title ? CrankTheme::Title() : CrankTheme::Body())
        .ColorAndOpacity(CrankTheme::HudText()).AutoWrapText(true);
}
TSharedRef<SWidget> SCrankOverlay::BuildTranslationModel() {
    auto Service=[this]{return Device.IsValid()?Device->GetTranslation():nullptr;};
    return SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
     [SNew(SHorizontalBox)
      +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)
      [SAssignNew(LightModelFocus,SCrankAction).Device(Device).ShowIcon(false).Label(FText::FromString(TEXT("轻量 · 440 MiB")))
       .Selected_Lambda([Service]{auto S=Service();return S && S->IsLightModel();})
       .IsEnabled_Lambda([Service]{auto S=Service();return S && !S->IsModelBusy();})
       .OnClicked_Lambda([Service]{if(auto S=Service())S->SelectModel(true);return FReply::Handled();})]
      +SHorizontalBox::Slot().FillWidth(1)
      [SAssignNew(Q4ModelFocus,SCrankAction).Device(Device).ShowIcon(false).Label(FText::FromString(TEXT("Q4 · 1.06 GiB")))
       .Selected_Lambda([Service]{auto S=Service();return S && !S->IsLightModel();})
       .IsEnabled_Lambda([Service]{auto S=Service();return S && !S->IsModelBusy();})
       .OnClicked_Lambda([Service]{if(auto S=Service())S->SelectModel(false);return FReply::Handled();})]]
     +SVerticalBox::Slot().AutoHeight()
     [SNew(SBox).MinDesiredHeight(32)
      [SNew(STextBlock).Text_Lambda([Service]{auto S=Service();return FText::FromString(S?S->ModelMessage():FString());})
       .Font(CrankTheme::Utility()).ColorAndOpacity_Lambda([Service]{auto S=Service();return S && S->HasModelError()?CrankTheme::HudError():CrankTheme::HudMuted();}).AutoWrapText(true)]]
     +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
     [SNew(SBox).HeightOverride(6).Visibility_Lambda([Service]{auto S=Service();return S && S->IsModelBusy()?EVisibility::Visible:EVisibility::Collapsed;})
      [SNew(SProgressBar).Style(&CrankTheme::Progress()).Percent_Lambda([Service]{auto S=Service();return S?S->ModelProgress():TOptional<float>();}).FillColorAndOpacity(CrankTheme::Accent())]]
     +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)
     [SNew(SBox).Visibility_Lambda([Service]{auto S=Service();return S && !S->HasModel()?EVisibility::Visible:EVisibility::Collapsed;})
      [SAssignNew(ModelDownloadFocus,SCrankAction).Device(Device).ShowIcon(false)
       .Label_Lambda([Service]{auto S=Service();return FText::FromString(S && S->IsModelBusy()?TEXT("取消模型下载"):S && S->HasModelError()?TEXT("重试模型下载"):TEXT("下载翻译模型"));})
       .OnClicked_Lambda([Service]{if(auto S=Service()){if(S->IsModelBusy())S->CancelModelDownload();else S->DownloadModel();}return FReply::Handled();})]];
}
TSharedRef<SWidget> SCrankOverlay::BuildSettings() {
    auto Content=SNew(SVerticalBox);
    auto Toggle=[&](const FString& Title,const FString& Hint,TFunction<bool()> Value,TFunction<void(bool)> Change) {
        TSharedRef<SCheckBox> Check=SNew(SCheckBox).Style(&CrankTheme::Toggle())
          .IsChecked_Lambda([Value]{return Value() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
          .OnCheckStateChanged_Lambda([this,Change](ECheckBoxState State){Change(State==ECheckBoxState::Checked); if(Device.IsValid()) Device->PulseHaptic();})
          [SNew(SVerticalBox)
           +SVerticalBox::Slot().AutoHeight()
           [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[HudText(Title)]
            +SHorizontalBox::Slot().AutoWidth().Padding(12,0,0,0)
            [SNew(STextBlock).Text_Lambda([Value]{return FText::FromString(Value() ? TEXT("开启") : TEXT("关闭"));})
             .Font(CrankTheme::Utility()).ColorAndOpacity_Lambda([Value]{return Value() ? CrankTheme::Accent() : CrankTheme::HudMuted();})]]
           +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
           [SNew(STextBlock).Text(FText::FromString(Hint)).Font(CrankTheme::Utility()).ColorAndOpacity(CrankTheme::HudMuted()).AutoWrapText(true)]];
        if(!SettingsFocus) SettingsFocus=Check;
        Content->AddSlot().AutoHeight().Padding(0,0,0,12)[SNew(SBox).MinDesiredHeight(CrankTheme::Target)[Check]];
    };
    Toggle(TEXT("沉浸式模式"),TEXT("放大屏幕与按键；屏幕上下滑动可转动摇柄"),[this]{return Device.IsValid() && Device->IsImmersive();},
        [this](bool V){if(Device.IsValid()) Device->SetImmersive(V);});
    Toggle(TEXT("摇柄收起"),TEXT("收起时停用摇柄；关闭此项即可展开"),[this]{return Device.IsValid() && Device->IsCrankDocked();},
        [this](bool V){if(Device.IsValid() && V!=Device->IsCrankDocked()) Device->ToggleCrankDocked();});
    Toggle(TEXT("视角跟随"),TEXT("在 3D 模式中跟随手机倾斜"),[this]{return Device.IsValid() && Device->IsMotionFollowEnabled();},
        [this](bool V){if(Device.IsValid()) Device->SetMotionFollow(V);});
    Content->AddSlot().AutoHeight().Padding(0,0,0,18)
    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() && Device->IsImmersive() ? TEXT("沉浸式模式不移动视角；回到 3D 后继续跟随") : Device.IsValid() && Device->IsMotionFollowEnabled() && !Device->HasMotionSample() ? TEXT("等待运动数据；可拖动背景调整视角") : TEXT("双指缩放；拖动背景旋转"));})
     .Font(CrankTheme::Utility()).ColorAndOpacity(CrankTheme::HudMuted()).AutoWrapText(true)];
    Content->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("跟随灵敏度  %.0f%%"),Device.IsValid() ? Device->GetMotionSensitivity()*100 : 35));})
        .Font(CrankTheme::Body()).ColorAndOpacity(CrankTheme::HudText())];
    Content->AddSlot().AutoHeight().Padding(0,8,0,18)
    [SNew(SBox).MinDesiredHeight(40)
     [SNew(SSlider).Style(&CrankTheme::Slider()).MinValue(.1f).MaxValue(.8f).StepSize(.05f)
      .Value_Lambda([this]{return Device.IsValid() ? Device->GetMotionSensitivity() : .35f;})
      .SliderHandleColor(CrankTheme::Accent()).SliderBarColor(CrankTheme::HudFrame())
      .OnValueChanged_Lambda([this](float V){if(Device.IsValid()) Device->SetMotionSensitivity(V);})]];
    Content->AddSlot().AutoHeight().Padding(0,0,0,20)
    [Action(TEXT("校准当前握持姿势"),[this]{if(Device.IsValid()) {Device->CalibrateMotion(); Device->PulseHaptic();} return FReply::Handled();},true,ECrankGlyph::Reset)];
    Toggle(TEXT("操作震动"),TEXT("按钮短反馈，摇柄转动带刻度感"),[this]{return Device.IsValid() && Device->IsHapticsEnabled();},
        [this](bool V){if(Device.IsValid()) Device->SetHaptics(V);});
    Content->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("震动强度  %.0f%%"),Device.IsValid() ? Device->GetHapticStrength()*100 : 60));})
        .Font(CrankTheme::Body()).ColorAndOpacity(CrankTheme::HudText())];
    Content->AddSlot().AutoHeight().Padding(0,8,0,20)
    [SNew(SBox).MinDesiredHeight(40)
     [SNew(SSlider).Style(&CrankTheme::Slider()).MinValue(.1f).MaxValue(1.f).StepSize(.1f)
      .Value_Lambda([this]{return Device.IsValid() ? Device->GetHapticStrength() : .6f;})
      .SliderHandleColor(CrankTheme::Accent()).SliderBarColor(CrankTheme::HudFrame())
      .OnValueChanged_Lambda([this](float V){if(Device.IsValid()) Device->SetHapticStrength(V);})]];
    Toggle(TEXT("减少界面动效"),TEXT("切换视图直接完成，保留操作反馈"),[this]{return Device.IsValid() && Device->IsReducedMotion();},
        [this](bool V){if(Device.IsValid()) Device->SetReducedMotion(V);});
    Content->AddSlot().AutoHeight().Padding(0,8,0,8)[HudText(TEXT("画面与性能"),true)];
    Content->AddSlot().AutoHeight().Padding(0,0,0,8)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Action(TEXT("30 帧 · 省电"),[this]{if(Device.IsValid()) Device->SetFrameRate(30); return FReply::Handled();},true,ECrankGlyph::Settings,[this]{return Device.IsValid() && Device->GetFrameRate()==30;},false)]
     +SHorizontalBox::Slot().FillWidth(1)[Action(TEXT("60 帧 · 流畅"),[this]{if(Device.IsValid()) Device->SetFrameRate(60); return FReply::Handled();},true,ECrankGlyph::Settings,[this]{return Device.IsValid() && Device->GetFrameRate()==60;},false)]];
    Content->AddSlot().AutoHeight().Padding(0,0,0,20)
    [SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Action(TEXT("标准画面"),[this]{if(Device.IsValid()) Device->SetResolutionScale(.75f); return FReply::Handled();},true,ECrankGlyph::Settings,[this]{return Device.IsValid() && Device->GetResolutionScale()<.9f;},false)]
     +SHorizontalBox::Slot().FillWidth(1)[Action(TEXT("高清画面"),[this]{if(Device.IsValid()) Device->SetResolutionScale(1); return FReply::Handled();},true,ECrankGlyph::Settings,[this]{return Device.IsValid() && Device->GetResolutionScale()>=.9f;},false)]];
    Content->AddSlot().AutoHeight()
    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() ? Device->GetExperienceError() : FString());})
     .Font(CrankTheme::Body()).ColorAndOpacity(CrankTheme::HudError()).AutoWrapText(true)];
    Content->AddSlot().AutoHeight().Padding(0,20,0,8)[HudText(TEXT("游戏介绍翻译"),true)];
    Content->AddSlot().AutoHeight()[BuildTranslationModel()];
    return Content;
}
void SCrankOverlay::Construct(const FArguments& Args) {
    Device=Args._Device;
    LibraryButton=Action(TEXT("游戏"),[this]{return ToggleLibrary();},true,ECrankGlyph::Library,[this]{return bLibraryOpen;},true,true);
    SettingsButton=Action(TEXT("设置"),[this]{return ToggleSettings();},true,ECrankGlyph::Settings,[this]{return bSettingsOpen;},true,true);
    auto Library=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SAssignNew(GameRows,SVerticalBox)];
#if PLATFORM_ANDROID
    Library->AddSlot().AutoHeight().Padding(0,16,0,8)[Action(TEXT("导入 ZIP 游戏包"),[this]{if(Device.IsValid()) Device->BeginGameImport(false); return FReply::Handled();})];
    Library->AddSlot().AutoHeight()[Action(TEXT("选择 PDX 文件夹"),[this]{if(Device.IsValid()) Device->BeginGameImport(true); return FReply::Handled();})];
    Library->AddSlot().AutoHeight().Padding(0,16,0,8)[HudText(TEXT("ZIP 游戏直链"))];
    Library->AddSlot().AutoHeight()
    [SAssignNew(UrlInput,SEditableTextBox).Style(&CrankTheme::Input()).Font(CrankTheme::Body()).RevertTextOnEscape(false)
     .HintText(FText::FromString(TEXT("https://…/game.zip")))];
    Library->AddSlot().AutoHeight().Padding(0,10,0,0)
    [Action(TEXT("下载并安装"),[this]{if(Device.IsValid() && UrlInput) Device->BeginUrlGameImport(UrlInput->GetText().ToString()); return FReply::Handled();})];
#else
    Library->AddSlot().AutoHeight().Padding(0,16,0,8)[HudText(TEXT("PDX 文件夹"))];
    Library->AddSlot().AutoHeight()
    [SAssignNew(DirectoryInput,SEditableTextBox).Style(&CrankTheme::Input()).Font(CrankTheme::Body()).RevertTextOnEscape(false)
     .HintText(FText::FromString(TEXT("粘贴游戏文件夹路径")))];
    Library->AddSlot().AutoHeight().Padding(0,12,0,0)[Action(TEXT("打开游戏"),[this]{return OpenDirectory();})];
#endif
    Library->AddSlot().AutoHeight().Padding(0,12,0,0)
    [SNew(SBox).HeightOverride(6).Visibility_Lambda([this]{return Device.IsValid() && Device->IsImportBusy() ? EVisibility::Visible : EVisibility::Collapsed;})
     [SNew(SProgressBar).Style(&CrankTheme::Progress()).Percent(TOptional<float>()).FillColorAndOpacity(CrankTheme::Accent())]];
    Library->AddSlot().AutoHeight().Padding(0,10,0,0)
    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() ? Device->GetImportMessage() : FString());})
     .Visibility_Lambda([this]{return Device.IsValid() && !Device->GetImportMessage().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;})
     .Font(CrankTheme::Body()).ColorAndOpacity(CrankTheme::HudMuted()).AutoWrapText(true)];
    Library->AddSlot().AutoHeight().Padding(0,10,0,0)
    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() && Device->HasError() ? Device->GetStatusText() : FString());})
     .Visibility_Lambda([this]{return Device.IsValid() && Device->HasError() ? EVisibility::Visible : EVisibility::Collapsed;})
     .Font(CrankTheme::Body()).ColorAndOpacity(CrankTheme::HudError()).AutoWrapText(true)];
    Library->AddSlot().AutoHeight().Padding(0,12,0,0)
    [SNew(SBox).Visibility_Lambda([this]{return Device.IsValid() && Device->IsLauncherDownloadBusy() ? EVisibility::Visible : EVisibility::Collapsed;})
     [Action(TEXT("取消下载"),[this]{if(Device.IsValid()) Device->CancelLauncherDownload();return FReply::Handled();},true,ECrankGlyph::Reset)]];
    Library->AddSlot().AutoHeight().Padding(0,12,0,0)
    [SNew(SBox).Visibility_Lambda([this]{return Device.IsValid() && Device->CanRetryImport() ? EVisibility::Visible : EVisibility::Collapsed;})
     [Action(TEXT("重试下载 / 导入"),[this]{if(Device.IsValid()) Device->RetryGameImport(); return FReply::Handled();},true,ECrankGlyph::Reset)]];
    auto Tools=SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[LibraryButton.ToSharedRef()]
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[SettingsButton.ToSharedRef()]
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
        [SNew(SCrankAction).Device(Device).Icon(ECrankGlyph::Screen)
         .Label_Lambda([this]{return FText::FromString(Device.IsValid() && Device->IsImmersive() ? TEXT("返回 3D") : TEXT("沉浸"));})
         .Compact_Lambda([this]{return GetCachedGeometry().GetLocalSize().X<740;})
         .Selected_Lambda([this]{return Device.IsValid() && Device->IsImmersive();})
         .OnClicked_Lambda([this]{if(Device.IsValid()) Device->SetImmersive(!Device->IsImmersive()); FSlateApplication::Get().SetAllUserFocusToGameViewport(); return FReply::Handled();})]
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
        [SNew(SBox).Visibility_Lambda([this]{return Device.IsValid() && Device->IsImmersive() ? EVisibility::Collapsed : EVisibility::Visible;})
         [Action(TEXT("放大"),[this]{if(Device.IsValid()) Device->ToggleEnlarged(); return FReply::Handled();},true,ECrankGlyph::Expand,[this]{return Device.IsValid() && Device->IsEnlarged();},true,true)]]
        +SHorizontalBox::Slot().AutoWidth()
        [SNew(SBox).Visibility_Lambda([this]{return Device.IsValid() && Device->IsImmersive() ? EVisibility::Collapsed : EVisibility::Visible;})
         [Action(TEXT("复位"),[this]{if(Device.IsValid()) Device->ResetView(); return FReply::Handled();},true,ECrankGlyph::Reset,nullptr,true,true)]]
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
        [SNew(SBox).Visibility_Lambda([this]{return Device.IsValid() && Device->IsImmersive() ? EVisibility::Visible : EVisibility::Collapsed;})
         .IsEnabled_Lambda([this]{return Device.IsValid() && !Device->IsOverlayOpen();})
         [Action(TEXT("菜单"),[this]{if(Device.IsValid()) Device->ToggleSystemMenu();return FReply::Handled();},true,ECrankGlyph::Menu,nullptr,true,true)]]
        +SHorizontalBox::Slot().AutoWidth()
        [SNew(SBox).Visibility_Lambda([this]{return Device.IsValid() && Device->IsImmersive() ? EVisibility::Visible : EVisibility::Collapsed;})
         .IsEnabled_Lambda([this]{return Device.IsValid() && !Device->IsOverlayOpen();})
         [Action(TEXT("锁屏"),[this]{if(Device.IsValid()) Device->ToggleLock();return FReply::Handled();},true,ECrankGlyph::Lock,nullptr,true,true)]];
    ChildSlot
    [SNew(SSafeZone).Padding(FMargin(CrankTheme::Edge)).Visibility(EVisibility::SelfHitTestInvisible)
     [SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
      +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
      [SNew(SBox).MaxDesiredWidth(140).Visibility_Lambda([this]{return GetCachedGeometry().GetLocalSize().X<650 ? EVisibility::Collapsed : EVisibility::HitTestInvisible;})
       [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
        [SNew(STextBlock).Text(FText::FromString(TEXT("Playdate"))).Font(CrankTheme::Label()).ColorAndOpacity(CrankTheme::Ink())]
        +SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() ? Device->GetGameTitle() : FString());})
         .Font(CrankTheme::Utility()).ColorAndOpacity(CrankTheme::Ink()).AutoWrapText(true)]]]
      +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top)[Tools]
      +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
      [SAssignNew(PanelContainer,SBox).WidthOverride_Lambda([this]{const FVector2D Size=GetCachedGeometry().GetLocalSize();return FMath::Min(380.f,float(Size.X)-2*CrankTheme::Edge);})
       .HeightOverride_Lambda([this]{const FVector2D Size=GetCachedGeometry().GetLocalSize();const float Available=FMath::Max(1.f,float(Size.Y)-2*CrankTheme::Edge);return FMath::Clamp(float(Size.Y)*.7f,FMath::Min(240.f,Available),FMath::Min(620.f,Available));})
       .Visibility_Lambda([this]{return bLibraryOpen || bSettingsOpen ? EVisibility::Visible : (PanelBlend>.01f ? EVisibility::HitTestInvisible : EVisibility::Collapsed);})
       [SNew(SBorder).BorderImage(&CrankTheme::Panel()).Padding(22)
        [SNew(SVerticalBox)
         +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
         [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bSettingsOpen ? TEXT("体验设置") : TEXT("游戏库"));}).Font(CrankTheme::Title()).ColorAndOpacity(CrankTheme::HudText())]
         +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
         [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(bSettingsOpen ? TEXT("让这台掌机适应你的握持方式") : TEXT("选择游戏，继续游玩"));})
          .Font(CrankTheme::Utility()).ColorAndOpacity(CrankTheme::HudMuted()).AutoWrapText(true)]
         +SVerticalBox::Slot().FillHeight(1)
         [SNew(SOverlay)
          +SOverlay::Slot()
          [SAssignNew(GameScroll,SScrollBox).Visibility_Lambda([this]{return bLibraryOpen ? EVisibility::Visible : EVisibility::Collapsed;})+SScrollBox::Slot()[Library]]
          +SOverlay::Slot()
          [SAssignNew(SettingsScroll,SScrollBox).Visibility_Lambda([this]{return bSettingsOpen ? EVisibility::Visible : EVisibility::Collapsed;})+SScrollBox::Slot()[BuildSettings()]]]
         +SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)
     [Action(TEXT("返回游戏"),[this]{return ClosePanels();},true,ECrankGlyph::Reset)]]]]
      +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
      [SNew(SBorder).BorderImage(&CrankTheme::Panel()).Padding(FMargin(12,9))
       .Visibility_Lambda([this]{return !bLibraryOpen && !bSettingsOpen && Device.IsValid() && !Device->IsImmersive() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;})
       [SNew(STextBlock).Text(FText::FromString(TEXT("双击显示屏放大 · 拖动背景调整视角"))).Font(CrankTheme::Utility()).ColorAndOpacity(CrankTheme::HudMuted())]]
      +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
      .Padding(TAttribute<FMargin>::CreateLambda([this]{return FMargin(0,0,0,Device.IsValid() && Device->IsImmersive() ? 0 : 52);}))
      [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() ? Device->GetStatusText() : FString());})
       .Visibility_Lambda([this]{return !bLibraryOpen && !bSettingsOpen && Device.IsValid() && (Device->HasError() || !Device->GetLauncherMessage().IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;})
       .Font(CrankTheme::Body()).ColorAndOpacity_Lambda([this]{return Device.IsValid() && Device->HasError() ? CrankTheme::Error() : CrankTheme::Ink();}).WrapTextAt(320)]
      +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
      [SNew(SBorder).BorderImage(&CrankTheme::Panel()).Padding(10)
       .Visibility_Lambda([this]{return !bLibraryOpen && !bSettingsOpen && Device.IsValid() && !Device->IsImmersive() ? EVisibility::Visible : EVisibility::Collapsed;})
       [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()
        [Action(TEXT("摇柄"),[this]{bDialExpanded=!bDialExpanded; return FReply::Handled();},true,ECrankGlyph::Crank)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,6)
        [SNew(SCrankDial).Device(Device).Visibility_Lambda([this]{return bDialExpanded ? EVisibility::Visible : EVisibility::Collapsed;})]
        +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,6)
        [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Device.IsValid() && Device->IsCrankDocked() ? TEXT("已收起") : FString::Printf(TEXT("%.0f°"),Device.IsValid() ? FMath::Fmod(Device->GetCrankAngle()+360000.f,360.f) : 0.f));})
         .Font(CrankTheme::Utility()).ColorAndOpacity(CrankTheme::Accent())]
        +SVerticalBox::Slot().AutoHeight()
        [SNew(SCrankAction).Device(Device).ShowIcon(false).Label_Lambda([this]{return FText::FromString(Device.IsValid() && Device->IsCrankDocked() ? TEXT("展开") : TEXT("收起"));})
         .OnClicked_Lambda([this]{if(Device.IsValid()) Device->ToggleCrankDocked(); return FReply::Handled();})]]]]];
    RefreshGames();
}
void SCrankOverlay::RefreshGames() {
    GameRows->ClearChildren(); GameButtons.Empty(); if(!Device.IsValid()) return;
    const auto Games=Device->GetGameDirectories();
    if(!Games.Num()) GameRows->AddSlot().AutoHeight()[HudText(TEXT("还没有游戏。导入 ZIP 或选择一个 PDX 文件夹。"))];
    int Index=0;
    for(const FString& Path:Games) {
        const bool Current=Path==Device->GetCurrentGameDirectory() && Device->IsGameRunning(); if(Current) FocusedGame=Index;
        TSharedRef<SWidget> Row=SNew(SCrankAction).Device(Device).Label(FText::FromString(Device->GetTitleForGame(Path)))
            .Subtitle(FText::FromString(Device->GetGameCapability(Path))).Cover(Device->GetGameCover(Path))
            .Selected_Lambda([this,Path]{return Device.IsValid() && Path==Device->GetCurrentGameDirectory() && Device->IsGameRunning();})
            .OnClicked_Lambda([this,Path]{return ChooseGame(Path);})
            .IsEnabled_Lambda([this]{return Device.IsValid() && !Device->IsImportBusy() && !Device->IsGameSwitching();});
        GameButtons.Add(Row); GameRows->AddSlot().AutoHeight().Padding(0,0,0,8)[Row]; Index++;
    }
    FocusedGame=FMath::Clamp(FocusedGame,0,FMath::Max(0,GameButtons.Num()-1));
}
FReply SCrankOverlay::ToggleLibrary() {
    if(bLibraryOpen) return ClosePanels();
    bSettingsOpen=false; bLibraryOpen=true;
    if(Device.IsValid()) Device->SetOverlayOpen(true);
    RefreshGames();
    if(GameButtons.Num()) FSlateApplication::Get().SetKeyboardFocus(GameButtons[FocusedGame],EFocusCause::SetDirectly);
    else if(DirectoryInput) FSlateApplication::Get().SetKeyboardFocus(DirectoryInput,EFocusCause::SetDirectly);
    return FReply::Handled();
}
FReply SCrankOverlay::ToggleSettings() {
    if(bSettingsOpen) return ClosePanels();
    bLibraryOpen=false; bSettingsOpen=true; if(Device.IsValid()) Device->SetOverlayOpen(true);
    if(SettingsFocus) FSlateApplication::Get().SetKeyboardFocus(SettingsFocus,EFocusCause::SetDirectly);
    return FReply::Handled();
}
FReply SCrankOverlay::ClosePanels() {
    const bool Settings=bSettingsOpen;
    bLibraryOpen=false; bSettingsOpen=false;
    if(Device.IsValid()) Device->SetOverlayOpen(false);
    FSlateApplication::Get().SetKeyboardFocus(Settings ? SettingsButton : LibraryButton,EFocusCause::SetDirectly);
    return FReply::Handled();
}
void SCrankOverlay::ShowLibrary() { if(!bLibraryOpen) ToggleLibrary(); }
void SCrankOverlay::ShowSettings(bool TranslationModel) {
    if(!bSettingsOpen)ToggleSettings();
    if(TranslationModel && SettingsScroll && Device.IsValid()) {
        const auto Service=Device->GetTranslation();
        const auto Target=Service && Service->HasModel() ? (Service->IsLightModel()?LightModelFocus:Q4ModelFocus) : ModelDownloadFocus;
        if(Target) {
            FSlateApplication::Get().SetKeyboardFocus(Target,EFocusCause::SetDirectly);
            SettingsScroll->ScrollDescendantIntoView(Target,false,EDescendantScrollDestination::IntoView,4);
            // Auto-wrapped labels need a visible layout pass before their final height is known.
            RegisterActiveTimer(.1f,FWidgetActiveTimerDelegate::CreateLambda([this,Weak=TWeakPtr<SWidget>(Target)](double,float) {
                if(bSettingsOpen && SettingsScroll && Weak.IsValid())
                    SettingsScroll->ScrollDescendantIntoView(Weak.Pin(),false,EDescendantScrollDestination::IntoView,4);
                return EActiveTimerReturnType::Stop;
            }));
        }
    }
}
FReply SCrankOverlay::ChooseGame(const FString& Directory) {
    if(Device.IsValid() && Device->PlayGame(Directory)) ClosePanels();
    return FReply::Handled();
}
FReply SCrankOverlay::OpenDirectory() {
    return DirectoryInput ? ChooseGame(DirectoryInput->GetText().ToString().TrimStartAndEnd().TrimQuotes()) : FReply::Handled();
}
FReply SCrankOverlay::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) {
    if((bLibraryOpen || bSettingsOpen) && Event.GetKey()==EKeys::Escape) return ClosePanels();
    if(bLibraryOpen && GameButtons.Num() && (Event.GetKey()==EKeys::Up || Event.GetKey()==EKeys::Down)) {
        FocusedGame=(FocusedGame+(Event.GetKey()==EKeys::Down ? 1 : GameButtons.Num()-1))%GameButtons.Num();
        GameScroll->ScrollDescendantIntoView(GameButtons[FocusedGame],false);
        FSlateApplication::Get().SetKeyboardFocus(GameButtons[FocusedGame],EFocusCause::Navigation); return FReply::Handled();
    }
    return SCompoundWidget::OnKeyDown(Geometry,Event);
}
void SCrankOverlay::Tick(const FGeometry& Geometry,double Time,float DeltaTime) {
    SCompoundWidget::Tick(Geometry,Time,DeltaTime);
    const float Alpha=Device.IsValid() && Device->IsReducedMotion() ? 1.f : CrankView::SmoothAlpha(16,DeltaTime);
    PanelBlend=FMath::Lerp(PanelBlend,bLibraryOpen || bSettingsOpen ? 1.f : 0.f,Alpha);
    PanelContainer->SetRenderOpacity(PanelBlend);
    PanelContainer->SetRenderTransform(FSlateRenderTransform(FVector2D(0,(1-PanelBlend)*16)));
}

