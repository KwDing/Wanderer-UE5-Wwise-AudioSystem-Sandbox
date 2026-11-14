#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Engine/World.h"

#include "Core/Diagnostics/DiagnosticRecorder.h"
#include "Interfaces/Audio/AudioBudgetProvider.h"
#include "Systems/Audio/AudioSchedulingStateTypes.h"

namespace
{
    constexpr float CentimetersPerMeter = 100.0f;

    bool KeepGroupResident(const FWandererAudioResourceGroup& InGroup,
                           FWandererAudioResourceState& InOutState,
                           double InNow)
    {
        using EPhase = FWandererAudioResourceState::EPhase;
        const float Limit = InOutState.Phase == EPhase::Dormant
                                ? InGroup.LoadDistanceM
                                : FMath::Max(InGroup.LoadDistanceM, InGroup.UnloadDistanceM);
        if (InGroup.bAlwaysResident || InOutState.DistanceM <= Limit)
        {
            InOutState.OutsideSinceSec = -1.0;
            return true;
        }

        if (InOutState.OutsideSinceSec < 0)
        {
            InOutState.OutsideSinceSec = InNow;
        }
        return InOutState.Phase != EPhase::Dormant
               && InNow - InOutState.OutsideSinceSec < InGroup.UnloadDelaySec;
    }
} // namespace

bool UWandererAudioSubsystem::InitializeResourceGroups()
{
    if (bResourcesInitialized)
    {
        return ResourceConfigError.IsEmpty();
    }

    const IAudioBudgetProvider* Provider = Cast<IAudioBudgetProvider>(BudgetWeakPtr.Get());
    if (!Provider)
    {
        return false;
    }

    // Preserve the first resource-update snapshot, independently of live admission limits.
    Provider->SubmitAudioResources(*this);
    for (FWandererAudioResourceGroup& Group : ResourceConfig)
    {
        if (!Group.ApplyPolicy(ResourceConfigError))
        {
            break;
        }
    }

    if (ResourceConfigError.IsEmpty())
    {
        ResourceConfigError = ValidateResourceGroups();
    }
    bResourcesInitialized = true;

    if (!ResourceConfigError.IsEmpty())
    {
        WANDERER_DIAGNOSTIC(this,
                            EDiagnosticDomain::Audio,
                            TEXT("resource-config-rejected"),
                            true,
                            ResourceConfigError);
        UE_LOG(LogWandererAudio, Error, TEXT("World Audio resources: %s"), *ResourceConfigError);
        return false;
    }

    for (const FWandererAudioResourceGroup& Group : ResourceConfig)
    {
        ResourceState->Groups.Add(Group.Name);
        SetSpatialResources(Group, false);
    }
    return true;
}

TSet<FName> UWandererAudioSubsystem::FindWantedGroups(const FVector& InListener)
{
    TSet<FName> Wanted;
    const double Now = GetWorld()->GetTimeSeconds();
    for (const FWandererAudioResourceGroup& Group : ResourceConfig)
    {
        FWandererAudioResourceState& State = ResourceState->Groups.FindChecked(Group.Name);
        const FBox Bounds(Group.BoundsCenter - Group.BoundsExtent,
                          Group.BoundsCenter + Group.BoundsExtent);
        State.DistanceM =
            FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(InListener)) / CentimetersPerMeter;
        if (KeepGroupResident(Group, State, Now))
        {
            Wanted.Add(Group.Name);
        }
    }
    return Wanted;
}

void UWandererAudioSubsystem::IncludeGroupDependencies(TSet<FName>& InOutWanted) const
{
    // Fixed-point closure supports shared connections without relying on spatial triggers.
    for (int32 Pass = 0; Pass < ResourceConfig.Num(); ++Pass)
    {
        for (const FWandererAudioResourceGroup& Group : ResourceConfig)
        {
            const bool bDormant = ResourceState->Groups.FindChecked(Group.Name).Phase
                                  == FWandererAudioResourceState::EPhase::Dormant;
            if (!InOutWanted.Contains(Group.Name) && bDormant)
            {
                continue;
            }
            for (FName Dependency : Group.Dependencies)
            {
                InOutWanted.Add(Dependency);
            }
        }
    }
}

void UWandererAudioSubsystem::UpdateResourceGroups(const FVector& InListener)
{
    if (!InitializeResourceGroups())
    {
        return;
    }

    TSet<FName> Wanted = FindWantedGroups(InListener);
    IncludeGroupDependencies(Wanted);
    for (const FWandererAudioResourceGroup& Group : ResourceConfig)
    {
        UpdateGroup(Group, Wanted.Contains(Group.Name));
    }
}
