#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"

#include "Features/GameLog/GameLogModel.h"
#include "Features/GameLog/GameLogPanel.h"
#include "Foundation/EditorFeatureRegistration.h"

namespace
{
    class FGameLogRegistration
    {
      public:
        void Start();
        void Stop();

      private:
        TSharedPtr<FGameLogModel> Model;
        TWeakPtr<SDockTab> TabWeakPtr;

        void OnRegisterMenus();
        TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& InArguments);
    };

    void FGameLogRegistration::Start()
    {
        Model = MakeShared<FGameLogModel>();
        FGlobalTabmanager::Get()
            ->RegisterNomadTabSpawner(TEXT("WandererGameLog"),
                FOnSpawnTab::CreateRaw(this, &FGameLogRegistration::OnSpawnTab))
            .SetDisplayName(FText::FromString(TEXT("Game Log Console")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FGameLogRegistration::OnRegisterMenus));
    }

    void FGameLogRegistration::Stop()
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("WandererGameLog"));
        if (const TSharedPtr<SDockTab> Tab = TabWeakPtr.Pin())
        {
            Tab->SetContent(SNullWidget::NullWidget);
            Tab->RequestCloseTab();
        }
        TabWeakPtr.Reset();
        Model.Reset();
    }

    void FGameLogRegistration::OnRegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("Wanderer"))
            .AddMenuEntry(TEXT("WandererGameLog"),
                FText::FromString(TEXT("Game Log Console")),
                FText::FromString(TEXT("Game Log Console")),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda(
                    []
                    {
                        FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("WandererGameLog")));
                    })));
    }

    TSharedRef<SDockTab> FGameLogRegistration::OnSpawnTab(const FSpawnTabArgs& InArguments)
    {
        const TSharedRef<SDockTab> Tab =
            SNew(SDockTab)
                .TabRole(ETabRole::NomadTab)
            [
                SNew(SGameLogPanel)
                    .Model(Model)
            ];
        TabWeakPtr = Tab;
        return Tab;
    }

    FGameLogRegistration Feature;
    FEditorFeatureRegistration Registration(
        TEXT("GameLog"),
        []
        {
            Feature.Start();
        },
        []
        {
            Feature.Stop();
        });
} // namespace
