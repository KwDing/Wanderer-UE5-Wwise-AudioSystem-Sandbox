#include "Components/ItemPlacementComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Volume.h"

namespace PlacementSupportDefaults
{
    constexpr double FloorNormalThreshold = 0.7;
    constexpr double FootprintScale = 0.85;
    constexpr double ProbeHalfDepth = 5;
    constexpr double NormalAgreementMin = 0.9;
} // namespace PlacementSupportDefaults

void UItemPlacementComponent::ConfigureQuery(
    AActor* InPawn, AActor* InPreview, UClass* InExcludedClass)
{
    PawnWeakPtr = InPawn;
    PreviewWeakPtr = InPreview;
    ExcludedSurfaceClass = InExcludedClass;
}

bool UItemPlacementComponent::FindSurface(FVector InStart, FVector InEnd, FHitResult& OutHit) const
{
    if (!GetWorld())
    {
        return false;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SpeakerSurface), true);
    if (PawnWeakPtr.IsValid())
    {
        Query.AddIgnoredActor(PawnWeakPtr.Get());
    }
    if (PreviewWeakPtr.IsValid())
    {
        Query.AddIgnoredActor(PreviewWeakPtr.Get());
    }
    TArray<FHitResult> Hits;
    GetWorld()->LineTraceMultiByObjectType(Hits,
        InStart,
        InEnd,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
        Query);
    for (const FHitResult& Hit : Hits)
    {
        const UPrimitiveComponent* Component = Hit.GetComponent();
        if (!Component || !Hit.GetActor() || Hit.GetActor()->IsHidden() || !Component->IsVisible()
            || Component->bHiddenInGame || !Component->bRenderInMainPass
            || Component->IsA<UShapeComponent>() || Hit.GetActor()->IsA<AVolume>()
            || Hit.GetActor()->IsA<APawn>()
            || (ExcludedSurfaceClass && Hit.GetActor()->IsA(ExcludedSurfaceClass))
            || Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
        {
            continue;
        }
        OutHit = Hit;
        return true;
    }
    return false;
}

bool UItemPlacementComponent::IsBlocked(
    const FTransform& InTransform, FVector InExtent, AActor* InPreview) const
{
    if (!GetWorld())
    {
        return true;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ItemPlacementOverlap), false, InPreview);
    constexpr double Inset = 0.5;
    constexpr double ExtentMinCm = 0.1;
    const FVector InnerExtent = (InExtent - FVector(Inset)).ComponentMax(FVector(ExtentMinCm));
    const FCollisionShape Shape = FCollisionShape::MakeBox(InnerExtent);
    return GetWorld()->OverlapBlockingTestByChannel(
               InTransform.GetLocation(), InTransform.GetRotation(), ECC_Visibility, Shape, Query)
           || GetWorld()->OverlapBlockingTestByChannel(
               InTransform.GetLocation(), InTransform.GetRotation(), ECC_Pawn, Shape, Query);
}

bool UItemPlacementComponent::ValidateSupport(
    const FHitResult& InHit, const FTransform& InTransform, FVector InExtent) const
{
    const FVector Normal = InHit.ImpactNormal.GetSafeNormal();
    const bool bFloor = Normal.Z > PlacementSupportDefaults::FloorNormalThreshold;
    const FVector AxisA = InTransform.GetRotation().GetAxisY();
    const FVector AxisB =
        bFloor ? InTransform.GetRotation().GetAxisX() : InTransform.GetRotation().GetAxisZ();
    const double LengthA = InExtent.Y * PlacementSupportDefaults::FootprintScale;
    const double LengthB =
        (bFloor ? InExtent.X : InExtent.Z) * PlacementSupportDefaults::FootprintScale;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector Point = InHit.ImpactPoint + AxisA * LengthA * (Index & 1 ? 1 : -1)
                              + AxisB * LengthB * (Index & 2 ? 1 : -1);
        FHitResult Support;
        if (!FindSurface(Point + Normal * PlacementSupportDefaults::ProbeHalfDepth,
                Point - Normal * PlacementSupportDefaults::ProbeHalfDepth,
                Support)
            || FVector::DotProduct(Support.ImpactNormal, Normal)
                   < PlacementSupportDefaults::NormalAgreementMin)
        {
            return false;
        }
    }
    return true;
}
