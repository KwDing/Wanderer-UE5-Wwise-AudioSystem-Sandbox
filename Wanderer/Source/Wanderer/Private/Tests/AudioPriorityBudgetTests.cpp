#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioEvent.h"
#include "AkComponent.h"

#include "Data/Audio/WandererObjectAudioProfile.h"
#include "Systems/Audio/AudioEmitterPool.h"
#include "Systems/Audio/AudioEventLeaseManager.h"

struct FAudioPriorityTestAccess
{
    static uint64 RequestOrder(UAkAudioEvent* InEvent)
    {
        return UAudioEventLeaseManager::GetRequestOrder(InEvent);
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioPriorityPoolBudgetTest,
    "Wanderer.Audio.Priority.PoolPartitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FAudioPriorityPoolBudgetTest::RunTest(const FString& InParameters)
{
    for (const int32 RegularSlots : {3, AudioSchedulingDefaults::RegularSlots})
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        if (!TestNotNull(TEXT("Test world"), World))
        {
            return false;
        }
        const TStrongObjectPtr<UAudioEmitterPool> Pool(NewObject<UAudioEmitterPool>(World));
        ON_SCOPE_EXIT
        {
            Pool->Shutdown([](UAkAudioEvent*, uint32, double, float) {});
            World->DestroyWorld(false);
        };
        AActor* Owner = World->SpawnActor<AActor>();
        FAudioBudgetLimits Limits;
        Limits.RegularSlots = RegularSlots;
        Limits.TransitionSlots = 2;
        Limits.EmergencySlots = 1;
        for (int32 Batch = 0; Batch < 3; ++Batch)
        {
            Pool->ConfigureBudget(Limits);
            Pool->Warmup(*World, 1000);
        }
        const int32 Capacity = RegularSlots + 3;
        AddInfo(FString::Printf(TEXT("Pool budget case: %d + 2 + 1"), RegularSlots));
        TestEqual(TEXT("Warmup respects regular, transition and emergency capacity"),
            Pool->GetStats().Capacity, Capacity);
        TArray<UAkComponent*> Regular;
        for (int32 Index = 0; Index < RegularSlots; ++Index)
        {
            UAkComponent* Emitter = Pool->Acquire(*Owner, false);
            if (!TestNotNull(TEXT("Regular slot"), Emitter))
            {
                return false;
            }
            Regular.Add(Emitter);
        }
        TestNull(TEXT("An extra regular cannot consume reserve capacity"), Pool->Acquire(*Owner, false));
        TestNotNull(TEXT("Emergency remains available at regular saturation"),
            Pool->Acquire(*Owner, false, EAudioEmitterLane::Emergency));
        TestNull(TEXT("A second emergency cannot expand the pool"),
            Pool->Acquire(*Owner, false, EAudioEmitterLane::Emergency));
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Pool->BeginRelease(*Owner, Regular[Index], nullptr, 0, 0, 0, NAME_None, false, false);
            TestNotNull(TEXT("A replacement can overlap a retiring emitter"), Pool->Acquire(*Owner, false));
        }
        TestEqual(TEXT("Steady active count respects its case budget"),
            Pool->GetStats().WorldActive, RegularSlots);
        TestEqual(TEXT("Two outgoing emitters overlap"), Pool->GetStats().WorldRetiring, 2);
        Pool->BeginRelease(*Owner, Regular[2], nullptr, 0, 0, 0, NAME_None, false, false);
        TestNull(TEXT("Further replacement waits when both extra slots are occupied"),
            Pool->Acquire(*Owner, false));
        Pool->ConfigureBudget(Limits);
        Pool->Warmup(*World, 1000);
        TestEqual(TEXT("Demand does not grow the configured pool"), Pool->GetStats().Capacity, Capacity);
        Pool->RecycleReadyEmitters([](UAkAudioEvent*) {});
        if (!TestNotNull(TEXT("Completion makes a waiting replacement possible"),
                Pool->Acquire(*Owner, false)))
        {
            return false;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioPriorityMediaQueueTest,
    "Wanderer.Audio.Priority.AsyncMediaOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FAudioPriorityMediaQueueTest::RunTest(const FString& InParameters)
{
    struct FMediaQueueFixture
    {
        TStrongObjectPtr<UAudioEventLeaseManager> Leases{NewObject<UAudioEventLeaseManager>()};
        TArray<FSoftObjectPath> Paths;
        TSharedPtr<FStreamableHandle> Assets;
        TArray<TWeakObjectPtr<UAkAudioEvent>> Events;
        double DeadlineSec = FPlatformTime::Seconds() + 15;
        bool bQueued = false;
        ~FMediaQueueFixture() { Leases->Stop(); }
    };
    const auto Fixture = MakeShared<FMediaQueueFixture>();
    for (const TCHAR* Name : {TEXT("grass_day"), TEXT("snow"), TEXT("pond_movement"), TEXT("lake")})
    {
        Fixture->Paths.Add(FSoftObjectPath(FString::Printf(
            TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_%s.Play_Amb_%s"), Name, Name)));
    }
    Fixture->Assets = UAssetManager::GetStreamableManager().RequestAsyncLoad(Fixture->Paths);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Fixture]()
    {
        if (FPlatformTime::Seconds() > Fixture->DeadlineSec)
        {
            AddError(TEXT("Async priority test timed out"));
            return true;
        }
        if (!Fixture->Assets || !Fixture->Assets->HasLoadCompleted())
        {
            return false;
        }
        if (!Fixture->bQueued)
        {
            for (const FSoftObjectPath& Path : Fixture->Paths)
            {
                UAkAudioEvent* Event = Cast<UAkAudioEvent>(Path.ResolveObject());
                if (!TestNotNull(TEXT("Async Event asset"), Event)
                    || !TestFalse(TEXT("Test media is manually managed"), Event->bAutoLoad)
                    || !TestTrue(TEXT("Acquire media lease"), Fixture->Leases->Acquire(Event)))
                {
                    return true;
                }
                Fixture->Events.Add(Event);
            }
            Fixture->Leases->Advance(Fixture->Events[0].Get(), -100);
            Fixture->Leases->Advance(Fixture->Events[1].Get(), 60);
            Fixture->Leases->Advance(Fixture->Events[2].Get(), 200);
            Fixture->Leases->Advance(Fixture->Events[3].Get(), 250);
            Fixture->Leases->Release(Fixture->Events[3].Get());
            Fixture->bQueued = true;
            return false;
        }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto Status = Fixture->Leases->Advance(Fixture->Events[Index].Get());
            if (Status == EAudioEventResourceStatus::Failed)
            {
                AddError(TEXT("Native asynchronous media request failed"));
                return true;
            }
            if (Status != EAudioEventResourceStatus::Ready)
            {
                return false;
            }
        }
        const uint64 Low = FAudioPriorityTestAccess::RequestOrder(Fixture->Events[0].Get());
        const uint64 Medium = FAudioPriorityTestAccess::RequestOrder(Fixture->Events[1].Get());
        const uint64 High = FAudioPriorityTestAccess::RequestOrder(Fixture->Events[2].Get());
        TestTrue(TEXT("Priority is resolved before issuing Wwise IO"), High && High < Medium && Medium < Low);
        TestEqual(TEXT("A cancelled queued request never reaches Wwise"),
            FAudioPriorityTestAccess::RequestOrder(Fixture->Events[3].Get()), uint64(0));
        AddInfo(FString::Printf(TEXT("Native media request order high=%llu medium=%llu low=%llu | %s"),
            High, Medium, Low, *UAudioEventLeaseManager::GetLoadSummary()));
        return true;
    }));
    return true;
}
#endif
