#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Audio/WandererCharacterAudioTypes.h"
#include "WandererCharacterAudioComponent.generated.h"

class UAkComponent;
class UAkSwitchValue;
class UWandererCharacterAudioProfile;

UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class WANDERER_API UWandererCharacterAudioComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UWandererCharacterAudioComponent();

    /** Bind on the game thread after registration. EndPlay closes the current play lifetime. */
    UFUNCTION(BlueprintCallable, Category = "Audio")
    bool InitializeAudio(UAkComponent* InEmitter);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    int32 PlayAction(FName ActionID,
        const FWandererCharacterAudioContext& Context,
        EWandererAudioPlaybackScope Scope = EWandererAudioPlaybackScope::Default);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    FWandererAudioExecutionResult ExecuteAction(FName ActionID,
        const FWandererCharacterAudioContext& Context,
        EWandererAudioPlaybackScope Scope = EWandererAudioPlaybackScope::Default);

    UFUNCTION(BlueprintCallable, Category = "Audio")
    void StopPlayback();

    UFUNCTION(BlueprintPure, Category = "Audio")
    bool IsAudioReady() const
    {
        return bReady;
    }

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    friend class FCharacterAudioLifetimeTest;
    struct FActionCooldowns
    {
        double Default = 0.0;
        double LeftFoot = 0.0;
        double RightFoot = 0.0;
        double& Get(EWandererAudioPlaybackScope Scope);
    };

    UPROPERTY(EditAnywhere,
        BlueprintReadOnly,
        Category = "Configuration",
        meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWandererCharacterAudioProfile> Profile;

    /** Observed emitter; the character owns it. */
    UPROPERTY(Transient)
    TWeakObjectPtr<UAkComponent> EmitterWeakPtr;

    UPROPERTY(Transient)
    TMap<FName, FWandererCharacterAudioActionRow> CachedActions;

    UPROPERTY(Transient)
    TMap<TEnumAsByte<EPhysicalSurface>, TObjectPtr<UAkSwitchValue>> SurfaceSwitches;

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UAkSwitchValue>> GaitSwitches;

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UAkSwitchValue>> WetnessSwitches;

    TMap<FName, FActionCooldowns> NextAllowedTimes;
    TSet<FName> WarnedActions;
    TMap<FName, FName> LastSkipReasons;
    bool bReady = false;
    bool bStopped = false;

    FWandererAudioExecutionResult ExecuteAudioActions(FName ActionID,
        const FWandererCharacterAudioActionRow& InRow,
        const FWandererCharacterAudioContext& Context,
        bool& bOutAttempted);
    bool FailConfiguration(const FString& Message) const;
    void ReportSkip(FName ActionID, const TCHAR* Reason, const TCHAR* Message);
    void ResetProfileCache();
    bool BuildProfileCache();
    bool CacheSwitchRow(const UDataTable& Table,
        FName RowName,
        const FWandererCharacterAudioSwitchRow& Row,
        TSet<FName>& LayerKeys);
    bool ValidateAudioActions() const;
    bool ReadActionTables(const UWandererCharacterAudioProfile& Source);
    bool ReadSwitchTables(const UWandererCharacterAudioProfile& Source);

    void WarnOnce(FName ActionID, const TCHAR* Message);

};
