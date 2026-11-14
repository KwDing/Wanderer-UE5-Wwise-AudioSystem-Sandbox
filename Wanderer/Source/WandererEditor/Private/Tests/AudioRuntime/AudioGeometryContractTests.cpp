#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Interfaces/Audio/AudioGeometryProvider.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioGeometryContractTest,
    "Wanderer.Audio.GeometryIndex.HostContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioGeometryContractTest::RunTest(const FString& InParameters)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    AWandererAudioAreaEmitter* Area = World->SpawnActor<AWandererAudioAreaEmitter>();
    AWandererAudioSplineEmitter* Spline = World->SpawnActor<AWandererAudioSplineEmitter>();
    ON_SCOPE_EXIT
    {
        if (Area)
        {
            Area->Destroy();
        }
        if (Spline)
        {
            Spline->Destroy();
        }
    };
    if (!TestNotNull(TEXT("Area host"), Area) || !TestNotNull(TEXT("Spline host"), Spline))
    {
        return false;
    }
    Area->SetSurfaceCells({FVector::ZeroVector, FVector(100, 0, 0)});
    const IAudioGeometryProvider* AreaContract = Cast<IAudioGeometryProvider>(Area);
    const IAudioGeometryProvider* SplineContract = Cast<IAudioGeometryProvider>(Spline);
    TestTrue(TEXT("Area kind retained"), AreaContract->GetAudioGeometryKind() == EAudioGeometryKind::Area);
    TestTrue(TEXT("Spline kind retained"), SplineContract->GetAudioGeometryKind() == EAudioGeometryKind::Spline);
    const FVector Listener(450, 320, 50);
    FVector Position;
    float Distance = 0.0f;
    float Blend = 0.0f;
    FAudioGeometrySample Sample;
    TestTrue(TEXT("Area query via interface"), AreaContract->QueryAudioGeometry(Listener, 2.0f, Sample));
    Area->FindClosestPoint(Listener, Position, Distance);
    TestTrue(TEXT("Area position preserved"), Sample.Position.Equals(Position));
    TestTrue(TEXT("Area distance converted once"), FMath::IsNearlyEqual(Sample.DistanceM, Distance / 100.0f));
    TestTrue(TEXT("Spline query via interface"), SplineContract->QueryAudioGeometry(Listener, 2.0f, Sample));
    Spline->QueryListener(Listener, 2.0f, Position, Distance, Blend);
    TestTrue(TEXT("Spline position preserved"), Sample.Position.Equals(Position));
    TestEqual(TEXT("Spline meters preserved"), Sample.DistanceM, Distance);
    TestEqual(TEXT("Spline blend preserved"), Sample.InsideBlend, Blend);
    return true;
}
#endif
