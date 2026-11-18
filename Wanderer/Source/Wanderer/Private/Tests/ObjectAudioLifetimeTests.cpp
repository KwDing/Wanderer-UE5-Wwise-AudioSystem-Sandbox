#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"

#include "Actors/WandererSpeakerActor.h"
#include "Data/Audio/WandererObjectAudioProfile.h"
#include "Data/Audio/WandererRadioAudioSettings.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

namespace
{
    struct FObjectAudioTestWorld
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        UWandererAudioSubsystem* Audio = World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr;

        FObjectAudioTestWorld()
        {
            if (World && Audio)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                World->InitializeActorsForPlay(FURL());
                Audio->OnWorldBeginPlay(*World);
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

        ~FObjectAudioTestWorld()
        {
            if (World)
            {
                World->BeginTearingDown();
                World->EndPlay(EEndPlayReason::Quit);
                World->DestroyWorld(false);
                if (Audio)
                {
                    GEngine->DestroyWorldContext(World);
                }
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectAudioLifecycleTest,
    "Wanderer.Audio.Object.RegistrationAndAttachment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FObjectAudioLifecycleTest::RunTest(const FString& InParameters)
{
    const FObjectAudioTestWorld Fixture;
    auto* Audio = Fixture.Audio;
    if (!TestNotNull(TEXT("World audio subsystem"), Audio))
    {
        return false;
    }
    auto* Speaker = Fixture.World->SpawnActor<AWandererSpeakerActor>();
    Speaker->SetPreview(true);
    Speaker->RequestPlayback(TEXT("RECORDER_SONG_DEFAULT"), true);
    TestFalse(TEXT("Preview rejects playback"), Speaker->IsPlaybackRequested());
    TestFalse(TEXT("Preview has no registration"), Speaker->GetEmitterHandle().IsSet());
    const FGuid InstanceID = Speaker->GetDeviceID();
    if (!TestTrue(TEXT("Confirming placement registers an emitter"), Speaker->ConfirmAudioPlacement(InstanceID)))
    {
        return false;
    }
    const FObjectAudioEmitterHandle OldHandle = Speaker->GetEmitterHandle();
    const auto OldDeviceID = Speaker->GetDeviceID();
    FTransform Transform;
    TestTrue(TEXT("Placed anchor resolves"), Audio->ResolveObjectEmitter(OldHandle, Transform));

    auto* EquippedOwner = Fixture.World->SpawnActor<AWandererSpeakerActor>();
    auto* Target = EquippedOwner->GetEmitterAnchor();
    EquippedOwner->SetActorLocation(FVector(400, 15, 10));
    FObjectAudioAttachment Attachment{Target, TEXT("MissingSocket"), FTransform(FVector(30, 0, 0)), false};
    TestFalse(TEXT("Missing strict socket rejects a rebind"), Audio->UpdateObjectEmitter(OldHandle, Attachment));
    Attachment.bFallbackToComponent = true;
    TestTrue(TEXT("Fallback allows rebinding to the equipment component"),
        Audio->UpdateObjectEmitter(OldHandle, Attachment));
    TestTrue(TEXT("Equipment attachment applies the component transform and local offset"),
        Audio->ResolveObjectEmitter(OldHandle, Transform)
            && Transform.GetLocation().Equals(Target->GetComponentLocation() + FVector(30, 0, 0)));
    const FObjectAudioTestWorld OtherWorld;
    if (!TestNotNull(TEXT("Second world audio subsystem"), OtherWorld.Audio))
    {
        return false;
    }
    const auto* OtherSpeaker = OtherWorld.World->SpawnActor<AWandererSpeakerActor>();
    Attachment.ComponentWeakPtr = OtherSpeaker->GetEmitterAnchor();
    TestFalse(TEXT("Objects cannot attach across worlds"), Audio->UpdateObjectEmitter(OldHandle, Attachment));

    Speaker->WithdrawAudioEmitter();
    Speaker->Destroy();
    auto* Replacement = Fixture.World->SpawnActor<AWandererSpeakerActor>();
    TestTrue(TEXT("The same inventory instance can be placed again"), Replacement->ConfirmAudioPlacement(InstanceID));
    TestTrue(TEXT("The new actor keeps a separate UI DeviceID"), Replacement->GetDeviceID() != OldDeviceID);
    const auto NewHandle = Replacement->GetEmitterHandle();
    TestTrue(TEXT("Re-registration changes the generation"), NewHandle.Generation != OldHandle.Generation);
    Audio->UnregisterObjectEmitter(OldHandle);
    TestTrue(TEXT("An old unregister cannot remove the replacement"), Audio->ResolveObjectEmitter(NewHandle, Transform));
    TestFalse(TEXT("The old generation cannot resolve"), Audio->ResolveObjectEmitter(OldHandle, Transform));
    FRadioEmitterSnapshot StaleSnapshot;
    StaleSnapshot.ContentID = TEXT("RECORDER_SONG_DEFAULT");
    StaleSnapshot.Number = 1;
    StaleSnapshot.bPlaying = true;
    StaleSnapshot.EmitterHandle = OldHandle;
    FString Status;
    TestFalse(TEXT("Radio rejects a start from a previously returned generation"),
        Audio->StartRadioPlayback(Replacement, StaleSnapshot.ContentID, {StaleSnapshot}, Status));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectAudioConcurrencyTest,
    "Wanderer.Audio.Object.ConcurrencyAndMedia",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FObjectAudioConcurrencyTest::RunTest(const FString& InParameters)
{
    if (!TestTrue(TEXT("Wwise initialized"), FAkAudioDevice::Get() && FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    const auto Fixture = MakeShared<FObjectAudioTestWorld>();
    auto* Audio = Fixture->Audio;
    if (!TestNotNull(TEXT("World audio subsystem"), Audio))
    {
        return false;
    }
    auto* Owner = Fixture->World->SpawnActor<AWandererSpeakerActor>();
    const FObjectAudioAttachment Attachment{Owner->GetEmitterAnchor(), NAME_None, FTransform::Identity, false};
    auto* Profile = NewObject<UWandererObjectAudioProfile>();
    const TSoftObjectPtr<UAkAudioEvent> Event(FSoftObjectPath(
        TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_snow.Play_Amb_snow")));
    const TSoftObjectPtr<UAkAudioEvent> ShortEvent(FSoftObjectPath(
        TEXT("/Game/WwiseAudio/Events/UI/Play_Map_Open.Play_Map_Open")));
    const TSoftObjectPtr<UAkAudioEvent> OtherShortEvent(FSoftObjectPath(
        TEXT("/Game/WwiseAudio/Events/UI/Play_Map_Cancel.Play_Map_Cancel")));
    const FName Loop(TEXT("Loop"));
    const FName AlternateLoop(TEXT("AlternateLoop"));
    const FName Short(TEXT("Short"));
    const FName OtherShort(TEXT("OtherShort"));
    Profile->Events = {{Loop, Event, true}, {AlternateLoop, Event, true},
        {Short, ShortEvent, false}, {OtherShort, OtherShortEvent, false}};
    // This test covers concurrency after cold loading; deadline expiry is tested in Rural.
    for (FObjectAudioEvent& Entry : Profile->Events)
    {
        Entry.StartDeadlineSec = 10;
    }
    const FGuid InstanceID = FGuid::NewGuid();
    const auto Handle = Audio->RegisterObjectEmitter(InstanceID, *Owner, Attachment, Profile);
    if (!TestTrue(TEXT("Generic object registered"), Handle.IsSet()))
    {
        return false;
    }
    using EResult = EObjectAudioRequestResult;
    TestTrue(TEXT("First loop reserves one instance"), Audio->PostObjectEvent(Handle, Loop) == EResult::LoadingMedia);
    TestTrue(TEXT("Duplicate loop is coalesced"), Audio->PostObjectEvent(Handle, Loop) == EResult::AlreadyRequested);
    TestTrue(TEXT("Another loop exceeds the default limit"), Audio->PostObjectEvent(Handle, AlternateLoop) == EResult::AtCapacity);
    TestTrue(TEXT("First short request reserves one instance"), Audio->PostObjectEvent(Handle, Short) == EResult::LoadingMedia);
    TestTrue(TEXT("Second short request reserves one instance"), Audio->PostObjectEvent(Handle, OtherShort) == EResult::LoadingMedia);
    TestTrue(TEXT("Third short request exceeds the default limit"), Audio->PostObjectEvent(Handle, Short) == EResult::AtCapacity);
    const auto PendingStats = Audio->GetObjectEmitterStats(Handle);
    TestEqual(TEXT("Four configured semantic Events"), PendingStats.ConfiguredEvents, 4);
    TestEqual(TEXT("Three pending requests"), PendingStats.PendingInstances, 3);
    TestEqual(TEXT("Asynchronous media preparation occupies no slot"), PendingStats.PhysicalSlots, 0);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Fixture, Audio, Owner, Attachment, Handle, Loop, Event, Deadline = FPlatformTime::Seconds() + 10]()
        {
            Audio->Tick(0.25f);
            const auto Stats = Audio->GetObjectEmitterStats(Handle);
            if (Stats.PlayingInstances != 3)
            {
                if (FPlatformTime::Seconds() < Deadline)
                {
                    return false;
                }
                AddError(FString::Printf(TEXT("Three accepted Events should Post after async media is ready: ")
                    TEXT("pending=%d playing=%d slots=%d admitted=%d failure=%d | %s"),
                    Stats.PendingInstances, Stats.PlayingInstances, Stats.PhysicalSlots,
                    Stats.bAdmitted, static_cast<int32>(Stats.LastFailure), *Audio->GetSummary()));
                return true;
            }
            TestEqual(TEXT("The three PlayingIDs share one physical slot"), Stats.PhysicalSlots, 1);
            Audio->UnregisterObjectEmitter(Handle);
            TestTrue(TEXT("Old handle may no longer request an Event"),
                Audio->PostObjectEvent(Handle, Loop) == EObjectAudioRequestResult::InvalidEmitter);
            auto* Profile = NewObject<UWandererObjectAudioProfile>();
            Profile->Events = {{Loop, Event, true}};
            const auto Replacement = Audio->RegisterObjectEmitter(Handle.InstanceID, *Owner, Attachment, Profile);
            TestTrue(TEXT("Replacement may queue a new request"),
                Audio->PostObjectEvent(Replacement, Loop) == EObjectAudioRequestResult::LoadingMedia);
            Audio->StopObjectEvent(Replacement, Loop);
            Audio->Tick(0.25f);
            const auto Cancelled = Audio->GetObjectEmitterStats(Replacement);
            TestEqual(TEXT("Cancellation removes the replacement request"), Cancelled.PendingInstances, 0);
            TestEqual(TEXT("Cancellation prevents a late Post"), Cancelled.PlayingInstances, 0);
            return true;
        }));
    return true;
}
#endif
