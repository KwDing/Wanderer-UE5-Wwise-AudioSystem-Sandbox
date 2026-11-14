#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

#include "Features/AudioManager/AudioManagerData.h"

class APawn;
class AWandererAudioSplineEmitter;
class ULineBatchComponent;
class UWandererAudioSourceComponent;

/** Manager-owned, game-thread sound-source visualization. Stop is terminal; actors remain weak. */
class FAudioVisualizationController : public TSharedFromThis<FAudioVisualizationController>
{
  public:
    FAudioVisualizationController();
    ~FAudioVisualizationController();
    void Start();
    void Stop();
    void RefreshSources();
    void SetEnabled(bool bInEnabled);
    void SetRangeM(float InRangeM);
    bool IsEnabled() const;
    float GetRangeM() const;
    const FString& GetSummary() const;

  private:
    friend class FSplineCharacterLinkTest;
    friend class FAudioHiddenVisualizationTest;
    friend class FAudioManagerTableTest;
    friend class FNearbySourceRelationsProbe;
    friend class FAudioStoppedPanelTest;

    struct FPlaybackView
    {
        FTransform Transform;
        double PixelWidth = AudioVisualizationSettings::ReferenceViewportWidth;
    };
    struct FPlaybackRelation
    {
        TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
        FVector Position = FVector::ZeroVector;
        float ShapeDistanceM = 0;
        int32 Index = 0;
        bool bPosted = false;
    };

    bool bEnabled = false;
    bool bStopped = false;
    uint32 DrawBatchID;
    float RangeM = AudioVisualizationSettings::DefaultRangeM;
    double NextDiscovery = 0;
    FTSTicker::FDelegateHandle TickerHandle;
    TWeakObjectPtr<UWorld> ObservedWorldWeakPtr;
    TWeakObjectPtr<UWorld> VisualizationWorldWeakPtr;
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> SourcesWeakPtr;
    FString Summary;

    static UWorld* ResolveWorld();
    bool OnTick(float InDeltaSeconds);
    void ClearDrawing();
    void DrawNow();
    void DrawPlaybackRelations(UWorld& InWorld, ULineBatchComponent& InBatch);
    bool DrawRelation(ULineBatchComponent& InBatch,
        const FPlaybackRelation& InRelation,
        const APawn& InPawn,
        const FPlaybackView& InView,
        int32 InLabelIndex);
    static void DrawSplinePlaybackLink(ULineBatchComponent& InBatch,
        const FVector& InPosition,
        const APawn* InPawn,
        FLinearColor InColor = AudioVisualizationSettings::EditorPlaybackColor,
        uint32 InBatchID = AudioVisualizationSettings::DefaultDrawBatchID);
    static void DrawPlaybackSourceIndex(ULineBatchComponent& InBatch,
        const FVector& InPosition,
        int32 InIndex,
        FLinearColor InColor,
        const FPlaybackView& InView,
        uint32 InBatchID = AudioVisualizationSettings::DefaultDrawBatchID);
    static float GetPlaybackDistance(
        const UWandererAudioSourceComponent& InSource, const FVector& InPosition, float InRangeM);
    static TArray<FPlaybackRelation> GatherPlaybackRelations(
        const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>& InSources,
        const APawn& InPawn,
        float InRangeM);
    static void DrawPlaybackSplineOutline(ULineBatchComponent& InBatch,
        const AWandererAudioSplineEmitter& InActor,
        uint32 InBatchID = AudioVisualizationSettings::DefaultDrawBatchID);
};
