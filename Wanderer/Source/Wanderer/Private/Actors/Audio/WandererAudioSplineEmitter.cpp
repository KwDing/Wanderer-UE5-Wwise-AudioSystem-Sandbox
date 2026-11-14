#include "Actors/Audio/WandererAudioSplineEmitter.h"

#include "Components/SplineComponent.h"

#include "Components/WorldAudio/WandererAudioSourceComponent.h"

namespace
{
    constexpr double CentimetersPerMeter = 100.0;
    constexpr float BlendCentimetersPerMeter = 100.0f;
    constexpr double LakeUpAlignmentMin = 0.9999;
    constexpr double InitialHalfExtentCm = 300.0;
    constexpr int32 InitialPointCount = 4;
    constexpr int32 LakePointCountMin = 3;
    constexpr int32 LinePointCountMin = 2;
    constexpr int32 SplinePointCountMax = 256;
    constexpr int32 CurvedSegmentSamples = 8;
    constexpr double LakeHeightDifferenceMaxCm = 1.0;
    constexpr double EdgeLengthSquaredMinSquareCm = 0.01;
    constexpr double TwiceAreaMinSquareCm = 1.0;
} // namespace

AWandererAudioSplineEmitter::AWandererAudioSplineEmitter()
{
    PrimaryActorTick.bCanEverTick = false;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("SoundSpline"));
    SetRootComponent(Spline);
    Spline->bInputSplinePointsToConstructionScript = true;
    Spline->ClearSplinePoints(false);
    for (const FVector& Point : {FVector(-InitialHalfExtentCm, -InitialHalfExtentCm, 0),
                                 FVector(InitialHalfExtentCm, -InitialHalfExtentCm, 0),
                                 FVector(InitialHalfExtentCm, InitialHalfExtentCm, 0),
                                 FVector(-InitialHalfExtentCm, InitialHalfExtentCm, 0)})
    {
        Spline->AddSplinePoint(Point, ESplineCoordinateSpace::Local, false);
    }
    for (int32 Index = 0; Index < InitialPointCount; ++Index)
    {
        Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
    }
    Spline->SetClosedLoop(true);
    AudioSource = CreateDefaultSubobject<UWandererAudioSourceComponent>(TEXT("AudioSource"));
}

FString AWandererAudioSplineEmitter::GetGeometryError() const
{
    UpdateGeometry();
    if (!GeometryError.IsEmpty())
    {
        return GeometryError;
    }
    if (GetActorTransform().ContainsNaN() || GetActorScale3D().GetMin() <= UE_SMALL_NUMBER)
    {
        return TEXT("Spline scale must be finite and positive");
    }
    if (!FMath::IsFinite(HeightAboveSurface) || !FMath::IsFinite(DepthBelowSurface)
        || HeightAboveSurface <= 0 || DepthBelowSurface <= 0)
    {
        return TEXT("Lake height/depth must be finite and positive");
    }
    if (Shape == EWandererAmbientSplineShape::Lake && GetActorUpVector().Z < LakeUpAlignmentMin)
    {
        return TEXT("Lake allows yaw rotation only; use Line for tilted paths");
    }
    return FString();
}

bool AWandererAudioSplineEmitter::QueryListener(FVector Listener,
                                                float BlendDistanceM,
                                                FVector& OutPosition,
                                                float& OutDistanceM,
                                                float& OutInsideBlend) const
{
    OutPosition = Listener;
    OutDistanceM = 0;
    OutInsideBlend = 0;
    if (!GetGeometryError().IsEmpty() || Listener.ContainsNaN() || !FMath::IsFinite(BlendDistanceM)
        || BlendDistanceM <= 0)
    {
        return false;
    }

    UpdateQueryIndex();
    const bool bLake = Shape == EWandererAmbientSplineShape::Lake;
    const double Surface = GetActorTransform().TransformPosition(Vertices[0]).Z;
    const FVector2D HeightRange(Surface - DepthBelowSurface * GetActorScale3D().Z,
                                Surface + HeightAboveSurface * GetActorScale3D().Z);
    const bool bInside = bLake && ContainsLakePoint(Listener);
    FVector SearchPoint = Listener;
    double BestDistanceSquared = TNumericLimits<double>::Max();
    if (bLake)
    {
        SearchPoint.Z = 0;
    }
    if (bInside)
    {
        // Farther edges cannot change the clamped inside mix or the listener projection.
        const double NeededDistance = FMath::Max(0.0,
                                                 FMath::Min3(BlendDistanceM * CentimetersPerMeter,
                                                             Listener.Z - HeightRange.X,
                                                             HeightRange.Y - Listener.Z));
        BestDistanceSquared = FMath::Square(NeededDistance);
    }

    FindClosestSegment(Listener, SearchPoint, HeightRange, BestDistanceSquared, OutPosition);
    if (bInside)
    {
        OutPosition =
            FVector(Listener.X, Listener.Y, FMath::Clamp(Listener.Z, HeightRange.X, HeightRange.Y));
        OutInsideBlend =
            CalcLakeInsideBlend(Listener, HeightRange, BestDistanceSquared, BlendDistanceM);
    }
    OutDistanceM = FVector::Distance(Listener, OutPosition) / CentimetersPerMeter;
    return true;
}

float AWandererAudioSplineEmitter::GetBoundsDistanceM(const FVector& Listener) const
{
    UpdateGeometry();
    FBox Bounds = LocalBounds;
    if (Shape == EWandererAmbientSplineShape::Lake)
    {
        Bounds.Min.Z -= DepthBelowSurface;
        Bounds.Max.Z += HeightAboveSurface;
    }
    return FMath::Sqrt(
               Bounds.TransformBy(GetActorTransform()).ComputeSquaredDistanceToPoint(Listener))
           / CentimetersPerMeter;
}

void AWandererAudioSplineEmitter::RecenterSpline()
{
    if (HasActorBegunPlay())
    {
        return;
    }
    UpdateGeometry();
    if (!LocalBounds.IsValid)
    {
        return;
    }
    const FVector Offset = LocalBounds.GetCenter();
    const FVector NewOrigin = GetActorTransform().TransformPosition(Offset);
    Modify();
    Spline->Modify();
    for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
    {
        Spline->SetLocationAtSplinePoint(
            Index,
            Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local) - Offset,
            ESplineCoordinateSpace::Local,
            false);
    }
    Spline->UpdateSpline();
    SetActorLocation(NewOrigin);
    bCacheReady = false;
}

void AWandererAudioSplineEmitter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Spline->SetClosedLoop(Shape == EWandererAmbientSplineShape::Lake);
    bCacheReady = false;
}

bool AWandererAudioSplineEmitter::ContainsLakePoint(const FVector& InListener) const
{
    FVector SearchPoint = InListener;
    SearchPoint.Z = 0;
    bool bInside = false;
    SegmentIndex.VisitCrossingEdges(
        SearchPoint,
        [&](int32 InIndex)
        {
            const FVector& A = WorldVertices[InIndex];
            const FVector& B = WorldVertices[(InIndex + 1) % WorldVertices.Num()];
            if ((A.Y > InListener.Y) != (B.Y > InListener.Y)
                && InListener.X < (B.X - A.X) * (InListener.Y - A.Y) / (B.Y - A.Y) + A.X)
            {
                bInside = !bInside;
            }
        });
    return bInside;
}

void AWandererAudioSplineEmitter::FindClosestSegment(const FVector& InListener,
                                                     const FVector& InSearchPoint,
                                                     const FVector2D& InHeightRange,
                                                     double& InOutBestDistanceSquared,
                                                     FVector& OutPosition) const
{
    const bool bLake = Shape == EWandererAmbientSplineShape::Lake;
    int32 BestID = MAX_int32;
    SegmentIndex.FindNearest(
        InSearchPoint,
        FVector::OneVector,
        InOutBestDistanceSquared,
        [&](int32 InIndex, double& InOutCurrentBest)
        {
            FVector A = WorldVertices[InIndex];
            FVector B = WorldVertices[(InIndex + 1) % WorldVertices.Num()];
            if (bLake)
            {
                A.Z = FMath::Clamp(InListener.Z, InHeightRange.X, InHeightRange.Y);
                B.Z = A.Z;
            }
            const FVector Closest = FMath::ClosestPointOnSegment(InListener, A, B);
            const double Squared = FVector::DistSquared(InListener, Closest);
            if (Squared < InOutCurrentBest || (Squared == InOutCurrentBest && InIndex < BestID))
            {
                InOutCurrentBest = Squared;
                BestID = InIndex;
                OutPosition = Closest;
            }
        });
}

float AWandererAudioSplineEmitter::CalcLakeInsideBlend(const FVector& InListener,
                                                       const FVector2D& InHeightRange,
                                                       double InBestDistanceSquared,
                                                       float InBlendDistanceM) const
{
    const double InnerDistance = FMath::Min3(FMath::Sqrt(InBestDistanceSquared),
                                             InListener.Z - InHeightRange.X,
                                             InHeightRange.Y - InListener.Z);
    const float Blend =
        FMath::Clamp(InnerDistance / (InBlendDistanceM * BlendCentimetersPerMeter), 0.0, 1.0);
    return FMath::SmoothStep(0.0f, 1.0f, Blend);
}

void AWandererAudioSplineEmitter::UpdateGeometry() const
{
    if (bCacheReady && CachedVersion == Spline->SplineCurves.Version && CachedShape == Shape)
    {
        return;
    }
    bCacheReady = true;
    bQueryIndexReady = false;
    CachedVersion = Spline->SplineCurves.Version;
    CachedShape = Shape;
    Vertices.Reset();
    LocalBounds = FBox(ForceInit);
    GeometryError.Reset();
    const int32 Count = Spline->GetNumberOfSplinePoints();
    const bool bLake = Shape == EWandererAmbientSplineShape::Lake;
    if (Count < (bLake ? LakePointCountMin : LinePointCountMin) || Count > SplinePointCountMax)
    {
        GeometryError = TEXT("Spline needs 3–256 Lake points or 2–256 Line points");
        return;
    }
    const int32 Segments = bLake ? Count : Count - 1;
    for (int32 Index = 0; Index < Segments; ++Index)
    {
        if (!SampleSplineSegment(Index))
        {
            return;
        }
    }
    if (bLake)
    {
        ValidateLake();
        return;
    }

    const FVector End =
        Spline->GetLocationAtSplinePoint(Count - 1, ESplineCoordinateSpace::Local);
    if (End.ContainsNaN())
    {
        GeometryError = TEXT("Non-finite Line endpoint");
        return;
    }
    Vertices.Add(End);
    LocalBounds += End;
}

bool AWandererAudioSplineEmitter::SampleSplineSegment(int32 InSegmentIndex) const
{
    const ESplinePointType::Type Type = Spline->GetSplinePointType(InSegmentIndex);
    if (Type == ESplinePointType::Constant)
    {
        GeometryError = TEXT("Constant spline segments are unsupported; use Linear or Curve");
        return false;
    }

    const int32 Steps = Type == ESplinePointType::Linear ? 1 : CurvedSegmentSamples;
    for (int32 Step = 0; Step < Steps; ++Step)
    {
        const FVector Point = Spline->GetLocationAtSplineInputKey(
            InSegmentIndex + static_cast<float>(Step) / Steps, ESplineCoordinateSpace::Local);
        if (Point.ContainsNaN())
        {
            GeometryError = TEXT("Non-finite spline point");
            return false;
        }
        Vertices.Add(Point);
        LocalBounds += Point;
    }
    return true;
}

void AWandererAudioSplineEmitter::UpdateQueryIndex() const
{
    const FTransform Transform = GetActorTransform();
    if (bQueryIndexReady && CachedQueryTransform.Equals(Transform, 0.0))
    {
        return;
    }
    CachedQueryTransform = Transform;
    bQueryIndexReady = true;
    WorldVertices.Reset(Vertices.Num());
    for (const FVector& Point : Vertices)
    {
        WorldVertices.Add(Transform.TransformPosition(Point));
    }
    const bool bLake = Shape == EWandererAmbientSplineShape::Lake;
    TArray<FBox> Boxes;
    const int32 EdgeCount = bLake ? Vertices.Num() : Vertices.Num() - 1;
    Boxes.Reserve(EdgeCount);
    for (int32 Index = 0; Index < EdgeCount; ++Index)
    {
        FVector A = WorldVertices[Index];
        FVector B = WorldVertices[(Index + 1) % WorldVertices.Num()];
        if (bLake)
        {
            A.Z = B.Z = 0;
        }
        FBox Box(ForceInit);
        Box += A;
        Box += B;
        Boxes.Add(Box);
    }
    SegmentIndex.Build(Boxes);
}

void AWandererAudioSplineEmitter::ValidateLake() const
{
    if (LocalBounds.GetSize().Z > LakeHeightDifferenceMaxCm)
    {
        GeometryError = TEXT("Lake points must share a horizontal height; use Line for waterfalls");
        return;
    }
    double TwiceArea = 0;
    for (int32 Index = 0; Index < Vertices.Num(); ++Index)
    {
        const FVector2D A(Vertices[Index]);
        const FVector2D B(Vertices[(Index + 1) % Vertices.Num()]);
        if (FVector2D::DistSquared(A, B) < EdgeLengthSquaredMinSquareCm)
        {
            GeometryError = TEXT("Duplicate adjacent spline points");
            return;
        }
        TwiceArea += A.X * B.Y - B.X * A.Y;
        if (HasLakeEdgeIntersection(Index, A, B))
        {
            GeometryError = TEXT("Lake spline crosses itself");
            return;
        }
    }
    if (FMath::Abs(TwiceArea) < TwiceAreaMinSquareCm)
    {
        GeometryError = TEXT("Lake spline has no enclosed area");
    }
}

bool AWandererAudioSplineEmitter::HasLakeEdgeIntersection(int32 InEdgeIndex,
                                                         const FVector2D& InStart,
                                                         const FVector2D& InEnd) const
{
    const FVector2D Edge = InEnd - InStart;
    for (int32 Other = InEdgeIndex + 2; Other < Vertices.Num(); ++Other)
    {
        // The first and last edges share the closing vertex.
        if (InEdgeIndex == 0 && Other == Vertices.Num() - 1)
        {
            continue;
        }
        const FVector2D OtherStart(Vertices[Other]);
        const FVector2D OtherEnd(Vertices[(Other + 1) % Vertices.Num()]);
        const FVector2D OtherEdge = OtherEnd - OtherStart;
        const double Denominator = FVector2D::CrossProduct(Edge, OtherEdge);
        if (FMath::Abs(Denominator) < UE_SMALL_NUMBER)
        {
            continue;
        }
        const FVector2D StartOffset = OtherStart - InStart;
        const double Along = FVector2D::CrossProduct(StartOffset, OtherEdge) / Denominator;
        const double Across = FVector2D::CrossProduct(StartOffset, Edge) / Denominator;
        if (Along >= 0 && Along <= 1 && Across >= 0 && Across <= 1)
        {
            return true;
        }
    }
    return false;
}
