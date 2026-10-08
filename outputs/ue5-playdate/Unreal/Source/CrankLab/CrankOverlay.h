#pragma once
#include "Widgets/SCompoundWidget.h"
#include "CrankDevicePawn.h"
#include "CrankControls.h"
class SEditableTextBox;
class SVerticalBox;
class SBox;
class SScrollBox;
class SCrankOverlay : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SCrankOverlay) {} SLATE_ARGUMENT(TWeakObjectPtr<ACrankDevicePawn>,Device) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void ShowLibrary();
    void ShowSettings(bool TranslationSettings=false);
    void RefreshGames();
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    void Tick(const FGeometry& Geometry,double Time,float DeltaTime) override;
private:
    TSharedRef<SWidget> Action(const FString& Label,TFunction<FReply()> Callback,bool AllowBusy=false,ECrankGlyph Icon=ECrankGlyph::Library,TFunction<bool()> Selected=nullptr,bool ShowIcon=true,bool Toolbar=false);
    TSharedRef<SWidget> BuildSettings();
    TSharedRef<SWidget> BuildTranslationSettings();
    FReply ToggleSettings();
    FReply ClosePanels();
    FReply ToggleLibrary();
    FReply OpenDirectory();
    FReply ChooseGame(const FString& Directory);
    TWeakObjectPtr<ACrankDevicePawn> Device;
    TSharedPtr<SEditableTextBox> DirectoryInput;
    TSharedPtr<SEditableTextBox> UrlInput;
    TSharedPtr<SVerticalBox> GameRows;
    TSharedPtr<SWidget> LibraryButton;
    TSharedPtr<SWidget> SettingsButton;
    TSharedPtr<SBox> PanelContainer;
    TSharedPtr<SWidget> SettingsFocus;
    TSharedPtr<SWidget> TranslationSettingsFocus;
    TSharedPtr<SScrollBox> GameScroll;
    TSharedPtr<SScrollBox> SettingsScroll;
    TArray<TSharedRef<SWidget>> GameButtons;
    int32 FocusedGame=0;
    bool bLibraryOpen=false;
    bool bSettingsOpen=false, bDialExpanded=true;
    float PanelBlend=0;
};
