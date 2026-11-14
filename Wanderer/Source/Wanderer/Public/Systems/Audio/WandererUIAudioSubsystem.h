#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Audio/UIAudioTypes.h"
#include "WandererUIAudioSubsystem.generated.h"

class UWandererUIAudioProfile;
class UAkAudioEvent;

/** Runtime profile ownership must be GC-scanned, unlike settings CDOs in cooked builds. */
UCLASS()
class WANDERER_API UWandererUIAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    /**
     * Game-thread, local-client request. Caller validates local input ownership.
     * InWorld must belong to this GameInstance; only the class is used for profile routing.
     * Returns zero for silent, stopped, invalid or failed requests. No widget is retained.
     */
    int32 PlayAction(UWorld* InWorld, const UClass* InScreenClass, EWandererUIAudioAction InAction);
    UWandererUIAudioProfile* ResolveProfile(const UClass* InScreenClass);

private:
    UPROPERTY(Transient)
    TMap<FSoftObjectPath, TObjectPtr<UWandererUIAudioProfile>> LoadedProfiles;

    bool bStopped = false;
    bool bResolving = false;

    bool CanPlay(const UWorld* InWorld) const;
    int32 PostFiniteEvent(UWorld* InWorld, UAkAudioEvent* InEvent);
};
