#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"

#include "Features/AudioManager/View/AudioManagerPanel.h"
#include "Features/AudioManager/InteriorFitAudioSyncService.h"
#include "Foundation/EditorFeatureRegistration.h"

namespace
{
    class FAudioManagerRegistration
    {
      public:
        void Start();
        void Stop();

      private:
        TSharedPtr<FInteriorFitAudioSyncService> Sync;
        TWeakPtr<SDockTab> TabWeakPtr;
        TWeakPtr<SAudioManagerPanel> PanelWeakPtr;

        void OnRegisterMenus();
        TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& InArguments);
    };

    void FAudioManagerRegistration::Start()
    {
        Sync = MakeShared<FInteriorFitAudioSyncService>();
        Sync->Start();
        FGlobalTabmanager::Get()
            ->RegisterNomadTabSpawner(TEXT("WandererAudioManager"),
                FOnSpawnTab::CreateRaw(this, &FAudioManagerRegistration::OnSpawnTab))
            .SetDisplayName(FText::FromString(TEXT("Audio Manager")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FAudioManagerRegistration::OnRegisterMenus));
    }

    void FAudioManagerRegistration::Stop()
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("WandererAudioManager"));
        if (Sync)
        {
            Sync->Stop();
        }
        Sync.Reset();
        if (const TSharedPtr<SAudioManagerPanel> Panel = PanelWeakPtr.Pin())
        {
            Panel->Stop();
        }
        PanelWeakPtr.Reset();
        if (const TSharedPtr<SDockTab> Tab = TabWeakPtr.Pin())
        {
            Tab->SetContent(SNullWidget::NullWidget);
            Tab->RequestCloseTab();
        }
        TabWeakPtr.Reset();
    }

    void FAudioManagerRegistration::OnRegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("Wanderer"))
            .AddMenuEntry(TEXT("WandererAudioManager"),
                FText::FromString(TEXT("Audio Manager")),
                FText::FromString(TEXT("Audio Manager")),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda(
                    []
                    {
                        FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("WandererAudioManager")));
                    })));
    }

    TSharedRef<SDockTab> FAudioManagerRegistration::OnSpawnTab(const FSpawnTabArgs& InArguments)
    {
        const TSharedRef<SAudioManagerPanel> Panel = SNew(SAudioManagerPanel);
        PanelWeakPtr = Panel;
        const TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab)[Panel];
        Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
            [OwnerWeakPtr = PanelWeakPtr](TSharedRef<SDockTab>)
            {
                if (const TSharedPtr<SAudioManagerPanel> Owner = OwnerWeakPtr.Pin())
                {
                    Owner->Stop();
                }
            }));
        TabWeakPtr = Tab;
        return Tab;
    }

    FAudioManagerRegistration Feature;
    FEditorFeatureRegistration Registration(
        TEXT("AudioManager"),
        []
        {
            Feature.Start();
        },
        []
        {
            Feature.Stop();
        });
} // namespace
