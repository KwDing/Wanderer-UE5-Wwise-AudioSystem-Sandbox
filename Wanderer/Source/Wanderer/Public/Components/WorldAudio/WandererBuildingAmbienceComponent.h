#pragma once
#include "CoreMinimal.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "WandererBuildingAmbienceComponent.generated.h"

/** One inline record per building, owned by the persistent map's existing audio budget. */
UCLASS(ClassGroup = (Audio),
    BlueprintType,
    meta = (BlueprintSpawnableComponent),
    HideCategories = ("World Audio"))
class WANDERER_API UWandererBuildingAmbienceComponent : public UWandererAudioSourceComponent
{
    GENERATED_BODY()
  public:
    UPROPERTY(
        EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (DisplayName = "建筑名称"))
    FName BuildingName;
    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Building",
        meta = (DisplayName = "Play Event",
            ToolTip =
                "Play_Amb_room 已配置建筑底噪控制。自定义 Event 的声音须支持 BuildingAmbience_Mode / Gain 本地 RTPC，或自行配置非空间化播放及淡入。"))
    TSoftObjectPtr<UAkAudioEvent> BuildingEvent;
    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Building",
        meta = (DisplayName = "Night Event（空=沿用 Play）"))
    TSoftObjectPtr<UAkAudioEvent> BuildingNightEvent;
    UPROPERTY(
        EditAnywhere, BlueprintReadWrite, Category = "Building", meta = (DisplayName = "启用"))
    bool bBuildingEnabled = true;
    UPROPERTY(EditInstanceOnly,
        BlueprintReadOnly,
        Category = "Building",
        meta = (DisplayName = "成员 Rooms",
            AllowedClasses = "/Script/AkAudio.AkSpatialAudioVolume"))
    TArray<TSoftObjectPtr<AActor>> MemberRooms;
    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Building",
        meta = (ClampMin = "0", ClampMax = "2", Units = "s", DisplayName = "淡入秒数"))
    float FadeInSeconds = .4f;
    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Building",
        meta = (ClampMin = "0", ClampMax = "2", Units = "s", DisplayName = "淡出秒数"))
    float BuildingFadeOutSeconds = .4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building",
        meta = (ClampMin = "0", Units = "s"))
    float ReverbTailHoldSec = 0;
    UPROPERTY(EditAnywhere,
        BlueprintReadWrite,
        Category = "Building",
        meta = (ClampMin = "0", ClampMax = "100", DisplayName = "优先级"))
    float BuildingPriority = 60;

    virtual const FWandererWorldAudioProfileRow* GetProfile() const override;
    virtual FString ValidateConfiguration() const override;
    virtual bool Evaluate(const FVector& Listener) override;
    virtual bool CollectAudioRoomPaths(TArray<FSoftObjectPath>& OutPaths) const override;
    virtual const UObject* GetPlaybackIdentity() const override;
    bool ContainsRoom(const AActor* Room) const;

  protected:
    virtual UAkGameObject* GetPlaybackObject() const override;
    virtual UAkGameObject* AcquirePlaybackObject() override;
    virtual void PreparePlaybackObject(UAkGameObject* Object) override;
    virtual void PlaybackStarted(UAkGameObject* Object) override;

  private:
    FString ValidateRoomMembership() const;
    FString ValidateBuildingOwnership() const;
    mutable FWandererWorldAudioProfileRow PlaybackProfile;
    FVector ListenerPosition = FVector::ZeroVector;
};
