#include "Data/Audio/WorldAudioBudgetData.h"

namespace
{
    bool HasValidBounds(const FWandererAudioResourceGroup& InGroup)
    {
        return InGroup.MaxEvents >= 0 && !InGroup.BoundsCenter.ContainsNaN()
               && !InGroup.BoundsExtent.ContainsNaN() && InGroup.BoundsExtent.GetMin() >= 0
               && FMath::IsFinite(InGroup.LoadDistanceM) && InGroup.LoadDistanceM >= 0
               && FMath::IsFinite(InGroup.UnloadDistanceM)
               && InGroup.UnloadDistanceM >= InGroup.LoadDistanceM
               && FMath::IsFinite(InGroup.UnloadDelaySec) && InGroup.UnloadDelaySec >= 0;
    }

} // namespace

bool FWandererAudioResourceGroup::ApplyPolicy(FString& OutError)
{
    if (!Policy.DataTable && Policy.RowName.IsNone())
    {
        return true;
    }

    if (!Policy.DataTable || Policy.RowName.IsNone()
        || Policy.DataTable->GetRowStruct() != FWandererAudioResourcePolicyRow::StaticStruct())
    {
        OutError = TEXT("Resource policy requires a valid DT and ResourcePolicyRow");
        return false;
    }

    const FWandererAudioResourcePolicyRow* Row =
        Policy.DataTable->FindRow<FWandererAudioResourcePolicyRow>(Policy.RowName,
                                                                   TEXT("WorldAudioPolicy"), false);
    if (!Row)
    {
        OutError = TEXT("Resource policy row is missing");
        return false;
    }

    bAlwaysResident = Row->bAlwaysResident;
    LoadDistanceM = Row->LoadDistanceM;
    UnloadDistanceM = Row->UnloadDistanceM;
    UnloadDelaySec = Row->UnloadDelaySec;
    MaxEvents = Row->MaxEvents;
    return true;
}

FString FWandererAudioResourceGroup::ValidateLayout(
    TConstArrayView<FWandererAudioResourceGroup> InGroups)
{
    TSet<FName> Names;
    for (const FWandererAudioResourceGroup& Group : InGroups)
    {
        if (Group.Name.IsNone() || Names.Contains(Group.Name))
        {
            return TEXT("Resource group names must be non-empty and unique");
        }
        if (!HasValidBounds(Group))
        {
            return FString::Printf(TEXT("Invalid bounds or distances for resource group '%s'"),
                                   *Group.Name.ToString());
        }
        Names.Add(Group.Name);
    }

    TSet<FName> Ready;
    for (int32 Pass = 0; Pass < InGroups.Num(); ++Pass)
    {
        for (const FWandererAudioResourceGroup& Group : InGroups)
        {
            bool bReady = true;
            for (FName Dependency : Group.Dependencies)
            {
                if (!Names.Contains(Dependency))
                {
                    return FString::Printf(TEXT("Resource group '%s' has unknown dependency '%s'"),
                                           *Group.Name.ToString(), *Dependency.ToString());
                }
                bReady &= Ready.Contains(Dependency);
            }
            if (bReady)
            {
                Ready.Add(Group.Name);
            }
        }
    }
    return Ready.Num() == Names.Num() ? FString() : TEXT("Resource dependencies contain a cycle");
}
