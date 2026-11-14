#include "Systems/Audio/AudioSpaceQueryProvider.h"

#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Volume.h"

#include "Types/GameEventTypes.h"

namespace
{
    constexpr float CentimetersPerMeter = 100.0f;
    constexpr float QueryDistanceMaxM = 100.0f;

    bool IsAcousticBoundary(const UPrimitiveComponent* Component)
    {
        if (!IsValid(Component)
            || Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
        {
            return false;
        }
        const AActor* Owner = Component->GetOwner();
        static const FName IgnoreTag(TEXT("Acoustic:Ignore"));
        if (Component->ComponentHasTag(IgnoreTag) || (Owner && Owner->ActorHasTag(IgnoreTag)))
        {
            return false;
        }
        // Movement-only shapes/volumes are not reflecting geometry, even if debug-visible.
        if (Component->IsA<UShapeComponent>() || (Owner && Owner->IsA<AVolume>()))
        {
            return false;
        }
        // Use authored visibility, never camera visibility / recent rendering: off-screen
        // walls still matter acoustically. Hidden collision proxies are excluded by default.
        return Component->IsVisible() && !Component->bHiddenInGame && Component->bRenderInMainPass
               && (!Owner || !Owner->IsHidden());
    }

    FWandererSpaceDirection TraceBoundary(
        UWorld& World, const FVector& Origin, const FVector& End,
                                          const FCollisionQueryParams& Query)
    {
        FCollisionObjectQueryParams Objects;
        Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
        TArray<FHitResult> Hits;
        World.LineTraceMultiByObjectType(Hits, Origin, End, Objects, Query);
        FWandererSpaceDirection Result;
        float NearestDistance = TNumericLimits<float>::Max();
        for (const FHitResult& Hit : Hits)
        {
            const UPrimitiveComponent* Component = Hit.GetComponent();
            // Filter each object hit so excluded blockers do not hide real walls behind them.
            if (!IsAcousticBoundary(Component) || Hit.Distance >= NearestDistance)
            {
                continue;
            }
            NearestDistance = Hit.Distance;
            Result.bHit = true;
            Result.DistanceMeters = Hit.Distance / CentimetersPerMeter;
            Result.Position = Hit.ImpactPoint;
            Result.ActorName = GetNameSafe(Hit.GetActor());
        }
        return Result;
    }
} // namespace

bool FAudioSpaceQueryProvider::Capture(
    UWorld* InWorld, APawn* InPawn, float InQueryDistanceMaxM,
    TArray<FWandererSpaceDirection>& OutDirections)
{
    check(IsInGameThread());
    OutDirections.Reset();
    if (!IsValid(InPawn) || !InWorld || InWorld->bIsTearingDown || InPawn->GetWorld() != InWorld
        || !FMath::IsFinite(InQueryDistanceMaxM) || InQueryDistanceMaxM <= 0
        || InQueryDistanceMaxM > QueryDistanceMaxM)
    {
        return false;
    }
    const FVector Origin = InPawn->GetActorLocation();
    const FRotator Heading(0, InPawn->GetActorRotation().Yaw, 0);
    const FVector Forward = Heading.Vector();
    const FVector Right = FRotationMatrix(Heading).GetUnitAxis(EAxis::Y);
    const FVector Directions[] = {Forward, -Forward,          -Right,
                                  Right,   FVector::UpVector, -FVector::UpVector};
    const FName Names[] = {TEXT("Front"), TEXT("Back"), TEXT("Left"),
                           TEXT("Right"), TEXT("Up"),   TEXT("Down")};
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererSpace), true, InPawn);
    TArray<AActor*> Attached;
    InPawn->GetAttachedActors(Attached, true, true);
    Query.AddIgnoredActors(Attached);
    constexpr int32 DirectionCount = UE_ARRAY_COUNT(Directions);
    OutDirections.Reserve(DirectionCount);
    for (int32 Index = 0; Index < DirectionCount; ++Index)
    {
        FWandererSpaceDirection& Result = OutDirections.Add_GetRef(TraceBoundary(
            *InWorld, Origin,
            Origin + Directions[Index] * InQueryDistanceMaxM * CentimetersPerMeter, Query));
        Result.Direction = Names[Index];
    }
    return true;
}
