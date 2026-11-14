#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"
#include "Core/NaturalSurfaceUtils.h"
#include "Core/WaterContactUtils.h"
#include "Systems/WandererEnvironmentSubsystem.h"

struct FSurfaceInteractionTestAccess
{
    static void Start(UWandererWaterInteractionComponent& InComponent,
        UStaticMeshComponent& InSurface, UStaticMeshComponent& InWaterline,
        UMaterialInstanceDynamic& InMaterial)
    {
        // Exercise lifecycle without loading Demo assets or activating a vendor solver.
        InComponent.UActorComponent::BeginPlay();
        InComponent.bStopped = false;
        FWandererIWSReceiver& Receiver = InComponent.IWSReceivers.AddDefaulted_GetRef();
        Receiver.SurfaceWeakPtr = &InSurface;
        Receiver.SurfaceTemplate = InSurface.GetMaterial(0);
        Receiver.SurfaceMaterial = &InMaterial;
        Receiver.WaterlineWeakPtr = &InWaterline;
        InSurface.SetMaterial(0, &InMaterial);
    }

    static void EnterUpdate(UWandererWaterInteractionComponent& InComponent)
    {
        InComponent.bUpdating = true;
    }

    static void Stop(UWandererWaterInteractionComponent& InComponent)
    {
        InComponent.EndPlay(EEndPlayReason::Destroyed);
    }

    static void Finish(UWandererWaterInteractionComponent& InComponent)
    {
        InComponent.FinishUpdate();
    }

    static int32 GetReceiverCount(const UWandererWaterInteractionComponent& InComponent)
    {
        return InComponent.IWSReceivers.Num();
    }

    static bool CanUpdate(const UWandererWaterInteractionComponent& InComponent)
    {
        return InComponent.CanUpdate();
    }
};

namespace
{
    struct FSurfaceTestWorld
    {
        UWorld* World = nullptr;

        FSurfaceTestWorld()
        {
            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                .AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                ERHIFeatureLevel::Num, &Values);
        }

        ~FSurfaceTestWorld()
        {
            if (World)
            {
                World->DestroyWorld(false);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentTerminalStateTest,
    "Wanderer.Environment.DayNightLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentTerminalStateTest::RunTest(const FString& InParameters)
{
    FSurfaceTestWorld Fixture;
    if (!TestNotNull(TEXT("Test world"), Fixture.World))
    {
        return false;
    }
    UWandererEnvironmentSubsystem* State =
        Fixture.World->GetSubsystem<UWandererEnvironmentSubsystem>();
    if (!TestNotNull(TEXT("World state"), State))
    {
        return false;
    }
    State->SetNight(true);
    TestTrue(TEXT("Night is retained"), State->IsNight());
    State->SetNight(true);
    TestTrue(TEXT("Repeated request is idempotent"), State->IsNight());
    State->Deinitialize();
    State->SetNight(true);
    TestFalse(TEXT("Late writes cannot reactivate stopped state"), State->IsNight());
    State->Deinitialize();
    TestFalse(TEXT("Editor worlds excluded"), State->DoesSupportWorldType(EWorldType::Editor));
    TestFalse(TEXT("Preview worlds excluded"),
        State->DoesSupportWorldType(EWorldType::EditorPreview));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaterDeferredCleanupTest,
    "Wanderer.Environment.WaterDeferredCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterDeferredCleanupTest::RunTest(const FString& InParameters)
{
    FSurfaceTestWorld Fixture;
    ACharacter* Owner = Fixture.World ? Fixture.World->SpawnActor<ACharacter>() : nullptr;
    if (!TestNotNull(TEXT("Character"), Owner))
    {
        return false;
    }
    UWandererWaterInteractionComponent* Water = NewObject<UWandererWaterInteractionComponent>(Owner);
    Water->RegisterComponent();
    UStaticMeshComponent* Surface = NewObject<UStaticMeshComponent>(Owner);
    UStaticMeshComponent* Waterline = NewObject<UStaticMeshComponent>(Owner);
    Waterline->RegisterComponent();
    UMaterialInterface* Original = UMaterial::GetDefaultMaterial(MD_Surface);
    Surface->SetMaterial(0, Original);
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Original, Surface);
    FSurfaceInteractionTestAccess::Start(*Water, *Surface, *Waterline, *Material);
    FSurfaceInteractionTestAccess::EnterUpdate(*Water);
    Water->ClearWaterRegistration();
    TestFalse(TEXT("Clear immediately cancels further work"),
        FSurfaceInteractionTestAccess::CanUpdate(*Water));
    TestEqual(TEXT("Borrowed receiver remains valid on active stack"),
        FSurfaceInteractionTestAccess::GetReceiverCount(*Water), 1);
    FSurfaceInteractionTestAccess::Stop(*Water);
    TestEqual(TEXT("EndPlay also defers array destruction"),
        FSurfaceInteractionTestAccess::GetReceiverCount(*Water), 1);
    FSurfaceInteractionTestAccess::Finish(*Water);
    TestEqual(TEXT("Unwind releases receiver state"),
        FSurfaceInteractionTestAccess::GetReceiverCount(*Water), 0);
    TestEqual(TEXT("Owned material restores original"), Surface->GetMaterial(0), Original);
    TestFalse(TEXT("Owned waterline unregistered"), Waterline->IsRegistered());
    Water->ClearWaterRegistration();
    TestFalse(TEXT("Repeated clear cannot reactivate"),
        FSurfaceInteractionTestAccess::CanUpdate(*Water));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWaterBodyGeometryTest,
    "Wanderer.Environment.WaterBodyGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWaterBodyGeometryTest::RunTest(const FString& InParameters)
{
    TArray<FWandererWaterBodySection> Sections;
    const FVector Point(2.0, 0.0, 0.0);
    TestEqual(TEXT("Empty contour preserves point"),
        UE::Wanderer::Water::ProjectToBodyEdge(Point, Sections), Point);
    TestFalse(TEXT("Empty contour has invalid bounds"),
        !!UE::Wanderer::Water::GetSectionBounds(Sections).IsValid);
    Sections.Add({FVector::ZeroVector, FVector::ZeroVector, 5.0f});
    TestTrue(TEXT("Sphere projects onto horizontal edge"),
        UE::Wanderer::Water::ProjectToBodyEdge(Point, Sections).Equals(FVector(5, 0, 0), 0.001));
    const FBox Bounds = UE::Wanderer::Water::GetSectionBounds(Sections);
    TestTrue(TEXT("Bounds contain full radius"), Bounds.Min.Equals(FVector(-5))
        && Bounds.Max.Equals(FVector(5)));
    TestEqual(TEXT("Demo lake center unchanged"),
        UE::Wanderer::Contours::LakeRadius(FVector(950, -8950, 0)), 0.0);
    TestEqual(TEXT("Dry elevated ground unchanged"),
        UE::Wanderer::Contours::LakeGroundMoisture(FVector(950, -8950, 0)), 0.0);
    TestEqual(TEXT("Wet submerged lake center unchanged"),
        UE::Wanderer::Contours::LakeGroundMoisture(FVector(950, -8950, -30)), 1.0);
    TestEqual(TEXT("Snow center unchanged"),
        UE::Wanderer::Contours::SnowRadius(FVector(-1250, -6300, 0)), 0.0);
    return true;
}
#endif
