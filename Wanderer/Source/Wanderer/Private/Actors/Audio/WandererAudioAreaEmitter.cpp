#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#if WITH_EDITOR
#include "Components/BoxComponent.h"
#endif

AWandererAudioAreaEmitter::AWandererAudioAreaEmitter()
{
    PrimaryActorTick.bCanEverTick = false;
    AudioSource = CreateDefaultSubobject<UWandererAudioSourceComponent>(TEXT("AudioSource"));
#if WITH_EDITORONLY_DATA
    EditorFocusBounds = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("EditorFocusBounds"));
    if (EditorFocusBounds)
    {
        EditorFocusBounds->SetupAttachment(RootComponent);
        EditorFocusBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        EditorFocusBounds->SetGenerateOverlapEvents(false);
        EditorFocusBounds->SetCanEverAffectNavigation(false);
        EditorFocusBounds->SetVisibility(false);
        EditorFocusBounds->SetHiddenInGame(true);
    }
#endif
}

#if WITH_EDITOR
void AWandererAudioAreaEmitter::RefreshEditorFocusBounds()
{
    if (!EditorFocusBounds)
    {
        return;
    }
    FBox LocalBounds(ForceInit);
    const double HalfCell = FMath::Max(1.0f, CellSize) * 0.5;
    for (const FVector& Cell : GetSurfaceCells())
    {
        if (Cell.ContainsNaN())
        {
            continue;
        }
        LocalBounds += Cell - FVector(HalfCell, HalfCell, FMath::Max(0.0f, DepthBelowSurface));
        LocalBounds += Cell + FVector(HalfCell, HalfCell, FMath::Max(0.0f, HeightAboveSurface));
    }
    // Empty geometry remains focusable at the actor origin; never include world origin in valid
    // bounds.
    EditorFocusBounds->SetRelativeLocation(
        LocalBounds.IsValid ? LocalBounds.GetCenter() : FVector::ZeroVector);
    EditorFocusBounds->SetBoxExtent(
        LocalBounds.IsValid ? LocalBounds.GetExtent() : FVector(50.0), false);
}

void AWandererAudioAreaEmitter::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    RefreshEditorFocusBounds();
}

void AWandererAudioAreaEmitter::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    RefreshEditorFocusBounds();
}

void AWandererAudioAreaEmitter::PostEditUndo()
{
    Super::PostEditUndo();
    RefreshEditorFocusBounds();
}
#endif
