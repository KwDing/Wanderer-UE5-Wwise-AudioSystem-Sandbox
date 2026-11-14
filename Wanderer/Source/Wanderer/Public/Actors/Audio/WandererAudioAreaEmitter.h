#pragma once

#include "CoreMinimal.h"
#include "InteriorFitArea.h"
#include "Interfaces/Audio/AudioGeometryProvider.h"
#include "WandererAudioAreaEmitter.generated.h"

class UWandererAudioSourceComponent;
class UBoxComponent;

/** Baked lake/surface geometry with WorldAudio playback ownership; no Presence. */
UCLASS(BlueprintType)
class WANDERER_API AWandererAudioAreaEmitter : public AInteriorFitArea,
                                               public IAudioGeometryProvider
{
    GENERATED_BODY()
  public:
    AWandererAudioAreaEmitter();
    virtual EAudioGeometryKind GetAudioGeometryKind() const override;
    virtual FString GetAudioGeometryError() const override;
    virtual float GetAudioBoundsDistanceM(const FVector& InListener) const override;
    virtual bool QueryAudioGeometry(const FVector& InListener,
        float InBlendDistanceM,
        FAudioGeometrySample& OutSample) const override;

#if WITH_EDITOR
    /** Supplies native editor F framing for cell-only areas, including imported geometry. */
    void RefreshEditorFocusBounds();
    virtual void PostRegisterAllComponents() override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Audio")
    TObjectPtr<UWandererAudioSourceComponent> AudioSource;

  private:
#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TObjectPtr<UBoxComponent> EditorFocusBounds;
#endif
};
