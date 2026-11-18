#include "Framework/Mapping/WandererMapComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "Data/MapScanData.h"

DEFINE_LOG_CATEGORY_STATIC(LogWandererMapScan, Log, All);

UWandererMapComponent::UWandererMapComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UWandererMapComponent::BeginPlay()
{
    Super::BeginPlay();
    FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UWandererMapComponent::OnScanLevelChanged);
    FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UWandererMapComponent::OnScanLevelChanged);
}

void UWandererMapComponent::Rescan()
{
    ScanCharacterWeakPtr.Reset();
    Snapshot.CellSize = CellSize;
    Snapshot.Seed = FVector::ZeroVector;
    Snapshot.Cells.Reset();
    Snapshot.CellLookup.Reset();
    Snapshot.Entrances.Reset();
    Snapshot.MapBounds = FBox2D(ForceInit);
    NextCell = 0;
    Phase = EScanPhase::Waiting;
    bLevelSetChanged = false;
    NextLevel = 0;
    NextActor = 0;
    NextComponent = 0;
    BoundsActorWeakPtr.Reset();
    bCollectTrialBounds = false;
    DoorCandidates.Reset();
    NextDoor = 0;
    EntranceBuckets.Reset();
    SlowestStepSec = 0;
    SlowestPhase = EScanPhase::Waiting;
    OverBudgetFrames = 0;
    Snapshot.bScanning = false;
    Snapshot.bTruncated = false;
    Snapshot.Regions.Reset();
    SurveyWater.Reset();
    SurveyQuery = FCollisionQueryParams();
    SurveyBounds = FBox(ForceInit);
    VisibleBounds = FBox(ForceInit);
    SurveyDimensions = FIntPoint::ZeroValue;
    Snapshot.bSurveying = false;
    NextSurveyCell = 0;
}

bool UWandererMapComponent::IsScanning() const
{
    return Snapshot.bScanning || Snapshot.bSurveying;
}

bool UWandererMapComponent::HasCell(const FIntPoint& Key) const
{
    return Snapshot.CellLookup.Contains(Key);
}

FIntPoint UWandererMapComponent::GetCellKey(const FVector& InPosition) const
{
    return FIntPoint(FMath::RoundToInt((InPosition.X - Snapshot.Seed.X) / CellSize),
        FMath::RoundToInt((InPosition.Y - Snapshot.Seed.Y) / CellSize));
}

void UWandererMapComponent::TickComponent(
    float InDeltaTime, ELevelTick InTickType, FActorComponentTickFunction* InThisTickFunction)
{
    Super::TickComponent(InDeltaTime, InTickType, InThisTickFunction);
    const double Deadline = FPlatformTime::Seconds() + UE::Wanderer::MapScan::FrameBudgetSeconds;
    const APlayerController* Controller = Cast<APlayerController>(GetOwner());
    ACharacter* Character = Controller && Controller->IsLocalController()
        ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (bLevelSetChanged || ((Phase != EScanPhase::Waiting || ScanCharacterWeakPtr.IsValid())
            && (!IsValid(Character) || ScanCharacterWeakPtr.Get() != Character)))
    {
        Rescan();
    }
    const UWorld* World = GetWorld();
    if (!IsValid(Character) || !World || World->bIsTearingDown)
    {
        return;
    }
    if (Phase == EScanPhase::Waiting)
    {
        StartScan(*Character);
    }
    AdvanceScan(Deadline);
}

void UWandererMapComponent::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
    FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
    FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
    SetComponentTickEnabled(false);
    Rescan();
    Super::EndPlay(InEndPlayReason);
}

void UWandererMapComponent::OnScanLevelChanged(ULevel*, UWorld* InWorld)
{
    if (InWorld && InWorld == GetWorld())
    {
        bLevelSetChanged = true;
    }
}

void UWandererMapComponent::StartScan(ACharacter& InCharacter)
{
    if (!InCharacter.GetCharacterMovement()->IsMovingOnGround())
    {
        return;
    }
    ScanCharacterWeakPtr = &InCharacter;
    Snapshot.CellSize = CellSize;
    Radius = InCharacter.GetCapsuleComponent()->GetScaledCapsuleRadius();
    HalfHeight = InCharacter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    StepHeight = InCharacter.GetCharacterMovement()->MaxStepHeight;
    WalkableFloorZ = InCharacter.GetCharacterMovement()->GetWalkableFloorZ();
    Snapshot.bScanning = true;
    Phase = EScanPhase::Seed;
}

void UWandererMapComponent::AdvanceScan(double InDeadline)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(WandererMapAdvanceScan);
    double Now = FPlatformTime::Seconds();
    const EScanPhase InitialPhase = Phase;
    while (IsScanning() && !bLevelSetChanged && Now < InDeadline)
    {
        const EScanPhase StepPhase = Phase;
        const double StepStart = Now;
        AdvanceScanStep();
        Now = FPlatformTime::Seconds();
        if (Now - StepStart > SlowestStepSec)
        {
            SlowestStepSec = Now - StepStart;
            SlowestPhase = StepPhase;
        }
        if (Now > InDeadline)
        {
            ++OverBudgetFrames;
        }
    }
    if (InitialPhase != EScanPhase::Complete && Phase == EScanPhase::Complete)
    {
        UE_LOG(LogWandererMapScan, Verbose,
            TEXT("Scan completed: %d frames over budget, slowest step %.3f ms (phase %d)."),
            OverBudgetFrames, SlowestStepSec * 1000.0, static_cast<int32>(SlowestPhase));
    }
}

void UWandererMapComponent::AdvanceScanStep()
{
    switch (Phase)
    {
    case EScanPhase::Seed:
        TrySeedScan();
        break;
    case EScanPhase::Bounds:
        AdvanceSurveyBounds();
        break;
    case EScanPhase::Survey:
        SampleSurveyCell();
        if (!Snapshot.bSurveying)
        {
            Phase = EScanPhase::Reachable;
        }
        break;
    case EScanPhase::Reachable:
        if (NextCell < Snapshot.Cells.Num() && Snapshot.Cells.Num() < MaximumCells)
        {
            ExpandCell(NextCell++);
        }
        else
        {
            Snapshot.bTruncated |= Snapshot.Cells.Num() >= MaximumCells;
            Phase = EScanPhase::Entrances;
        }
        break;
    case EScanPhase::Entrances:
        AdvanceEntranceScan();
        break;
    default:
        break;
    }
}

void UWandererMapComponent::TrySeedScan()
{
    const ACharacter* Character = ScanCharacterWeakPtr.Get();
    FVector Floor;
    if (Character && Character->GetCharacterMovement()->IsMovingOnGround())
    {
        Snapshot.Seed = Character->GetActorLocation() - FVector(0, 0, HalfHeight);
        if (FindFloor(Snapshot.Seed, Floor))
        {
            AddCell(Floor);
            BeginSurveyBounds();
            Phase = EScanPhase::Bounds;
            return;
        }
    }
    Snapshot.bScanning = false;
    Phase = EScanPhase::Waiting;
}

bool UWandererMapComponent::FindFloor(const FVector& InPosition, FVector& OutFloor) const
{
    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(WandererMapFloor), false, ScanCharacterWeakPtr.Get());
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit,
            InPosition + FVector(0, 0, StepHeight + UE::Wanderer::MapScan::CapsuleClearance),
            InPosition - FVector(0, 0, StepHeight + UE::Wanderer::MapScan::CapsuleClearance),
            ECC_Pawn,
            Params))
    {
        return false;
    }
    if (Hit.ImpactNormal.Z < WalkableFloorZ
        || FMath::Abs(Hit.ImpactPoint.Z - InPosition.Z) > StepHeight)
    {
        return false;
    }
    OutFloor = Hit.ImpactPoint;
    const FVector Center =
        OutFloor + FVector(0, 0, HalfHeight + UE::Wanderer::MapScan::CapsuleClearance);
    return !GetWorld()->OverlapBlockingTestByChannel(Center,
        FQuat::Identity,
        ECC_Pawn,
        FCollisionShape::MakeCapsule(Radius, HalfHeight),
        Params);
}

bool UWandererMapComponent::CanTraverse(const FVector& InFrom, const FVector& InTo) const
{
    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(WandererMapPassage), false, ScanCharacterWeakPtr.Get());
    const double Height =
        FMath::Max(InFrom.Z, InTo.Z) + HalfHeight + UE::Wanderer::MapScan::CapsuleClearance;
    FHitResult Hit;
    return !GetWorld()->SweepSingleByChannel(Hit,
        FVector(InFrom.X, InFrom.Y, Height),
        FVector(InTo.X, InTo.Y, Height),
        FQuat::Identity,
        ECC_Pawn,
        FCollisionShape::MakeCapsule(Radius, HalfHeight),
        Params);
}

void UWandererMapComponent::ExpandCell(int32 Index)
{
    const FVector From = Snapshot.Cells[Index];
    const FVector Directions[] = {
        FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0)};
    for (const FVector& Direction : Directions)
    {
        const FVector Target = From + Direction * CellSize;
        if (Snapshot.CellLookup.Contains(GetCellKey(Target)))
        {
            continue;
        }
        if (FVector::DistSquared2D(Target, Snapshot.Seed) > FMath::Square(ScanRadius))
        {
            Snapshot.bTruncated = true;
            continue;
        }
        FVector Floor;
        if (FindFloor(Target, Floor) && CanTraverse(From, Floor))
        {
            AddCell(Floor);
        }
    }
}

void UWandererMapComponent::AddCell(const FVector& InFloor)
{
    Snapshot.CellLookup.Add(GetCellKey(InFloor), Snapshot.Cells.Add(InFloor));
    Snapshot.MapBounds += FVector2D(InFloor.X, InFloor.Y);
}

void UWandererMapComponent::AdvanceEntranceScan()
{
    if (NextDoor >= DoorCandidates.Num())
    {
        DoorCandidates.Reset();
        EntranceBuckets.Reset();
        Snapshot.bScanning = false;
        Phase = EScanPhase::Complete;
        return;
    }
    if (const UStaticMeshComponent* Door = DoorCandidates[NextDoor++].Get();
        Door && Door->IsRegistered() && Door->GetWorld() == GetWorld()
        && Door->GetStaticMesh() && Door->GetStaticMesh()->GetName().Contains(TEXT("Door")))
    {
        TryAddEntrance(*Door);
    }
}

void UWandererMapComponent::TryAddEntrance(const UStaticMeshComponent& InMesh)
{
    FVector Min;
    FVector Max;
    InMesh.GetLocalBounds(Min, Max);
    const FTransform Transform = InMesh.GetComponentTransform();
    const FVector Center = Transform.TransformPosition(
        FVector((Min.X + Max.X) / 2, (Min.Y + Max.Y) / 2, Min.Z));
    const FVector Axis = Transform.TransformVectorNoScale(
        (Max.X - Min.X < Max.Y - Min.Y) ? FVector::ForwardVector : FVector::RightVector);
    FVector Front;
    FVector Back;
    if (!FindFloor(Center + Axis * (Radius + CellSize), Front)
        || !FindFloor(Center - Axis * (Radius + CellSize), Back)
        || !HasCell(GetCellKey(Front)) || !HasCell(GetCellKey(Back)) || !CanTraverse(Front, Back))
    {
        return;
    }

    using namespace UE::Wanderer::MapScan;
    const FInt64Point Key(FMath::FloorToInt64(Center.X / EntranceMergeDistance),
        FMath::FloorToInt64(Center.Y / EntranceMergeDistance));
    for (int32 X = -1; X <= 1; ++X)
    {
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            const auto* Points = EntranceBuckets.Find(Key + FInt64Point(X, Y));
            if (Points && Points->ContainsByPredicate([&Center](const FVector& InExisting)
                { return FVector::DistSquared2D(Center, InExisting) < EntranceMergeDistanceSquared; }))
            {
                return;
            }
        }
    }
    EntranceBuckets.FindOrAdd(Key).Add(Center);
    Snapshot.Entrances.Add(Center);
}
