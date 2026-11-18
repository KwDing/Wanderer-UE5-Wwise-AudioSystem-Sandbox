#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Audio/UIAudioTypes.h"
#include "Types/Audio/AudioSchedulingTypes.h"
#include "WandererUIAudioSubsystem.generated.h"

class UWandererUIAudioProfile;
class UAkAudioEvent;
class UAudioEventLeaseManager;
struct FStreamableHandle;

/** Runtime profile ownership must be GC-scanned, unlike settings CDOs in cooked builds. */
UCLASS()
class WANDERER_API UWandererUIAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void PreloadConfiguredProfiles();

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

    UPROPERTY(Transient)
    TObjectPtr<UAudioEventLeaseManager> EventLeases;

    TMap<FSoftObjectPath, TSharedPtr<FStreamableHandle>> ProfileRequests;
    TMap<TWeakObjectPtr<UAkAudioEvent>, EAudioEventResourceStatus> EventRequests;
    FTSTicker::FDelegateHandle PreloadTicker;
    bool bStopped = false;
    bool bResolving = false;

    bool CanPlay(const UWorld* InWorld) const;
    void RequestProfile(const FSoftObjectPath& InPath);
    void RequestEvent(UAkAudioEvent* InEvent);
    bool AdvancePreloads(float InDeltaSec);
    void PreloadProfileEvents(const UWandererUIAudioProfile& InProfile);
    int32 PostFiniteEvent(UWorld* InWorld, UAkAudioEvent* InEvent);
};
