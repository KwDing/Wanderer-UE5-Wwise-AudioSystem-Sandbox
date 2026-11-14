#include "Features/AudioManager/Authoring/BuildingAudioEditOperation.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioEvent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"

namespace
{
    bool bEditingBuilding = false;

    bool IsContextLive(UWorld* InWorld, const TFunction<bool()>& InCanContinue)
    {
        if (InCanContinue && !InCanContinue())
        {
            return false;
        }
        return IsValid(InWorld) && !InWorld->bIsTearingDown && GEditor && !GEditor->PlayWorld
               && InWorld == GEditor->GetEditorWorldContext().World();
    }

    bool CanStart()
    {
        return IsInGameThread() && GEditor && !GEditor->PlayWorld && !GIsTransacting
               && !GEditor->IsTransactionActive() && !bEditingBuilding;
    }

    bool IsOwnerLive(AActor* InOwner, UWorld* InWorld, ULevel* InLevel)
    {
        return IsValid(InOwner) && !InOwner->IsActorBeingDestroyed()
               && InOwner->GetWorld() == InWorld && InOwner->GetLevel() == InLevel
               && IsValid(InLevel) && !FLevelUtils::IsLevelLocked(InLevel);
    }

    AWandererWorldAudioBudget* FindBudget(UWorld* InWorld)
    {
        AWandererWorldAudioBudget* Budget = nullptr;
        for (TActorIterator<AWandererWorldAudioBudget> It(InWorld); It; ++It)
        {
            if (Budget || It->GetLevel() != InWorld->PersistentLevel)
            {
                return nullptr;
            }
            Budget = *It;
        }
        return Budget;
    }

    bool MatchesMembers(const UWandererBuildingAmbienceComponent& InBuilding,
                        const TArray<TWeakObjectPtr<AActor>>& InRoomsWeakPtr)
    {
        if (InBuilding.MemberRooms.Num() != InRoomsWeakPtr.Num())
        {
            return false;
        }
        for (const TWeakObjectPtr<AActor>& RoomWeakPtr : InRoomsWeakPtr)
        {
            if (!RoomWeakPtr.IsValid() || !InBuilding.ContainsRoom(RoomWeakPtr.Get()))
            {
                return false;
            }
        }
        return true;
    }

    bool HasGroup(const AWandererWorldAudioBudget* InBudget, FName InGroup)
    {
        return InGroup.IsNone()
               || (IsValid(InBudget)
                   && InBudget->ResourceGroups.ContainsByPredicate(
                       [InGroup](const FWandererAudioResourceGroup& InItem)
                       { return InItem.Name == InGroup; }));
    }

    bool ValidateRooms(const TArray<TWeakObjectPtr<AActor>>& InRoomsWeakPtr, UWorld* InWorld,
                       FName InName, const UWandererBuildingAmbienceComponent* InIgnored)
    {
        for (const TWeakObjectPtr<AActor>& RoomWeakPtr : InRoomsWeakPtr)
        {
            const AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(RoomWeakPtr.Get());
            if (!IsValid(Room) || Room->GetWorld() != InWorld || !IsValid(Room->Room)
                || !Room->Room->bEnable || Room->Room->AutoPost
                || FLevelUtils::IsLevelLocked(Room->GetLevel()))
            {
                return false;
            }
        }
        for (TActorIterator<AActor> It(InWorld); It; ++It)
        {
            TArray<UWandererBuildingAmbienceComponent*> Buildings;
            It->GetComponents(Buildings);
            for (const UWandererBuildingAmbienceComponent* Building : Buildings)
            {
                if (Building == InIgnored)
                {
                    continue;
                }
                if (Building->BuildingName == InName)
                {
                    return false;
                }
                for (const TWeakObjectPtr<AActor>& RoomWeakPtr : InRoomsWeakPtr)
                {
                    if (Building->ContainsRoom(RoomWeakPtr.Get()))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    struct FBuildingGroupChange
    {
        TWeakObjectPtr<UWandererBuildingAmbienceComponent> BuildingWeakPtr;
        TWeakObjectPtr<AActor> OwnerWeakPtr;
        TWeakObjectPtr<ULevel> LevelWeakPtr;
        FName Previous;
        bool bWritten = false;

        bool IsLive(UWorld* InWorld) const
        {
            return BuildingWeakPtr.IsValid()
                   && IsOwnerLive(OwnerWeakPtr.Get(), InWorld, LevelWeakPtr.Get())
                   && BuildingWeakPtr->GetOwner() == OwnerWeakPtr.Get()
                   && OwnerWeakPtr->GetInstanceComponents().Contains(BuildingWeakPtr.Get());
        }
    };

    bool RestoreGroups(TArray<FBuildingGroupChange>& InOutChanges, UWorld* InWorld, FName InGroup)
    {
        bool bRestored = true;
        for (FBuildingGroupChange& Change : InOutChanges)
        {
            if (!Change.bWritten)
            {
                continue;
            }
            if (!Change.IsLive(InWorld) || Change.BuildingWeakPtr->ResourceGroup != InGroup)
            {
                bRestored = false;
                continue;
            }
            Change.BuildingWeakPtr->ResourceGroup = Change.Previous;
            Change.BuildingWeakPtr->PostEditChange();
            bRestored = Change.IsLive(InWorld)
                        && Change.BuildingWeakPtr->ResourceGroup == Change.Previous && bRestored;
        }
        return bRestored;
    }
} // namespace

UWandererBuildingAmbienceComponent* FBuildingAudioEditOperation::Create(
    const TArray<AActor*>& InRooms, FName InName, UAkAudioEvent* InEvent, FString& OutError,
    TFunction<bool()> InCanContinue)
{
    OutError = TEXT("未创建：上下文、名称、Event、Room 归属或持久关卡 Budget 无效。");
    if (!CanStart() || InRooms.IsEmpty() || InName.IsNone() || !IsValid(InEvent))
    {
        return nullptr;
    }
    TGuardValue<bool> Guard(bEditingBuilding, true);
    const TStrongObjectPtr<UAkAudioEvent> Event(InEvent);
    const TWeakObjectPtr<UWorld> WorldWeakPtr = GEditor->GetEditorWorldContext().World();
    if (!IsContextLive(WorldWeakPtr.Get(), InCanContinue))
    {
        return nullptr;
    }
    const TWeakObjectPtr<AWandererWorldAudioBudget> BudgetWeakPtr = FindBudget(WorldWeakPtr.Get());
    const TWeakObjectPtr<ULevel> LevelWeakPtr = WorldWeakPtr->PersistentLevel;
    TArray<TWeakObjectPtr<AActor>> RoomsWeakPtr;
    for (AActor* Room : InRooms)
    {
        RoomsWeakPtr.AddUnique(Room);
    }
    if (!IsOwnerLive(BudgetWeakPtr.Get(), WorldWeakPtr.Get(), LevelWeakPtr.Get())
        || !ValidateRooms(RoomsWeakPtr, WorldWeakPtr.Get(), InName, nullptr))
    {
        return nullptr;
    }
    FScopedTransaction Transaction(
        NSLOCTEXT("WorldAudio", "CreateBuilding", "Create building audio"));
    BudgetWeakPtr->Modify();
    if (!IsContextLive(WorldWeakPtr.Get(), InCanContinue)
        || !IsOwnerLive(BudgetWeakPtr.Get(), WorldWeakPtr.Get(), LevelWeakPtr.Get())
        || !ValidateRooms(RoomsWeakPtr, WorldWeakPtr.Get(), InName, nullptr))
    {
        Transaction.Cancel();
        return nullptr;
    }
    UWandererBuildingAmbienceComponent* Building = NewObject<UWandererBuildingAmbienceComponent>(
        BudgetWeakPtr.Get(), NAME_None, RF_Transactional);
    const TWeakObjectPtr<UWandererBuildingAmbienceComponent> BuildingWeakPtr = Building;
    Building->BuildingName = InName;
    Building->BuildingEvent = Event.Get();
    for (const TWeakObjectPtr<AActor>& RoomWeakPtr : RoomsWeakPtr)
    {
        Building->MemberRooms.Add(RoomWeakPtr.Get());
    }
    BudgetWeakPtr->AddInstanceComponent(Building);
    Building->OnComponentCreated();
    if (BuildingWeakPtr.IsValid() && BudgetWeakPtr.IsValid())
    {
        BuildingWeakPtr->RegisterComponent();
    }
    const bool bValid
        = IsContextLive(WorldWeakPtr.Get(), InCanContinue)
          && IsOwnerLive(BudgetWeakPtr.Get(), WorldWeakPtr.Get(), LevelWeakPtr.Get())
          && BuildingWeakPtr.IsValid() && BuildingWeakPtr->GetOwner() == BudgetWeakPtr.Get()
          && BuildingWeakPtr->IsRegistered()
          && BudgetWeakPtr->GetInstanceComponents().Contains(BuildingWeakPtr.Get())
          && BuildingWeakPtr->BuildingName == InName
          && BuildingWeakPtr->BuildingEvent == Event.Get()
          && MatchesMembers(*BuildingWeakPtr.Get(), RoomsWeakPtr)
          && ValidateRooms(RoomsWeakPtr, WorldWeakPtr.Get(), InName, BuildingWeakPtr.Get());
    if (!bValid)
    {
        if (BuildingWeakPtr.IsValid())
        {
            BuildingWeakPtr->DestroyComponent();
        }
        Transaction.Cancel();
        OutError = TEXT("创建中止：提交通知改变上下文；本次新组件已清理。");
        return nullptr;
    }
    OutError.Reset();
    return BuildingWeakPtr.Get();
}

bool FBuildingAudioEditOperation::ApplyResourceGroup(
    const TArray<UWandererBuildingAmbienceComponent*>& InBuildings, FName InGroup, UWorld* InWorld,
    FString& OutError, TFunction<bool()> InCanContinue)
{
    OutError = TEXT("未修改：目标、资源组或编辑上下文无效。");
    if (!CanStart() || InBuildings.IsEmpty() || !IsContextLive(InWorld, InCanContinue))
    {
        return false;
    }
    TGuardValue<bool> Guard(bEditingBuilding, true);
    const TWeakObjectPtr<UWorld> WorldWeakPtr = InWorld;
    const TWeakObjectPtr<AWandererWorldAudioBudget> BudgetWeakPtr = FindBudget(InWorld);
    if (!HasGroup(BudgetWeakPtr.Get(), InGroup))
    {
        return false;
    }
    TArray<FBuildingGroupChange> Changes;
    TSet<UWandererBuildingAmbienceComponent*> Seen;
    for (UWandererBuildingAmbienceComponent* Building : InBuildings)
    {
        if (!IsValid(Building) || !IsValid(Building->GetOwner()) || Seen.Contains(Building))
        {
            return false;
        }
        FBuildingGroupChange Change{Building, Building->GetOwner(),
                                    Building->GetOwner()->GetLevel(), Building->ResourceGroup};
        if (!Change.IsLive(InWorld))
        {
            return false;
        }
        Seen.Add(Building);
        Changes.Add(Change);
    }
    FScopedTransaction Transaction(
        NSLOCTEXT("WorldAudio", "BuildingGroup", "Set building audio group"));
    bool bValid = true;
    for (FBuildingGroupChange& Change : Changes)
    {
        if (!IsContextLive(WorldWeakPtr.Get(), InCanContinue) || !Change.IsLive(WorldWeakPtr.Get())
            || Change.BuildingWeakPtr->ResourceGroup != Change.Previous)
        {
            bValid = false;
            break;
        }
        Change.BuildingWeakPtr->Modify();
        if (!IsContextLive(WorldWeakPtr.Get(), InCanContinue) || !Change.IsLive(WorldWeakPtr.Get())
            || Change.BuildingWeakPtr->ResourceGroup != Change.Previous)
        {
            bValid = false;
            break;
        }
        Change.bWritten = true;
        Change.BuildingWeakPtr->ResourceGroup = InGroup;
        Change.BuildingWeakPtr->PostEditChange();
    }
    bValid = bValid && IsContextLive(WorldWeakPtr.Get(), InCanContinue);
    for (const FBuildingGroupChange& Change : Changes)
    {
        bValid = bValid && Change.IsLive(WorldWeakPtr.Get())
                 && Change.BuildingWeakPtr->ResourceGroup == InGroup;
    }
    if (!bValid
        || !HasGroup(BudgetWeakPtr.Get(), InGroup)
        || (!InGroup.IsNone() && FindBudget(WorldWeakPtr.Get()) != BudgetWeakPtr.Get()))
    {
        const bool bRestored = RestoreGroups(Changes, WorldWeakPtr.Get(), InGroup);
        OutError = bRestored ? TEXT("修改中止；本批资源组已恢复。")
                             : TEXT("修改中止；存在外部冲突，保留本批 Undo，请检查后撤销。");
        if (bRestored)
        {
            Transaction.Cancel();
        }
        return false;
    }
    OutError.Reset();
    return true;
}
