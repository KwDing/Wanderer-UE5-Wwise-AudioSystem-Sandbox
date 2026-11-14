#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "Components/SplineComponent.h"
#include "InteriorFitArea.h"
#include "InteriorFitSpatialIndex.h"
#include "Misc/ScopeExit.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "UObject/UnrealType.h"

namespace
{
FVector BruteArea(const AInteriorFitArea& Area, const FVector& Listener)
{
    const FTransform Transform = Area.GetActorTransform();
    const FVector Local = Transform.InverseTransformPosition(Listener);
    const double Half = Area.CellSize * .5;
    double Best = TNumericLimits<double>::Max();
    FVector Result = Listener;
    for (const FVector& Cell : Area.GetSurfaceCells())
    {
        const FVector Point(FMath::Clamp(Local.X, Cell.X - Half, Cell.X + Half), FMath::Clamp(Local.Y, Cell.Y - Half, Cell.Y + Half),
            FMath::Clamp(Local.Z, Cell.Z - Area.DepthBelowSurface, Cell.Z + Area.HeightAboveSurface));
        const FVector WorldPoint = Transform.TransformPosition(Point);
        const double Distance = FVector::DistSquared(WorldPoint, Listener);
        if (Distance < Best) { Best = Distance; Result = WorldPoint; }
        if (Best < UE_SMALL_NUMBER) break;
    }
    return Result;
}

TArray<FVector> SampleSpline(const AWandererAudioSplineEmitter& Actor)
{
    TArray<FVector> Result;
    const USplineComponent* Spline = Actor.Spline;
    const bool bLake = Actor.Shape == EWandererAmbientSplineShape::Lake;
    const int32 Count = Spline->GetNumberOfSplinePoints();
    for (int32 Index = 0; Index < (bLake ? Count : Count - 1); ++Index)
    {
        const int32 Steps = Spline->GetSplinePointType(Index) == ESplinePointType::Linear ? 1 : 8;
        for (int32 Step = 0; Step < Steps; ++Step)
            Result.Add(Spline->GetLocationAtSplineInputKey(Index + float(Step) / Steps, ESplineCoordinateSpace::World));
    }
    if (!bLake) Result.Add(Spline->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::World));
    return Result;
}

FVector BruteSpline(const AWandererAudioSplineEmitter& Actor, const FVector& Listener, double& OutBlend)
{
    const TArray<FVector> Vertices = SampleSpline(Actor);
    const bool bLake = Actor.Shape == EWandererAmbientSplineShape::Lake;
    const double Lower = Vertices[0].Z - Actor.DepthBelowSurface * Actor.GetActorScale3D().Z;
    const double Upper = Vertices[0].Z + Actor.HeightAboveSurface * Actor.GetActorScale3D().Z;
    double Best = TNumericLimits<double>::Max();
    bool bInside = false;
    FVector Result = Listener;
    for (int32 Index = 0; Index < (bLake ? Vertices.Num() : Vertices.Num() - 1); ++Index)
    {
        FVector A = Vertices[Index];
        FVector B = Vertices[(Index + 1) % Vertices.Num()];
        if (bLake)
        {
            if ((A.Y > Listener.Y) != (B.Y > Listener.Y) && Listener.X < (B.X - A.X) * (Listener.Y - A.Y) / (B.Y - A.Y) + A.X) bInside = !bInside;
            A.Z = B.Z = FMath::Clamp(Listener.Z, Lower, Upper);
        }
        const FVector Point = FMath::ClosestPointOnSegment(Listener, A, B);
        const double Distance = FVector::DistSquared(Point, Listener);
        if (Distance < Best) { Best = Distance; Result = Point; }
    }
    OutBlend = 0;
    if (bLake && bInside)
    {
        Result = FVector(Listener.X, Listener.Y, FMath::Clamp(Listener.Z, Lower, Upper));
        OutBlend = FMath::Clamp(FMath::Min3(FMath::Sqrt(Best), Listener.Z - Lower, Upper - Listener.Z) / 200.0, 0.0, 1.0);
        OutBlend = FMath::SmoothStep(0.0f, 1.0f, static_cast<float>(OutBlend));
    }
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioAreaIndexTest, "Wanderer.Audio.GeometryIndex.Area", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioAreaIndexTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    auto* Area = World->SpawnActor<AInteriorFitArea>();
    if (!TestNotNull(TEXT("Temporary Area"), Area)) return false;
    ON_SCOPE_EXIT { Area->Destroy(); };
    TArray<FVector> Cells;
    for (int32 X = -40; X < 40; ++X) for (int32 Y = -40; Y < 40; ++Y)
        if (FMath::Abs(X) > 8 || FMath::Abs(Y) > 8) Cells.Add(FVector(X * 100, Y * 100, (X % 5) * 15));
    Area->SetSurfaceCells(Cells);
    FRandomStream Random(19517);
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        Area->SetActorTransform(FTransform(FRotator(Mode * 10, Mode * 37, Mode * 5), FVector(1000, -3000, 400), FVector(Mode == 2 ? -2 : 1, 1.7, .8)));
        for (int32 Query = 0; Query < 400; ++Query)
        {
            const FVector Listener = Area->GetActorTransform().TransformPosition(FVector(Random.FRandRange(-6500, 6500), Random.FRandRange(-6500, 6500), Random.FRandRange(-900, 900)));
            FVector Point;
            float Distance = 0;
            if (!Area->FindClosestPoint(Listener, Point, Distance) || !Point.Equals(BruteArea(*Area, Listener), .001) || !FMath::IsNearlyEqual(double(Distance), FVector::Distance(Listener, Point), .002))
            { AddError(TEXT("Area differs from original enumeration")); return false; }
            if (Area->GetBoundsDistanceM(Listener) > Distance / 100.0 + .001) { AddError(TEXT("Area bounds is not a conservative lower bound")); return false; }
        }
    }
    Area->SetActorTransform(FTransform::Identity);
    Area->SetSurfaceCells({FVector(-200, 0, 0), FVector(200, 0, 0)});
    FVector Point;
    float Distance = 0;
    Area->FindClosestPoint(FVector::ZeroVector, Point, Distance);
    TestTrue(TEXT("Equal distance retains first primitive"), Point.X < 0);
    FVector Centre;
    TestTrue(TEXT("Debug cell uses same nearest primitive/tie order"), Area->GetClosestSurfaceCell(FVector::ZeroVector, Centre) && Centre.Equals(FVector(-200, 0, 0)));
    Area->SetSurfaceCells({FVector(1200, 0, 0), FVector(2000, 0, 0)});
    Area->FindClosestPoint(FVector::ZeroVector, Point, Distance);
    TestTrue(TEXT("Same-count cell replacement invalidates"), Point.X > 1000);
    Area->CellSize = 4000;
    Area->FindClosestPoint(FVector::ZeroVector, Point, Distance);
    TestTrue(TEXT("Scalar geometry change invalidates"), Point.IsNearlyZero());
    FProperty* Property = FindFProperty<FProperty>(Area->GetClass(), TEXT("SurfaceCells"));
    *Property->ContainerPtrToValuePtr<TArray<FVector>>(Area) = { FVector(10000, 0, 0) };
    FPropertyChangedEvent Event(Property);
    static_cast<UObject*>(Area)->PostEditChangeProperty(Event);
    Area->FindClosestPoint(FVector::ZeroVector, Point, Distance);
    TestTrue(TEXT("Editor reflection write invalidates"), Point.X > 7000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSplineIndexTest, "Wanderer.Audio.GeometryIndex.Spline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSplineIndexTest::RunTest(const FString& Parameters)
{
    auto* Actor = GEditor->GetEditorWorldContext().World()->SpawnActor<AWandererAudioSplineEmitter>();
    if (!TestNotNull(TEXT("Temporary Spline"), Actor)) return false;
    ON_SCOPE_EXIT { Actor->Destroy(); };
    FRandomStream Random(2017);
    for (int32 Mode = 0; Mode < 4; ++Mode)
    {
        Actor->Shape = Mode == 3 ? EWandererAmbientSplineShape::Line : EWandererAmbientSplineShape::Lake;
        Actor->Spline->ClearSplinePoints(false);
        for (int32 Index = 0; Index < 128; ++Index)
        {
            const double Angle = Index * 2 * UE_PI / 128;
            const double Radius = 3000 + 400 * FMath::Sin(Angle * 5);
            Actor->Spline->AddSplinePoint(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), Mode == 3 ? Index * 20 : 0), ESplineCoordinateSpace::Local, false);
            Actor->Spline->SetSplinePointType(Index, Mode == 2 ? ESplinePointType::Curve : ESplinePointType::Linear, false);
        }
        Actor->Spline->SetClosedLoop(Mode != 3, true);
        Actor->SetActorTransform(FTransform(FRotator(Mode == 1 ? .2 : 0, Mode * 37, 0), FVector(2000, -7000, 600), FVector(1.3, .7, 1.8)));
        if (!TestTrue(TEXT("Valid fixture"), Actor->GetGeometryError().IsEmpty())) return false;
        for (int32 Query = 0; Query < 400; ++Query)
        {
            if (Query == 200) Actor->AddActorWorldOffset(FVector(600, 350, 500));
            const FVector Listener = Actor->GetActorLocation() + FVector(Random.FRandRange(-6000, 6000), Random.FRandRange(-6000, 6000), Random.FRandRange(-1400, 1000));
            FVector Point;
            float Distance = 0;
            float Blend = 0;
            double ExpectedBlend = 0;
            const FVector Expected = BruteSpline(*Actor, Listener, ExpectedBlend);
            if (!Actor->QueryListener(Listener, 2, Point, Distance, Blend) || !Point.Equals(Expected, .001) || !FMath::IsNearlyEqual(double(Blend), ExpectedBlend, .00001))
            { AddError(TEXT("Spline point or inside mix differs from original enumeration")); return false; }
            if (!FMath::IsNearlyEqual(double(Distance), FVector::Distance(Listener, Expected) / 100.0, .0001))
            { AddError(TEXT("Spline distance differs")); return false; }
        }
        Actor->Spline->SetLocationAtSplinePoint(0, Actor->Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local) + FVector(20, 0, 0), ESplineCoordinateSpace::Local);
        FVector Point;
        float Distance = 0;
        float Blend = 0;
        double ExpectedBlend = 0;
        const FVector Listener = Actor->GetActorLocation() + FVector(5000, 0, 0);
        const FVector Expected = BruteSpline(*Actor, Listener, ExpectedBlend);
        TestTrue(TEXT("Spline point edit refreshes index"), Actor->QueryListener(Listener, 2, Point, Distance, Blend) && Point.Equals(Expected, .001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSpatialPruningTest, "Wanderer.Audio.GeometryIndex.Pruning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSpatialPruningTest::RunTest(const FString& Parameters)
{
    TArray<FBox> Boxes;
    for (int32 Index = 0; Index < 8192; ++Index) Boxes.Emplace(FVector(Index * 100, 0, 0), FVector(Index * 100 + 10, 10, 10));
    FInteriorFitSpatialIndex Tree;
    Tree.Build(Boxes);
    int32 Visited = 0;
    double Best = TNumericLimits<double>::Max();
    Tree.FindNearest(FVector(5, 5, 5), FVector::OneVector, Best, [&](int32 ID, double& CurrentBest)
    { ++Visited; CurrentBest = FMath::Min(CurrentBest, Boxes[ID].ComputeSquaredDistanceToPoint(FVector(5, 5, 5))); });
    TestTrue(TEXT("8192 primitives query touches at most one small leaf"), Visited <= 8);
    TestEqual(TEXT("Exact zero inside primitive"), Best, 0.0);
    return true;
}
#endif
