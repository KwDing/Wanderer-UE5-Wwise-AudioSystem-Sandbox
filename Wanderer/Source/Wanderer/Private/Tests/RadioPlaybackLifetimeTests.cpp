#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Tests/AutomationCommon.h"
#include "UObject/GarbageCollection.h"

#include "AkAudioDevice.h"
#include "AkComponent.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/RadioPlaybackManager.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

struct FRadioPlaybackTestAccess
{
    static TArray<UAkComponent*> Emitters(
        const UWandererAudioSubsystem& InAudio, AActor* InOwner, FName InContentID)
    {
        const auto* Session = InAudio.RadioSessions.Find(InOwner);
        const auto* Group = Session ? (*Session)->FindSyncGroup(InContentID) : nullptr;
        TArray<UAkComponent*> Result;
        if (Group)
        {
            Result.Add(Group->Emitter.Get());
        }
        return Result;
    }

    static UAudioEmitterPool* Pool(const UWandererAudioSubsystem& InAudio)
    {
        return InAudio.EmitterPool;
    }

    static bool IsPaused(const UWandererAudioSubsystem& InAudio, AActor* InOwner, FName InContentID)
    {
        const auto* Session = InAudio.RadioSessions.Find(InOwner);
        const auto* Group = Session ? (*Session)->FindSyncGroup(InContentID) : nullptr;
        return Group && Group->bPaused;
    }

    static bool HasResources(const UWandererAudioSubsystem& InAudio, AActor* InOwner)
    {
        const auto* Session = InAudio.RadioSessions.Find(InOwner);
        return Session && ((*Session)->MediaRequest || (*Session)->EventLeases
            || (*Session)->PlayRadioEvent || (*Session)->StopRadioEvent
            || !(*Session)->PendingStarts.IsEmpty() || !(*Session)->SyncGroups.IsEmpty());
    }

    static bool MediaReady(const UWandererAudioSubsystem& InAudio, AActor* InOwner)
    {
        const auto* Session = InAudio.RadioSessions.Find(InOwner);
        return Session && (*Session)->MediaStatus == EAudioEventResourceStatus::Ready;
    }
};

namespace
{
    const FName RadioTestSong(TEXT("RECORDER_SONG_DEFAULT"));

    struct FRadioPlaybackTestWorld
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        UWorld* OtherWorld = nullptr;
        TWeakObjectPtr<UAkComponent> PreviousListener = FAkAudioDevice::Get()->GetSpatialAudioListener();

        FRadioPlaybackTestWorld()
        {
            if (World)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                World->InitializeActorsForPlay(FURL());
                auto* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
                Budget->RegularSlots = 3;
                Budget->TransitionSlots = 2;
                Budget->EmergencySlots = 1;
                World->GetSubsystem<UWandererAudioSubsystem>()->OnWorldBeginPlay(*World);
                World->GetWorldSettings()->NotifyBeginPlay();
                auto* Player = World->SpawnActor<APlayerController>();
                auto* Pawn = World->SpawnActor<APawn>();
                auto* Root = NewObject<USceneComponent>(Pawn);
                Pawn->SetRootComponent(Root);
                Pawn->AddInstanceComponent(Root);
                Root->RegisterComponent();
                Player->Possess(Pawn);
            }
        }

        ~FRadioPlaybackTestWorld()
        {
            if (OtherWorld)
            {
                OtherWorld->DestroyWorld(false);
            }
            if (World)
            {
                World->BeginTearingDown();
                World->EndPlay(EEndPlayReason::Quit);
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
            if (FAkAudioDevice* Device = FAkAudioDevice::Get())
            {
                Device->SetSpatialAudioListener(PreviousListener.Get());
            }
        }
    };

    TArray<FRadioEmitterSnapshot> MakeRadioTestSources()
    {
        FRadioEmitterSnapshot Source;
        Source.Number = 1;
        Source.ContentID = RadioTestSong;
        Source.Transform.SetLocation(FVector(100.0, 0.0, 0.0));
        Source.bPlaying = true;
        return {Source};
    }

    UAkComponent* MakeRadioTestListener(UWorld& InWorld)
    {
        AActor* Owner = InWorld.SpawnActor<AActor>();
        UAkComponent* Listener = NewObject<UAkComponent>(Owner);
        Listener->RegisterComponent();
        return Listener;
    }

    TArray<AkPlayingID> ReadRadioTestPlayingIDs(const TArray<UAkComponent*>& Emitters)
    {
        TArray<AkPlayingID> Result;
        IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
        if (!Engine || !Engine->Query)
        {
            return Result;
        }
        for (const UAkComponent* Emitter : Emitters)
        {
            AkUInt32 Count = 1;
            AkPlayingID ID = AK_INVALID_PLAYING_ID;
            if (Engine->Query->GetPlayingIDsFromGameObject(Emitter->GetAkGameObjectID(), Count, &ID)
                    == AK_Success && Count == 1 && ID != AK_INVALID_PLAYING_ID)
            {
                Result.Add(ID);
            }
        }
        Result.Sort();
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioPlaybackLifetimeTest,
    "Wanderer.RadioPlayback.SessionOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadioPlaybackLifetimeTest::RunTest(const FString& InParameters)
{
    if (!TestTrue(TEXT("Wwise initialized"), FAkAudioDevice::Get() && FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    const auto Fixture = MakeShared<FRadioPlaybackTestWorld>();
    UWorld* World = Fixture->World;
    if (!TestNotNull(TEXT("Session world"), World))
    {
        return false;
    }
    auto* Audio = World->GetSubsystem<UWandererAudioSubsystem>();
    AActor* Owner = World->SpawnActor<AActor>();
    AActor* Other = World->SpawnActor<AActor>();
    FString Status;
    const auto Sources = MakeRadioTestSources();
    if (!TestTrue(TEXT("Cold requests are accepted"), Audio
        && Audio->StartRadioPlayback(Owner, RadioTestSong, Sources, Status)
        && Audio->StartRadioPlayback(Other, RadioTestSong, Sources, Status)))
    {
        return false;
    }
    TestTrue(TEXT("Cold request waits for media"),
        Audio->GetRadioPlaybackPhase(Owner, RadioTestSong) == ERadioPlaybackPhase::LoadingMedia);
    TestFalse(TEXT("Cold request does not create a group"), Audio->HasRadioContent(Owner, RadioTestSong));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Fixture, Audio, Owner, Other, Sources, Deadline = FPlatformTime::Seconds() + 10]()
        {
            TArray<FName> Failed;
            Audio->Tick(0.25f);
            Audio->UpdateRadioPlayback(Owner, Sources, 0, Failed);
            Audio->UpdateRadioPlayback(Other, Sources, 0, Failed);
            if (!Audio->HasRadioContent(Owner, RadioTestSong) || !Audio->HasRadioContent(Other, RadioTestSong))
            {
                if (Failed.IsEmpty() && FPlatformTime::Seconds() < Deadline)
                {
                    return false;
                }
                AddError(TEXT("Both asynchronous owner sessions must post"));
                return true;
            }
            auto Emitters = FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong);
            TestEqual(TEXT("One emitter is leased for the owner's content"), Emitters.Num(), 1);
            TArray<TWeakObjectPtr<UAkComponent>> EmittersWeakPtrs;
            for (UAkComponent* Emitter : Emitters)
            {
                EmittersWeakPtrs.Add(Emitter);
            }
            Emitters.Reset();
            CollectGarbage(RF_NoFlags, true);
            TestTrue(TEXT("Session resource graph survives GC"), Audio->HasRadioContent(Owner, RadioTestSong));
            Audio->ReleaseRadioPlayback(Owner);
            TestFalse(TEXT("Released owner has no retained session"), Audio->HasRadioContent(Owner, RadioTestSong));
            TestTrue(TEXT("Releasing one owner preserves the other"), Audio->HasRadioContent(Other, RadioTestSong));
            TestTrue(TEXT("The surviving session can continue"), Audio->UpdateRadioPlayback(Other, Sources, 0, Failed));
            Audio->ReleaseRadioPlayback(Owner);
            Audio->Deinitialize();
            TestFalse(TEXT("Deinitialize releases remaining owner"), Audio->HasRadioContent(Other, RadioTestSong));
            CollectGarbage(RF_NoFlags, true);
            for (const auto& Emitter : EmittersWeakPtrs)
            {
                TestFalse(TEXT("Released emitter is not retained"), Emitter.IsValid());
            }
            return true;
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioPlaybackReentrantReleaseTest,
    "Wanderer.RadioPlayback.ReleaseDuringEmitterUpdate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FRadioPlaybackReentrantReleaseTest::RunTest(const FString& InParameters)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    if (!TestTrue(TEXT("Wwise initialized"), Device && FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    const auto Fixture = MakeShared<FRadioPlaybackTestWorld>();
    UWorld* World = Fixture->World;
    if (!TestNotNull(TEXT("Session world"), World))
    {
        return false;
    }
    auto* Audio = World->GetSubsystem<UWandererAudioSubsystem>();
    AActor* Owner = World->SpawnActor<AActor>();
    if (!TestTrue(TEXT("Listener belongs to the test world"),
            Audio && Device->SetSpatialAudioListener(MakeRadioTestListener(*World))))
    {
        return false;
    }
    FString Status;
    const auto Sources = MakeRadioTestSources();
    if (!TestTrue(TEXT("Accepted test session start"),
        Audio->StartRadioPlayback(Owner, RadioTestSong, Sources, Status)))
    {
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Fixture, Audio, Owner, Sources, Deadline = FPlatformTime::Seconds() + 10]()
        {
            TArray<FName> Failed;
            Audio->Tick(0.25f);
            Audio->UpdateRadioPlayback(Owner, Sources, 0, Failed);
            if (!Audio->HasRadioContent(Owner, RadioTestSong))
            {
                if (Failed.IsEmpty() && FPlatformTime::Seconds() < Deadline)
                {
                    return false;
                }
                AddError(TEXT("Asynchronous session must post"));
                return true;
            }
            auto Emitters = FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong);
            int32 ReleaseCount = 0;
            TArray<FDelegateHandle> Handles;
            for (UAkComponent* Emitter : Emitters)
            {
                Handles.Add(Emitter->TransformUpdated.AddLambda(
                    [Audio, Owner, &ReleaseCount](USceneComponent*, EUpdateTransformFlags, ETeleportType)
                    {
                        ++ReleaseCount;
                        Audio->ReleaseRadioPlayback(Owner);
                    }));
            }
            auto Moved = Sources;
            Moved[0].Transform.SetLocation(FVector(200, 0, 0));
            TestFalse(TEXT("An update interrupted by release stops"),
                Audio->UpdateRadioPlayback(Owner, Moved, 0, Failed));
            TestEqual(TEXT("Release was reentrant during the transform update"), ReleaseCount, 1);
            TestFalse(TEXT("Released session no longer registered"), Audio->HasRadioContent(Owner, RadioTestSong));
            for (int32 Index = 0; Index < Emitters.Num(); ++Index)
            {
                Emitters[Index]->TransformUpdated.Remove(Handles[Index]);
                TestFalse(TEXT("Returned emitter has Tick disabled"),
                    Emitters[Index]->IsComponentTickEnabled());
            }
            TestTrue(TEXT("Deferred shutdown returned all session emitters"),
                FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong).IsEmpty());
            return true;
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioPlaybackListenerRecoveryTest,
    "Wanderer.RadioPlayback.ListenerRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FRadioPlaybackListenerRecoveryTest::RunTest(const FString& InParameters)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    if (!TestTrue(TEXT("Wwise initialized"), Device && FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    const auto Fixture = MakeShared<FRadioPlaybackTestWorld>();
    Fixture->OtherWorld = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestTrue(TEXT("Two isolated test worlds"), Fixture->World && Fixture->OtherWorld))
    {
        return false;
    }
    auto* Audio = Fixture->World->GetSubsystem<UWandererAudioSubsystem>();
    AActor* Owner = Fixture->World->SpawnActor<AActor>();
    AActor* CancelledOwner = Fixture->World->SpawnActor<AActor>();
    UAkComponent* CorrectListener = MakeRadioTestListener(*Fixture->World);
    Device->SetSpatialAudioListener(nullptr);
    TestNull(TEXT("Begin with no spatial listener"), Device->GetSpatialAudioListener());
    FString Status;
    const auto Sources = MakeRadioTestSources();
    if (!TestTrue(TEXT("Accepted the two cold requests"), Audio
        && Audio->StartRadioPlayback(Owner, RadioTestSong, Sources, Status)
        && Audio->StartRadioPlayback(CancelledOwner, RadioTestSong, Sources, Status)))
    {
        return false;
    }
    TArray<FName> Failed;
    Audio->UpdateRadioPlayback(CancelledOwner, {}, 0, Failed);
    TestTrue(TEXT("Withdrawn intent cancels an unposted start"),
        Audio->GetRadioPlaybackPhase(CancelledOwner, RadioTestSong) == ERadioPlaybackPhase::Stopped);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Fixture, Audio, Device, Owner, CancelledOwner, CorrectListener, Sources,
            Deadline = FPlatformTime::Seconds() + 10]()
        {
            TArray<FName> Failures;
            Audio->Tick(0.25f);
            Audio->UpdateRadioPlayback(CancelledOwner, {}, 0, Failures);
            Audio->UpdateRadioPlayback(Owner, Sources, 0, Failures);
            const auto InitialIDs = ReadRadioTestPlayingIDs(FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong));
            if (InitialIDs.Num() != 1)
            {
                if (Failures.IsEmpty() && FPlatformTime::Seconds() < Deadline)
                {
                    return false;
                }
                AddError(TEXT("One PlayingID must post while waiting for a listener"));
                return true;
            }
            TestFalse(TEXT("No late Post after withdrawal"), Audio->HasRadioContent(CancelledOwner, RadioTestSong));
            TestTrue(TEXT("Missing listener keeps the live group"),
                Audio->GetRadioPlaybackPhase(Owner, RadioTestSong) == ERadioPlaybackPhase::WaitingForListener);
            UAkComponent* ForeignListener = MakeRadioTestListener(*Fixture->OtherWorld);
            TestTrue(TEXT("Install listener from other world"), Device->SetSpatialAudioListener(ForeignListener));
            TestTrue(TEXT("Cross-world listener is a normal wait"),
                Audio->UpdateRadioPlayback(Owner, Sources, 0.05f, Failures));
            TestTrue(TEXT("Cross-world wait preserves all IDs"), ReadRadioTestPlayingIDs(FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong)) == InitialIDs);
            TestTrue(TEXT("Install correct-world listener"), Device->SetSpatialAudioListener(CorrectListener));
            auto Moved = Sources;
            const FVector Location(250, 10, 0);
            Moved[0].Transform.SetLocation(Location);
            TestTrue(TEXT("Correct-world listener automatically resumes spatial updates"),
                Audio->UpdateRadioPlayback(Owner, Moved, 0.05f, Failures));
            TestTrue(TEXT("Group is ready without re-Post"),
                Audio->GetRadioPlaybackPhase(Owner, RadioTestSong) == ERadioPlaybackPhase::Ready
                    && ReadRadioTestPlayingIDs(FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong)) == InitialIDs);
            auto Emitters = FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong);
            TestTrue(TEXT("Restored listener applies the latest transform"),
                Emitters.ContainsByPredicate([Location](const UAkComponent* Emitter)
                {
                    return Emitter->GetComponentLocation().Equals(Location);
                }));
            TestTrue(TEXT("Listener loss produces no failed content"), Failures.IsEmpty());
            FRadioEmitterSnapshot Partner = Moved[0];
            Partner.Number = 2;
            Partner.Transform.SetLocation(FVector(350, -20, 0));
            Moved.Add(Partner);
            TestTrue(TEXT("Adding a second position reuses the existing content"),
                Audio->UpdateRadioPlayback(Owner, Moved, 0.05f, Failures));
            TestEqual(TEXT("Same-content devices use one component"),
                FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong).Num(), 1);

            const TArray<FRadioEmitterSnapshot> Remaining{Partner};
            TestTrue(TEXT("Removing the anchor preserves playback at the remaining position"),
                Audio->UpdateRadioPlayback(Owner, Remaining, 0.05f, Failures)
                    && ReadRadioTestPlayingIDs(Emitters) == InitialIDs
                    && Emitters.Num() == 1
                    && Emitters[0]->GetComponentLocation().Equals(Partner.Transform.GetLocation()));
            const auto State = [Audio, Owner] { return FRadioPlaybackTestAccess::IsPaused(
                *Audio, Owner, RadioTestSong); };
            Partner.bPlaying = false;
            TestTrue(TEXT("Pausing the content retains its PlayingID"),
                Audio->UpdateRadioPlayback(Owner, {Partner}, 0.05f, Failures)
                    && State() && ReadRadioTestPlayingIDs(Emitters) == InitialIDs);
            TestTrue(TEXT("Redeploying resumes the same PlayingID"),
                Audio->UpdateRadioPlayback(Owner, Remaining, 0.05f, Failures)
                    && !State() && ReadRadioTestPlayingIDs(Emitters) == InitialIDs);
            TestTrue(TEXT("Position, pause and retrieval updates report no failure"), Failures.IsEmpty());
            return true;
        }));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioPlaybackDeploymentTest,
    "Wanderer.RadioPlayback.DeploymentResidency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FRadioPlaybackDeploymentTest::RunTest(const FString& InParameters)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    if (!TestTrue(TEXT("Wwise initialized"), Device && FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    const auto Fixture = MakeShared<FRadioPlaybackTestWorld>();
    UWorld* World = Fixture->World;
    if (!TestNotNull(TEXT("Deployment test world"), World))
    {
        return false;
    }
    UWandererAudioSubsystem* Audio = World->GetSubsystem<UWandererAudioSubsystem>();
    AActor* Owner = World->SpawnActor<AActor>();
    if (!Owner || !Device->SetSpatialAudioListener(MakeRadioTestListener(*World)))
    {
        AddError(TEXT("Deployment owner and listener must initialize"));
        return false;
    }
    TestFalse(TEXT("An unused inventory owns no radio media or components"),
        FRadioPlaybackTestAccess::HasResources(*Audio, Owner));
    auto Sources = MakeRadioTestSources();
    Sources[0].bPlaying = false;
    FString Status;
    TestTrue(TEXT("Placement accepts asynchronous preloading"), Audio->PreloadRadioPlayback(Owner));
    TestTrue(TEXT("Preload keeps media pending"),
        FRadioPlaybackTestAccess::HasResources(*Audio, Owner));
    TestTrue(TEXT("Preload has no playback component"),
        FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong).IsEmpty());
    Sources[0].bPlaying = true;
    if (!TestTrue(TEXT("Play queues on the deployed session"),
            Audio->StartRadioPlayback(Owner, RadioTestSong, Sources, Status)))
    {
        return false;
    }

    struct FDeploymentState
    {
        int32 Stage = 0;
        int32 SavedPositionMs = 0;
        uint32 PreviousPlayingID = 0;
        TWeakObjectPtr<UAkComponent> PreviousEmitter;
    };
    const auto State = MakeShared<FDeploymentState>();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Fixture, Audio, Owner, Sources, State,
            Deadline = FPlatformTime::Seconds() + 20]() mutable
        {
            if (FPlatformTime::Seconds() > Deadline)
            {
                AddError(FString::Printf(TEXT("Deployment residency timed out at stage %d"),
                    State->Stage));
                return true;
            }
            TArray<FName> Failures;
            Audio->Tick(0.25f);
            if (State->Stage == 1)
            {
                TestFalse(TEXT("Retrieval retains value-only bookmarks"),
                    FRadioPlaybackTestAccess::HasResources(*Audio, Owner));
                if (UAkComponent* Old = State->PreviousEmitter.Get();
                    Old && Old->IsRegisteredWithWwise())
                {
                    return false;
                }
                TestTrue(TEXT("Retrieved content retains a saved position"),
                    Audio->GetRadioPosition(Owner, 1, RadioTestSong) >= 100);
                FString ResumeStatus;
                TestTrue(TEXT("Redeployment accepts an asynchronous resume"),
                    Audio->StartRadioPlayback(Owner, RadioTestSong, Sources, ResumeStatus));
                State->Stage = 2;
            }
            if (!Audio->UpdateRadioPlayback(Owner, Sources, 0.05f, Failures)
                || !Failures.IsEmpty())
            {
                AddError(TEXT("Deployment update must succeed"));
                return true;
            }
            const auto Emitters = FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong);
            const auto IDs = ReadRadioTestPlayingIDs(Emitters);
            const int32 PositionMs = Audio->GetRadioPosition(Owner, 1, RadioTestSong);
            if (IDs.Num() != 1 || PositionMs < 100)
            {
                return false;
            }
            if (State->Stage == 2)
            {
                AkTimeMs NativePositionMs = 0;
                IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
                if (!Engine || Engine->GetSourcePlayPosition(IDs[0], &NativePositionMs, false)
                        != AK_Success
                    || NativePositionMs < State->SavedPositionMs - 50)
                {
                    return false;
                }
                TestTrue(TEXT("Resume uses a new PlayingID after asynchronous resource retirement"),
                    IDs[0] != State->PreviousPlayingID);
                TestTrue(TEXT("New playback restores its saved position"),
                    NativePositionMs >= State->SavedPositionMs - 50);
                FRadioEmitterSnapshot Replacement = Sources[0];
                Replacement.ContentID = TEXT("RECORDER_NOISE");
                Replacement.bPlaying = false;
                TestTrue(TEXT("Removing the last device of one content releases that group's slot"),
                    Audio->UpdateRadioPlayback(Owner, {Replacement}, 0, Failures)
                        && FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong).IsEmpty());
                TestTrue(TEXT("An idle replacement content has no playback component"),
                    FRadioPlaybackTestAccess::Emitters(
                        *Audio, Owner, Replacement.ContentID).IsEmpty());
                Replacement.bPlaying = true;
                FString NewContentStatus;
                TestTrue(TEXT("A different content creates its own component only on Play"),
                    Audio->StartRadioPlayback(Owner, Replacement.ContentID,
                        {Replacement}, NewContentStatus));
                return true;
            }

            State->SavedPositionMs = PositionMs;
            State->PreviousPlayingID = IDs[0];
            State->PreviousEmitter = Emitters[0];
            TestTrue(TEXT("Retrieving all accepts an empty position list"),
                Audio->UpdateRadioPlayback(Owner, {}, 0, Failures));
            TestTrue(TEXT("Retrieval keeps the content bookmark"),
                Audio->HasRadioContent(Owner, RadioTestSong));
            TestTrue(TEXT("Retrieval returns the content's component"),
                FRadioPlaybackTestAccess::Emitters(*Audio, Owner, RadioTestSong).IsEmpty());
            TestFalse(TEXT("Retrieval releases the session's media lease and async request"),
                FRadioPlaybackTestAccess::HasResources(*Audio, Owner));
            State->Stage = 1;
            return false;
        }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioEmitterPoolBatchTest,
    "Wanderer.RadioPlayback.PoolBatchAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FRadioEmitterPoolBatchTest::RunTest(const FString& InParameters)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
    if (!TestTrue(TEXT("Wwise initialized"), Device && FAkAudioDevice::IsInitialized()
        && Engine && Engine->Query))
    {
        return false;
    }
    const auto Fixture = MakeShared<FRadioPlaybackTestWorld>();
    if (!TestNotNull(TEXT("Test world"), Fixture->World))
    {
        return false;
    }
    auto* Audio = Fixture->World->GetSubsystem<UWandererAudioSubsystem>();
    auto* Pool = FRadioPlaybackTestAccess::Pool(*Audio);
    AActor* FirstOwner = Fixture->World->SpawnActor<AActor>();
    AActor* SecondOwner = Fixture->World->SpawnActor<AActor>();
    if (!TestTrue(TEXT("Reset test asynchronously preloads its Switch and RTPC definitions"),
            Audio->PreloadRadioPlayback(FirstOwner)))
    {
        return false;
    }

    struct FResetProbe
    {
        int32 Stage = 0;
        FString LastResult;
        TArray<UAkComponent*> Emitters;
        UAkComponent* Held = nullptr;
        UAkComponent* First = nullptr;
        int32 Capacity = 0;
    };
    const auto Probe = MakeShared<FResetProbe>();
    auto PreloadSources = MakeRadioTestSources();
    PreloadSources[0].bPlaying = false;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Fixture, Audio, Pool, FirstOwner, SecondOwner, Engine, PreloadSources, Probe,
            Deadline = FPlatformTime::Seconds() + 10]()
        {
            if (FPlatformTime::Seconds() > Deadline)
            {
                AddError(FString::Printf(TEXT("Pool reset timed out at stage %d: %s"),
                    Probe->Stage, *Probe->LastResult));
                return true;
            }
            if (Probe->Stage == 0)
            {
                TArray<FName> Failures;
                Audio->Tick(0.25f);
                Audio->UpdateRadioPlayback(FirstOwner, PreloadSources, 0, Failures);
                if (!FRadioPlaybackTestAccess::MediaReady(*Audio, FirstOwner))
                {
                    return false;
                }
                Probe->Held = Pool->Acquire(*FirstOwner, false);
                if (!TestNotNull(TEXT("A warmed emitter is available"), Probe->Held))
                {
                    return true;
                }
                TestFalse(TEXT("A three-slot request waits with two regular slots available"),
                    Pool->AcquireBatch(*SecondOwner, 3, Probe->Emitters));
                TestTrue(TEXT("The failed batch returns no slots"), Probe->Emitters.IsEmpty());
                if (!TestTrue(TEXT("Both regular slots remain available"),
                        Pool->AcquireBatch(*SecondOwner, 2, Probe->Emitters)))
                {
                    return true;
                }
                // Poison a batch slot which will be included in the next batch borrow.
                Probe->First = Probe->Emitters[0];
                Probe->Capacity = Pool->GetStats().Capacity;
                const AkGameObjectID ObjectID = Probe->First->GetAkGameObjectID();
                TestTrue(TEXT("Write the previous switch"),
                    Engine->SetSwitch("RadioPlayback", "Noise", ObjectID) == AK_Success);
                TestTrue(TEXT("Write the previous object gain"),
                    Engine->SetRTPCValue("DeviceGain", 0.35f, ObjectID, 0) == AK_Success);
                Probe->First->SetWorldLocation(FVector(100, 0, 0));
                Probe->First->SetComponentTickInterval(0.05f);
                Probe->First->SetComponentTickEnabled(true);
                Probe->Stage = 1;
                return false;
            }

            using namespace AK::SoundEngine::Query;
            const AkGameObjectID ObjectID = Probe->First->GetAkGameObjectID();
            AkSwitchStateID Switch = 0;
            AkRtpcValue Gain = 0;
            RTPCValue_type Scope = RTPCValue_GameObject;
            const AKRESULT SwitchResult = Engine->Query->GetSwitch(
                Engine->GetIDFromString("RadioPlayback"), ObjectID, Switch);
            const AKRESULT GainResult = Engine->Query->GetRTPCValue(
                "DeviceGain", ObjectID, AK_INVALID_PLAYING_ID, Gain, Scope);
            if (Probe->Stage == 1)
            {
                if (SwitchResult != AK_Success || Switch != Engine->GetIDFromString("Noise")
                    || GainResult != AK_Success || Scope != RTPCValue_GameObject
                    || !FMath::IsNearlyEqual(Gain, 0.35f))
                {
                    return false;
                }
                Pool->BeginRadioRelease(*FirstOwner, Probe->Held, nullptr, 0, nullptr, 0);
                for (UAkComponent* Emitter : Probe->Emitters)
                {
                    Pool->BeginRadioRelease(*SecondOwner, Emitter, nullptr, 0, nullptr, 0);
                }
                TArray<UAkComponent*> Reused;
                TestFalse(TEXT("Returned slots wait for cleanup"),
                    Pool->AcquireBatch(*SecondOwner, 3, Reused));
                Audio->Tick(0);
                TestFalse(TEXT("Returning a radio slot unregisters its Wwise object"),
                    Probe->First->IsRegisteredWithWwise());
                if (!TestTrue(TEXT("All drained slots can be borrowed again"),
                        Pool->AcquireBatch(*SecondOwner, 3, Reused)))
                {
                    return true;
                }
                TestEqual(TEXT("Borrowing does not grow the warmed pool"),
                    Pool->GetStats().Capacity, Probe->Capacity);
                TestTrue(TEXT("The modified UE component is reused"), Reused.Contains(Probe->First));
                for (UAkComponent* Emitter : Reused)
                {
                    TestFalse(TEXT("Reused Tick is disabled"), Emitter->IsComponentTickEnabled());
                    TestEqual(TEXT("Tick interval is reset"), Emitter->GetComponentTickInterval(), 0.0f);
                    TestTrue(TEXT("Position is reset"), Emitter->GetComponentLocation().IsZero());
                }
                Probe->Stage = 2;
                return false;
            }

            AkSoundPosition Position;
            const bool bRegistered = Engine->Query->GetPosition(ObjectID, Position) == AK_Success;
            // A registered object without an explicit Switch returns PartialSuccess and ID 0.
            const bool bSwitchCleared = (SwitchResult == AK_PartialSuccess && Switch == 0)
                || (SwitchResult == AK_Success && Switch == Engine->GetIDFromString("Song"));
            const bool bGainCleared = GainResult == AK_Success
                && (Scope == RTPCValue_Global || Scope == RTPCValue_Default);
            Probe->LastResult = FString::Printf(
                TEXT("registered=%d switchResult=%d switch=%u gainResult=%d gain=%f scope=%d"),
                bRegistered, static_cast<int32>(SwitchResult), Switch,
                static_cast<int32>(GainResult), Gain, static_cast<int32>(Scope));
            if (!bRegistered || !bSwitchCleared || !bGainCleared)
            {
                return false;
            }
            TestTrue(TEXT("Wwise registration is restored"), bRegistered);
            TestTrue(TEXT("The old Switch override is cleared"), bSwitchCleared);
            TestTrue(TEXT("The old object RTPC override is cleared"), bGainCleared);
            return true;
        }));
    return true;
}
#endif
