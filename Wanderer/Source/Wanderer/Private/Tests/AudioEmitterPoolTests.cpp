#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "AkComponent.h"

#include "Interfaces/Audio/AudioPlaybackService.h"
#include "Systems/Audio/AudioEmitterPool.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioEmitterPoolRegionalRegistrationTest,
    "Wanderer.Audio.EmitterPool.RegionalRegistration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
        | EAutomationTestFlags::EngineFilter)

bool FAudioEmitterPoolRegionalRegistrationTest::RunTest(const FString& InParameters)
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
    if (!TestTrue(TEXT("Wwise available"), WandererAudioPlayback::IsReady(*World)))
    {
        return false;
    }

    const FName City(TEXT("City"));
    AActor* CitySource = World->SpawnActor<AActor>();
    AActor* RuralSource = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("City source"), CitySource)
        || !TestNotNull(TEXT("Rural source"), RuralSource))
    {
        return false;
    }
    Pool->Warmup(*World, 2);
    UAkComponent* CityEmitter = Pool->Acquire(*CitySource, false);
    UAkComponent* RuralEmitter = Pool->Acquire(*RuralSource, false);
    if (!TestNotNull(TEXT("City slot"), CityEmitter)
        || !TestNotNull(TEXT("Rural slot"), RuralEmitter))
    {
        return false;
    }

    const auto ReturnCityEmitter = [&]
    {
        Pool->BeginRelease(*CitySource, CityEmitter, nullptr, 0, 0, 0, City, false, false);
        Pool->RecycleReadyEmitters([](UAkAudioEvent*) {});
    };
    ReturnCityEmitter();
    TestTrue(TEXT("Ordinary budget release keeps a slot warm"),
        CityEmitter->IsRegisteredWithWwise());
    Pool->SuspendIdleGroup(City);
    TestFalse(TEXT("Region exit unregisters the parked Wwise object"),
        CityEmitter->IsRegisteredWithWwise());
    TestTrue(TEXT("The pooled UE component is retained"), CityEmitter->IsRegistered());
    TestTrue(TEXT("Another region's active Wwise object is retained"),
        RuralEmitter->IsRegisteredWithWwise());

    UAkComponent* Reused = Pool->Acquire(*CitySource, false);
    if (!TestTrue(TEXT("Returning to the region reuses and registers the same slot"),
            Reused == CityEmitter && CityEmitter->IsRegisteredWithWwise()))
    {
        return false;
    }
    ReturnCityEmitter();
    Pool->SuspendIdleGroup(City);
    TArray<UAkComponent*> Batch;
    return TestTrue(TEXT("A batch can also reopen a suspended slot"),
        Pool->AcquireBatch(*CitySource, 1, Batch) && Batch.Num() == 1
            && Batch[0] == CityEmitter && CityEmitter->IsRegisteredWithWwise());
}

#endif
