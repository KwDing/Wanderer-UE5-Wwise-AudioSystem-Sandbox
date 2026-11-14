#include "Systems/Audio/WandererAudioSubsystem.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"

#include "AkAcousticPortal.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"

#include "Core/ActorPathUtils.h"

namespace
{
    AActor* ResolveActor(const TSoftObjectPtr<AActor>& InReference, UWorld* InWorld)
    {
        AActor* Actor = InReference.Get();
        if (Actor && Actor->GetWorld() == InWorld)
        {
            return Actor;
        }

        const FSoftObjectPath Path =
            ActorPathUtils::ForWorld(InReference.ToSoftObjectPath(), InWorld);
        Actor = Cast<AActor>(Path.ResolveObject());
        return Actor && Actor->GetWorld() == InWorld ? Actor : nullptr;
    }

    void ChangeRegistration(UActorComponent* InComponent,
                            bool bInActive,
                            TSet<TWeakObjectPtr<UActorComponent>>& InOutSuspended)
    {
        if (!IsValid(InComponent))
        {
            return;
        }

        if (bInActive)
        {
            if (InOutSuspended.Remove(InComponent))
            {
                InComponent->RegisterComponent();
            }
            return;
        }

        if (InComponent->IsRegistered())
        {
            InComponent->UnregisterComponent();
            InOutSuspended.Add(InComponent);
        }
    }

    FString ValidateMembers(const TArray<TSoftObjectPtr<AActor>>& InMembers,
                            UWorld* InWorld,
                            TSet<FSoftObjectPath>& InOutMembers)
    {
        for (const TSoftObjectPtr<AActor>& Member : InMembers)
        {
            const FSoftObjectPath Path = Member.ToSoftObjectPath();
            if (Path.IsNull() || InOutMembers.Contains(Path))
            {
                return TEXT("Room/Portal must belong to exactly one group; ")
                    TEXT("use Dependencies for shared connections");
            }

            if (!ResolveActor(Member, InWorld))
            {
                return TEXT("Spatial member missing from this world");
            }

            InOutMembers.Add(Path);
        }
        return FString();
    }

} // namespace

void UWandererAudioSubsystem::SetSpatialResources(const FWandererAudioResourceGroup& InGroup,
                                                  bool bInActive)
{
    const auto Rooms = [this, &InGroup, bInActive]()
    {
        for (const TSoftObjectPtr<AActor>& Reference : InGroup.Rooms)
        {
            AAkSpatialAudioVolume* Room =
                Cast<AAkSpatialAudioVolume>(ResolveActor(Reference, GetWorld()));
            if (Room)
            {
                ChangeRegistration(Room->Room, bInActive, SuspendedComponents);
            }
        }
    };
    const auto Portals = [this, &InGroup, bInActive]()
    {
        for (const TSoftObjectPtr<AActor>& Reference : InGroup.Portals)
        {
            AActor* Actor = ResolveActor(Reference, GetWorld());
            if (Actor)
            {
                ChangeRegistration(Actor->FindComponentByClass<UAkPortalComponent>(),
                                   bInActive,
                                   SuspendedComponents);
            }
        }
    };

    // These lambdas run synchronously. Connections never outlive their rooms.
    if (bInActive)
    {
        Rooms();
        Portals();
    }
    else
    {
        Portals();
        Rooms();
    }
}

FString UWandererAudioSubsystem::ValidateResourceGroups() const
{
    const FString LayoutError = FWandererAudioResourceGroup::ValidateLayout(ResourceConfig);
    if (!LayoutError.IsEmpty())
    {
        return LayoutError;
    }

    TSet<FSoftObjectPath> SpatialMembers;
    for (const FWandererAudioResourceGroup& Group : ResourceConfig)
    {
        FString Error = ValidateMembers(Group.Rooms, GetWorld(), SpatialMembers);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        Error = ValidateMembers(Group.Portals, GetWorld(), SpatialMembers);
        if (!Error.IsEmpty())
        {
            return Error;
        }
    }
    return FString();
}
