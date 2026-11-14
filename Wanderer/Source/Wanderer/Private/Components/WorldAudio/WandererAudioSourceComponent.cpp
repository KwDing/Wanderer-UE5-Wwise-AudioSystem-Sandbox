#include "Components/WorldAudio/WandererAudioSourceComponent.h"

#include "Engine/World.h"
#include "UObject/Package.h"

#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Core/ActorPathUtils.h"
#include "Interfaces/Audio/AudioGeometryProvider.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

namespace
{
    constexpr float PriorityDistanceMinM = 1.0f;
    constexpr float ProtectedPriority = 100.0f;
    constexpr float UnprotectedPriorityMax = 99.0f;
} // namespace

UWandererAudioSourceComponent::UWandererAudioSourceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UWandererAudioSourceComponent::ValidateConfiguration() const
{
    const FWandererWorldAudioProfileRow* Row = GetProfile();
    if (!Row)
    {
        return TEXT("DT类型不匹配或配置行不存在");
    }
    const FString Error = Row->Validate();
    if (!Error.IsEmpty())
    {
        return Error;
    }
    if (!FMath::IsFinite(PriorityBias))
    {
        return TEXT("Priority Bias不是有限数值");
    }
    if (Row->Mode == EWandererWorldAudioMode::Spline || Row->Mode == EWandererWorldAudioMode::Area)
    {
        const IAudioGeometryProvider* Shape = Cast<IAudioGeometryProvider>(GetOwner());
        const EAudioGeometryKind Expected = Row->Mode == EWandererWorldAudioMode::Spline
                                                ? EAudioGeometryKind::Spline
                                                : EAudioGeometryKind::Area;
        if (!Shape || Shape->GetAudioGeometryKind() != Expected)
        {
            return TEXT("Profile requires a matching audio geometry provider");
        }
        const FString ShapeError = Shape->GetAudioGeometryError();
        if (!ShapeError.IsEmpty())
        {
            return ShapeError;
        }
        if (!FMath::IsFinite(Row->AreaSpreadRadiusM) || Row->AreaSpreadRadiusM <= 0)
        {
            return TEXT("Area/Spline radius must be positive");
        }
    }
    if (Row->Mode == EWandererWorldAudioMode::Room)
    {
        const AAkSpatialAudioVolume* Room = ResolveRoom();
        if (!Room || !Room->Room || !Room->Room->bEnable)
        {
            return TEXT("Room引用未加载、跨世界或未启用");
        }
        if (Room->Room->AutoPost)
        {
            return TEXT("Room Auto Post必须关闭：防止与管理组件重复播放");
        }
    }
    return FString();
}

const FWandererWorldAudioProfileRow* UWandererAudioSourceComponent::GetProfile() const
{
    const UDataTable* Table = Profile.DataTable;
    return Table && Table->GetRowStruct() == FWandererWorldAudioProfileRow::StaticStruct()
               ? Table->FindRow<FWandererWorldAudioProfileRow>(
                     Profile.RowName, TEXT("World audio"), false)
               : nullptr;
}

AAkSpatialAudioVolume* UWandererAudioSourceComponent::ResolveRoom() const
{
    AActor* Actor = AcousticRoom.Get();
    if (Actor && Actor->GetWorld() == GetWorld())
    {
        return Cast<AAkSpatialAudioVolume>(Actor);
    }
    const FSoftObjectPath Path =
        ActorPathUtils::ForWorld(AcousticRoom.ToSoftObjectPath(), GetWorld());
    Actor = Cast<AActor>(Path.ResolveObject());
    return Actor && Actor->GetWorld() == GetWorld() ? Cast<AAkSpatialAudioVolume>(Actor) : nullptr;
}

bool UWandererAudioSourceComponent::Evaluate(const FVector& InListener)
{
    bProtected = false;
    InsideBlend = 0;
    if (UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
    {
        if (!System->IsResourceGroupActive(ResourceGroup))
        {
            Status = System->GetResourceGroupStatus(ResourceGroup);
            return false;
        }
    }
    const FString Error = ValidateConfiguration();
    if (!Error.IsEmpty())
    {
        Status = Error;
        return false;
    }
    if (!bEnabled)
    {
        Status = TEXT("Disabled");
        return false;
    }
    const FWandererWorldAudioProfileRow& Row = *GetProfile();
    bool bInsideRoom = false;
    if (!EvaluateGeometry(InListener, Row, bInsideRoom))
    {
        return false;
    }
    bProtected = (Row.Mode != EWandererWorldAudioMode::Global && Row.ProtectNearM > 0
                  && DistanceM <= Row.ProtectNearM)
                 || (bInsideRoom && Row.bProtectInsideRoom);
    const float Penalty =
        FMath::Clamp(
            DistanceM / FMath::Max(Row.AudibleDistanceM, PriorityDistanceMinM), 0.0f, 1.0f)
        * Row.DistancePriorityPenalty;
    EffectivePriority =
        bProtected
            ? ProtectedPriority
            : FMath::Clamp(Row.Priority + PriorityBias - Penalty, 0.0f, UnprotectedPriorityMax);
    const float Limit = PlayingID || LoadHandle ? Row.ReleaseDistanceM : Row.ActivationDistanceM;
    if (DistanceM > Limit)
    {
        Status = TEXT("Distance culled");
        return false;
    }
    return true;
}

void UWandererAudioSourceComponent::SetEvaluationState(float InPriority,
                                                       bool bInProtected,
                                                       const FString& InStatus)
{
    EffectivePriority = InPriority;
    bProtected = bInProtected;
    DistanceM = 0;
    Status = InStatus;
}

void UWandererAudioSourceComponent::BeginPlay()
{
    Super::BeginPlay();
    if (UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
    {
        System->RegisterSource(this);
    }
}

void UWandererAudioSourceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWandererAudioSubsystem* System = GetWorld()->GetSubsystem<UWandererAudioSubsystem>())
    {
        System->UnregisterSource(this);
    }
    ShutdownPlayback();
    if (OwnedEmitter)
    {
        OwnedEmitter->DestroyComponent();
    }
    OwnedEmitter = nullptr;
    Super::EndPlay(Reason);
}
