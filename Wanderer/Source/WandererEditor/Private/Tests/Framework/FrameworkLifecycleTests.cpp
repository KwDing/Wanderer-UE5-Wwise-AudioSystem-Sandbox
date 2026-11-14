#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Core/PlayerFeatureBroadcaster.h"
#include "Framework/CharacterSelectionModel.h"
#include "Framework/RadioControlModel.h"
#include "Framework/WandererGameState.h"
#include "Framework/WandererLoadingComponent.h"
#include "Framework/WandererPlayerController.h"

namespace
{
    struct FFrameworkLifecycleProbe
    {
        FAutomationTestBase& Test;
        TWeakObjectPtr<AWandererPlayerController> PlayerWeakPtr;
        FDelegateHandle StartedHandle;
        bool bStarted = false;
        double Deadline = FPlatformTime::Seconds() + 180.0;

        explicit FFrameworkLifecycleProbe(FAutomationTestBase& InTest) : Test(InTest)
        {
        }
        ~FFrameworkLifecycleProbe()
        {
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(StartedHandle);
        }

        void EndPlayer(AWandererPlayerController& InPlayer)
        {
            PlayerWeakPtr = &InPlayer;
            AActor& Actor = InPlayer;
            Actor.RouteEndPlay(EEndPlayReason::RemovedFromWorld);
            // An old ApplyPlayState continuation would overwrite this after the callback returns.
            InPlayer.bShowMouseCursor = false;
        }

        void VerifyStopped()
        {
            AWandererPlayerController* Player = PlayerWeakPtr.Get();
            if (!Test.TestNotNull(TEXT("Observed controller survives non-destroy EndPlay"), Player))
            {
                return;
            }
            Test.TestFalse(TEXT("Input continuation did not run"), Player->bShowMouseCursor);
            Test.TestFalse(
                TEXT("Map presentation is terminal"), Player->IsMapPresentationAvailable());
            Test.TestFalse(TEXT("Loading released its move lock"), Player->IsMoveInputIgnored());
            Test.TestFalse(TEXT("Loading released its look lock"), Player->IsLookInputIgnored());
            Test.TestFalse(
                TEXT("Stopped entry rejects transitions"), Player->SetEntryLoadingState(true));
            Test.TestFalse(
                TEXT("Stopped selection cannot reopen"), Player->OpenCharacterSelection());
            Test.TestFalse(
                TEXT("Stopped model cleared its portrait"), Player->IsCharacterSelectionOpen());
            AWandererGameState* State = Player->GetWorld()->GetGameState<AWandererGameState>();
            Test.TestFalse(TEXT("Controller detached before state reset"),
                State && State->OnPlayStateChanged.Contains(Player, FName(TEXT("ApplyPlayState"))));
        }
    };

    int32 CountPlayerFeatures(AWandererPlayerController& InPlayer)
    {
        int32 Count = 0;
        for (TActorIterator<AActor> It(InPlayer.GetWorld()); It; ++It)
        {
            if (It->GetOwner() == &InPlayer && !It->IsActorBeingDestroyed())
            {
                ++Count;
            }
        }
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            if (It->GetOwningPlayer() == &InPlayer && It->IsInViewport())
            {
                ++Count;
            }
        }
        return Count;
    }

    AWandererPlayerController* GetPlayer()
    {
        return GEditor && GEditor->PlayWorld
                   ? Cast<AWandererPlayerController>(GEditor->PlayWorld->GetFirstPlayerController())
                   : nullptr;
    }

    bool WaitForEntry(FFrameworkLifecycleProbe& InProbe)
    {
        AWandererPlayerController* Player = GetPlayer();
        if (Player && !Player->GetLoadingComponent()->IsLoading())
        {
            InProbe.PlayerWeakPtr = Player;
            return true;
        }
        if (FPlatformTime::Seconds() < InProbe.Deadline)
        {
            return false;
        }
        InProbe.Test.AddError(TEXT("Scene entry timed out in framework test."));
        return true;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameworkStartupStopTest,
    "Wanderer.Framework.StopDuringStartup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFrameworkStartupStopTest::RunTest(const FString& InParameters)
{
    TSharedRef<FFrameworkLifecycleProbe> Probe = MakeShared<FFrameworkLifecycleProbe>(*this);
    TWeakPtr<FFrameworkLifecycleProbe> ProbeWeakPtr = Probe;
    Probe->StartedHandle = FPlayerFeatureBroadcaster::GetStartedEvent().AddLambda(
        [ProbeWeakPtr](APlayerController& InPlayer)
        {
            TSharedPtr<FFrameworkLifecycleProbe> Pinned = ProbeWeakPtr.Pin();
            if (!Pinned)
            {
                return;
            }
            AWandererPlayerController* Player = Cast<AWandererPlayerController>(&InPlayer);
            if (Player && Player->GetWorld()->WorldType == EWorldType::PIE)
            {
                Pinned->bStarted = true;
                Pinned->EndPlayer(*Player);
            }
        });
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [Probe]()
        {
            Probe->Test.TestTrue(TEXT("Startup callback exercised"), Probe->bStarted);
            Probe->VerifyStopped();
            FPlayerFeatureBroadcaster::GetStartedEvent().Remove(Probe->StartedHandle);
            if (AWandererPlayerController* Player = Probe->PlayerWeakPtr.Get())
            {
                const int32 Before = CountPlayerFeatures(*Player);
                FPlayerFeatureBroadcaster::BroadcastStarted(*Player);
                Probe->Test.TestEqual(TEXT("Late startup cannot create entities or mount UI"),
                    CountPlayerFeatures(*Player), Before);
            }
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameworkStateStopTest,
    "Wanderer.Framework.StopDuringStateCallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFrameworkStateStopTest::RunTest(const FString& InParameters)
{
    TSharedRef<FFrameworkLifecycleProbe> Probe = MakeShared<FFrameworkLifecycleProbe>(*this);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [Probe]()
        {
            return WaitForEntry(*Probe);
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [Probe]()
        {
            AWandererPlayerController* Player = Probe->PlayerWeakPtr.Get();
            if (!Player)
            {
                return true;
            }
            const FDelegateHandle Handle = Player->GetMapAudioEvent().AddLambda(
                [Probe](EWandererUIAudioAction InAction)
                {
                    if (InAction == EWandererUIAudioAction::Open && Probe->PlayerWeakPtr.IsValid())
                    {
                        Probe->EndPlayer(*Probe->PlayerWeakPtr.Get());
                    }
                });
            AWandererGameState* State = Player->GetWorld()->GetGameState<AWandererGameState>();
            Probe->Test.TestFalse(TEXT("Ended player cancels pause transition"),
                State->SetPlayState(EWandererPlayState::PauseMenu));
            Player->GetMapAudioEvent().Remove(Handle);
            Probe->VerifyStopped();
            Probe->Test.TestFalse(
                TEXT("Stopped callback leaves no pause"), UGameplayStatics::IsGamePaused(State));
            Probe->Test.TestTrue(TEXT("Stopped callback restored Normal"),
                State->GetPlayState() == EWandererPlayState::Normal);
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameworkPauseOwnershipTest,
    "Wanderer.Framework.PauseOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFrameworkPauseOwnershipTest::RunTest(const FString& InParameters)
{
    TSharedRef<FFrameworkLifecycleProbe> Probe = MakeShared<FFrameworkLifecycleProbe>(*this);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [Probe]()
        {
            return WaitForEntry(*Probe);
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [Probe]()
        {
            AWandererPlayerController* Player = Probe->PlayerWeakPtr.Get();
            if (!Player)
            {
                return true;
            }
            AWandererGameState* State = Player->GetWorld()->GetGameState<AWandererGameState>();
            Probe->Test.TestFalse(TEXT("Invalid state rejected"),
                State->SetPlayState(static_cast<EWandererPlayState>(255)));
            Probe->Test.TestTrue(
                TEXT("State acquired pause"), State->SetPlayState(EWandererPlayState::PauseMenu));
            Probe->Test.TestTrue(TEXT("World paused"), UGameplayStatics::IsGamePaused(State));
            AActor* Actor = State;
            Actor->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
            Probe->Test.TestFalse(
                TEXT("State released its pause on exit"), UGameplayStatics::IsGamePaused(State));
            Probe->Test.TestFalse(TEXT("Terminal state refuses mutation"),
                State->SetPlayState(EWandererPlayState::Normal));
            UGameplayStatics::SetGamePaused(Player, true);
            AWandererGameState* Observer = Player->GetWorld()->SpawnActor<AWandererGameState>();
            Probe->Test.TestTrue(TEXT("Existing pause may be observed"),
                Observer->SetPlayState(EWandererPlayState::PauseMenu));
            Actor = Observer;
            Actor->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
            Probe->Test.TestTrue(
                TEXT("Another owner's pause survives"), UGameplayStatics::IsGamePaused(Player));
            UGameplayStatics::SetGamePaused(Player, false);
            return true;
        }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
