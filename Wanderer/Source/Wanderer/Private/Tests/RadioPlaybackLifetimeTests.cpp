#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"

#include "AkAudioDevice.h"
#include "AkComponent.h"

#include "Systems/Audio/WandererAudioSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioPlaybackLifetimeTest,
    "Wanderer.RadioPlayback.SessionOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadioPlaybackLifetimeTest::RunTest(const FString& InParameters)
{
    if (!TestTrue(TEXT("Wwise initialized; run without -NoSound"), FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Session world"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    UWandererAudioSubsystem* Audio = World->GetSubsystem<UWandererAudioSubsystem>();
    if (!TestNotNull(TEXT("World initialized the audio subsystem"), Audio))
    {
        return false;
    }
    AActor* Owner = World->SpawnActor<AActor>();
    AActor* Other = World->SpawnActor<AActor>();
    const FName Song(TEXT("RECORDER_SONG_DEFAULT"));
    FString Status;
    TArray<FRadioEmitterSnapshot> Sources;
    FRadioEmitterSnapshot& Source = Sources.AddDefaulted_GetRef();
    Source.Number = 1;
    Source.ContentID = Song;
    if (!TestTrue(TEXT("Start first owner session"),
            Audio->StartRadioPlayback(Owner, Song, Sources, Status))
        || !TestTrue(TEXT("Start second owner session"),
            Audio->StartRadioPlayback(Other, Song, Sources, Status)))
    {
        Audio->Deinitialize();
        return false;
    }
    TArray<UAkComponent*> Emitters;
    Owner->GetComponents(Emitters);
    TestEqual(TEXT("Four co-started emitters belong to owner"), Emitters.Num(), 4);
    TArray<TWeakObjectPtr<UAkComponent>> EmittersWeakPtrs;
    for (UAkComponent* Emitter : Emitters)
    {
        EmittersWeakPtrs.Add(Emitter);
    }
    Emitters.Reset();
    CollectGarbage(RF_NoFlags, true);
    TestTrue(TEXT("Session resource graph survives GC"), Audio->HasRadioContent(Owner, Song));
    Audio->ReleaseRadioPlayback(Owner);
    TestFalse(TEXT("Released owner has no retained session"), Audio->HasRadioContent(Owner, Song));
    TestTrue(TEXT("Releasing one owner preserves the other"), Audio->HasRadioContent(Other, Song));
    Audio->ReleaseRadioPlayback(Owner);
    Audio->Deinitialize();
    TestFalse(TEXT("Deinitialize releases remaining owner"), Audio->HasRadioContent(Other, Song));
    CollectGarbage(RF_NoFlags, true);
    for (const TWeakObjectPtr<UAkComponent>& EmitterWeakPtr : EmittersWeakPtrs)
    {
        TestFalse(TEXT("Released emitter is not retained"), EmitterWeakPtr.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadioPlaybackReentrantReleaseTest,
    "Wanderer.RadioPlayback.ReleaseDuringEmitterUpdate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadioPlaybackReentrantReleaseTest::RunTest(const FString& InParameters)
{
    FAkAudioDevice* Device = FAkAudioDevice::Get();
    if (!TestTrue(TEXT("Wwise initialized"), Device && FAkAudioDevice::IsInitialized()))
    {
        return false;
    }
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Session world"), World))
    {
        return false;
    }
    const TWeakObjectPtr<UAkComponent> PreviousListenerWeakPtr(Device->GetSpatialAudioListener());
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        if (UAkComponent* PreviousListener = PreviousListenerWeakPtr.Get())
        {
            Device->SetSpatialAudioListener(PreviousListener);
        }
    };
    UWandererAudioSubsystem* Audio = World->GetSubsystem<UWandererAudioSubsystem>();
    AActor* Owner = World->SpawnActor<AActor>();
    AActor* ListenerOwner = World->SpawnActor<AActor>();
    UAkComponent* Listener = NewObject<UAkComponent>(ListenerOwner);
    Listener->RegisterComponent();
    if (!TestTrue(TEXT("Listener belongs to the test world"),
            Audio && Device->SetSpatialAudioListener(Listener)))
    {
        return false;
    }
    const FName Song(TEXT("RECORDER_SONG_DEFAULT"));
    TArray<FRadioEmitterSnapshot> Sources;
    FRadioEmitterSnapshot& Source = Sources.AddDefaulted_GetRef();
    Source.ContentID = Song;
    Source.Number = 1;
    Source.Transform.SetLocation(FVector(100.0, 0.0, 0.0));
    Source.bPlaying = true;
    FString Status;
    if (!TestTrue(TEXT("Started the test session"),
            Audio->StartRadioPlayback(Owner, Song, Sources, Status)))
    {
        return false;
    }
    TArray<UAkComponent*> Emitters;
    Owner->GetComponents(Emitters);
    int32 ReleaseCount = 0;
    for (UAkComponent* Emitter : Emitters)
    {
        Emitter->TransformUpdated.AddLambda(
            [Audio, Owner, &ReleaseCount](
                USceneComponent*, EUpdateTransformFlags, ETeleportType)
            {
                ++ReleaseCount;
                Audio->ReleaseRadioPlayback(Owner);
            });
    }
    TArray<FName> Failed;
    TestFalse(TEXT("An update interrupted by release stops"),
        Audio->UpdateRadioPlayback(Owner, Sources, 0, Failed));
    TestEqual(TEXT("Release was reentrant during the transform update"), ReleaseCount, 1);
    TestFalse(TEXT("Released session no longer registered"), Audio->HasRadioContent(Owner, Song));
    Owner->GetComponents(Emitters);
    TestTrue(TEXT("Deferred shutdown released all emitters"), Emitters.IsEmpty());
    return true;
}
#endif
