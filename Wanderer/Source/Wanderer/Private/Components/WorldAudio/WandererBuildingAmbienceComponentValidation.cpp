#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"

#include "Engine/World.h"
#include "EngineUtils.h"

#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

FString UWandererBuildingAmbienceComponent::ValidateRoomMembership() const
{
    TSet<FSoftObjectPath> Seen;
    for (const TSoftObjectPtr<AActor>& Member : MemberRooms)
    {
        const FSoftObjectPath Path = Member.ToSoftObjectPath();
        if (Path.IsNull() || Seen.Contains(Path))
        {
            return TEXT("成员 Room 为空或重复");
        }
        Seen.Add(Path);
        if (!Member.Get())
        {
            return TEXT("成员 Room 未加载或已删除；请检查成员列表（不会自动加载它）");
        }
        const AAkSpatialAudioVolume* Room = Cast<AAkSpatialAudioVolume>(Member.Get());
        if (!Room || !Room->Room || !Room->Room->bEnable || Room->Room->AutoPost)
        {
            return TEXT("成员 Room 必须启用且关闭 Auto Post");
        }
    }
    return FString();
}

FString UWandererBuildingAmbienceComponent::ValidateBuildingOwnership() const
{
    TSet<FSoftObjectPath> Seen;
    for (const TSoftObjectPtr<AActor>& Member : MemberRooms)
    {
        Seen.Add(Member.ToSoftObjectPath());
    }
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TArray<UWandererBuildingAmbienceComponent*> Buildings;
        It->GetComponents(Buildings);
        for (const UWandererBuildingAmbienceComponent* Other : Buildings)
        {
            if (Other == this)
            {
                continue;
            }
            if (Other->BuildingName == BuildingName)
            {
                return TEXT("建筑名称重复");
            }
            if (Other->MemberRooms.ContainsByPredicate(
                    [&Seen](const TSoftObjectPtr<AActor>& Member)
                    {
                        return Seen.Contains(Member.ToSoftObjectPath());
                    }))
            {
                return TEXT("Room 已属于另一个建筑底噪");
            }
        }
    }
    return FString();
}
