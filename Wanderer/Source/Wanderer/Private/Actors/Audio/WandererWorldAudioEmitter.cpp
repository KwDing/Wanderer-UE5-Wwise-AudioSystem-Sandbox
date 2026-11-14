#include "Actors/Audio/WandererWorldAudioEmitter.h"
#include "Components/SceneComponent.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"

AWandererWorldAudioEmitter::AWandererWorldAudioEmitter()
{
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    AudioSource = CreateDefaultSubobject<UWandererAudioSourceComponent>(TEXT("AudioSource"));
}
