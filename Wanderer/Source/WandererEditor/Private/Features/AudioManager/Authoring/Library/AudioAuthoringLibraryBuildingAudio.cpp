#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"

#include "Editor.h"

#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Features/AudioManager/Authoring/AudioComponentDeletionOperation.h"
#include "Features/AudioManager/Authoring/BuildingAudioEditOperation.h"

UWandererBuildingAmbienceComponent* UAudioAuthoringLibrary::CreateBuildingAmbience(
    const TArray<AActor*>& Rooms, FName Name, UAkAudioEvent* Event)
{
    FString Error;
    return FBuildingAudioEditOperation::Create(Rooms, Name, Event, Error);
}

bool UAudioAuthoringLibrary::DeleteBuildingAmbience(UWandererBuildingAmbienceComponent* Building)
{
    FString Error;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    return FAudioComponentDeletionOperation::Execute({Building}, World, Error);
}
