#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Data/WandererMapMarkerSettings.h"
#include "Framework/WandererPlayerController.h"

namespace
{
    AWandererPlayerController* FindMapPlayer()
    {
        return GEditor && GEditor->PlayWorld
                   ? Cast<AWandererPlayerController>(GEditor->PlayWorld->GetFirstPlayerController())
                   : nullptr;
    }

    FName FindSceneDoor(const UWorld& InWorld)
    {
        const FString Scene = UWandererMapMarkerSettings::GetScenePackage(InWorld);
        for (const FWandererMapDoorMarker& Marker :
            GetDefault<UWandererMapMarkerSettings>()->GetDoorMarkers())
        {
            if (Marker.ScenePackage == Scene)
            {
                return Marker.ID;
            }
        }
        return NAME_None;
    }

    UUserWidget* FindMapView(AWandererPlayerController& InPlayer)
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
            &InPlayer, Widgets, UUserWidget::StaticClass(), true);
        for (UUserWidget* Widget : Widgets)
        {
            if (Widget->GetOwningPlayer() == &InPlayer
                && Widget->GetClass()->GetFName() == TEXT("WandererMapWidget"))
            {
                return Widget;
            }
        }
        return nullptr;
    }
    void VerifyMapSelection(FAutomationTestBase& InTest)
    {

        AWandererPlayerController* Player = FindMapPlayer();
        if (!InTest.TestNotNull(TEXT("Map controller"), Player))
        {
            return;
        }
        const FMapScanSnapshot* Scan = Player->GetMapScanSnapshot();
        InTest.TestTrue(TEXT("Completed map has bounds and connected cells"),
            Scan && Scan->MapBounds.bIsValid && !Scan->Cells.IsEmpty());
        InTest.TestTrue(
            TEXT("Completed survey has surface regions"), Scan && !Scan->Regions.IsEmpty());
        Player->SetMapOpen(true);
        InTest.TestTrue(TEXT("Map is open"), Player->IsMapOpen());
        InTest.TestFalse(TEXT("Unknown marker rejected"), Player->SelectMapDoor(NAME_None));
        const FName Door = FindSceneDoor(*Player->GetWorld());
        InTest.TestFalse(TEXT("Demo door configured"), Door.IsNone());
        InTest.TestTrue(TEXT("Configured door selected"), Player->SelectMapDoor(Door));
        UUserWidget* View = FindMapView(*Player);
        if (InTest.TestNotNull(TEXT("Registered private map view"), View))
        {
            View->RemoveFromParent();
            InTest.TestEqual(
                TEXT("Selection survives view removal"), Player->GetSelectedMapDoor(), Door);
            constexpr int32 MapViewportOrder = 100;
            View->AddToViewport(MapViewportOrder);
        }
        return;
    }

    void VerifyMapReentrantClose(FAutomationTestBase& InTest)
    {


            AWandererPlayerController* Player = FindMapPlayer();
            if (!Player || !Player->GetPawn())
            {
                InTest.AddError(TEXT("Map regression requires a possessed player."));
                return;
            }
            const FVector Before = Player->GetPawn()->GetActorLocation();
            const TWeakObjectPtr<AWandererPlayerController> PlayerWeakPtr = Player;
            const FDelegateHandle Handle = Player->GetMapAudioEvent().AddLambda(
                [PlayerWeakPtr](EWandererUIAudioAction InAction)
                {
                    if (InAction == EWandererUIAudioAction::Choose && PlayerWeakPtr.IsValid())
                    {
                        PlayerWeakPtr->CloseMap(EWandererUIAudioAction::Silent);
                    }
                });
            InTest.TestFalse(TEXT("Reentrant close cancels old teleport request"),
                Player->TeleportSelectedMapDoor());
            Player->GetMapAudioEvent().Remove(Handle);
            InTest.TestTrue(TEXT("Cancelled request does not move pawn"),
                Player->GetPawn()->GetActorLocation().Equals(Before));
            InTest.TestTrue(TEXT("Close clears selection"), Player->GetSelectedMapDoor().IsNone());
            Player->SetEntryMapScanEnabled(false);
            APawn* PreviousPawn = Player->GetPawn();
            Player->UnPossess();
            const FMapScanSnapshot* Scan = Player->GetMapScanSnapshot();
            InTest.TestTrue(TEXT("Unpossess clears even a paused scanner"),
                Scan && Scan->Cells.IsEmpty() && !Scan->MapBounds.bIsValid);
            InTest.TestFalse(
                TEXT("Missing pawn hides player marker"), Player->GetMapPlayerSnapshot().bVisible);
            Player->Possess(PreviousPawn);
            Player->StartEntryMapScan();
            return;
        
    }

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapInteractionLifecycleTest,
    "Wanderer.Map.ScanInteractionLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMapInteractionLifecycleTest::RunTest(const FString& InParameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Start = FPlatformTime::Seconds()]()
        {
            const AWandererPlayerController* Player = FindMapPlayer();
            if (Player && Player->IsMapInteractionReady() && Player->IsEntryMapReady())
            {
                return true;
            }
            constexpr double ReadyTimeoutSeconds = 180.0;
            if (FPlatformTime::Seconds() - Start < ReadyTimeoutSeconds)
            {
                return false;
            }
            AddError(TEXT("Map PIE did not reach a completed scan within the timeout."));
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this]()
        {
            VerifyMapSelection(*this);
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this]()
        {
            VerifyMapReentrantClose(*this);
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Start = FPlatformTime::Seconds()]()
        {
            const AWandererPlayerController* Player = FindMapPlayer();
            if (Player && Player->IsEntryMapReady())
            {
                return true;
            }
            constexpr double RescanTimeoutSeconds = 180.0;
            if (FPlatformTime::Seconds() - Start < RescanTimeoutSeconds)
            {
                return false;
            }
            AddError(TEXT("Possession did not rebuild the map scan."));
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
