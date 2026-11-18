#include "UI/Loading/WandererLoadingMapWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Modules/ModuleManager.h"
#include "MoviePlayer.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Core/ModuleLifecycleBroadcaster.h"
#include "Data/WandererMapMarkerSettings.h"

namespace
{
    class FLoadingScreenRegistration
    {
    public:
        FLoadingScreenRegistration()
        {
            StartedHandle = FModuleLifecycleBroadcaster::GetStartedEvent().AddRaw(
                this, &FLoadingScreenRegistration::Start);
            StoppedHandle = FModuleLifecycleBroadcaster::GetStoppedEvent().AddRaw(
                this, &FLoadingScreenRegistration::Stop);
        }
        ~FLoadingScreenRegistration()
        {
            FModuleLifecycleBroadcaster::GetStartedEvent().Remove(StartedHandle);
            FModuleLifecycleBroadcaster::GetStoppedEvent().Remove(StoppedHandle);
        }
    private:
        FDelegateHandle StartedHandle;
        FDelegateHandle StoppedHandle;
        FDelegateHandle PreLoadHandle;
        FProgressBarStyle LoadingProgressStyle;
        void Start()
        {
            LoadingProgressStyle.SetBackgroundImage(
                FSlateRoundedBoxBrush(FLinearColor(0.95f, 0.88f, 0.70f), 3.0f));
            LoadingProgressStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f));
            PreLoadHandle = FCoreUObjectDelegates::PreLoadMap.AddRaw(
                this, &FLoadingScreenRegistration::ShowLoadingScreen);
        }
        void Stop()
        {
            FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadHandle);
            PreLoadHandle.Reset();
            if (FModuleManager::Get().IsModuleLoaded(TEXT("MoviePlayer")))
            {
                if (IGameMoviePlayer* Player = GetMoviePlayer())
                {
                    Player->StopMovie();
                    Player->WaitForMovieToFinish();
                }
            }
        }
        TSharedRef<SWidget> CreateProgressWidget()
        {
            return SNew(SBox)
                .HeightOverride(18)
                [
                    SNew(SProgressBar)
                    .Style(&LoadingProgressStyle)
                    .FillColorAndOpacity(FLinearColor(0.32f, 0.065f, 0.055f))
                    .BorderPadding(FVector2D::ZeroVector)
                ];
        }

        TSharedRef<SWidget> CreateContentWidget(const FText& InTitle)
        {
            return SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 20)
                [
                    SNew(STextBlock)
                    .Text(InTitle)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
                    .ColorAndOpacity(FLinearColor(0.22f, 0.13f, 0.065f))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBox)
                    .HeightOverride(450)
                    [
                        SNew(SWandererLoadingMapWidget)
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 16)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Loading map…")))
                    .ColorAndOpacity(FLinearColor(0.22f, 0.13f, 0.065f))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    CreateProgressWidget()
                ];
        }

        void ShowLoadingScreen(const FString& InMapName)
        {
            if (!IsMoviePlayerEnabled() || IsRunningDedicatedServer())
            {
                return;
            }
            const FText Title = GetDefault<UWandererMapMarkerSettings>()->GetSceneTitle(InMapName);
            FLoadingScreenAttributes Screen;
            Screen.bAutoCompleteWhenLoadingCompletes = true;
            Screen.bMoviesAreSkippable = false;
            // No UObject callbacks: MoviePlayer paints this on its loading thread.
            Screen.WidgetLoadingScreen = SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.76f, 0.65f, 0.44f, 1))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(800)
                    [
                        CreateContentWidget(Title)
                    ]
                ];
            GetMoviePlayer()->SetupLoadingScreen(Screen);
            GetMoviePlayer()->PlayMovie();
        }
    };
    FLoadingScreenRegistration LoadingScreenRegistration;
}
