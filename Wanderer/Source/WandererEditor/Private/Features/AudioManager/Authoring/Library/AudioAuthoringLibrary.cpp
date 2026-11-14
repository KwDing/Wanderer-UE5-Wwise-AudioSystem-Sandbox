#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"

#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

UWandererAudioSourceComponent* UAudioAuthoringLibrary::AttachRoomAudioComponent(
    AActor* RoomActor, UWandererAudioSourceComponent* TemplateSource)
{
    auto* Room = Cast<AAkSpatialAudioVolume>(RoomActor);
    if (!IsValid(Room) || !GEditor || GEditor->PlayWorld
        || Room->GetWorld() != GEditor->GetEditorWorldContext().World()
        || FLevelUtils::IsLevelLocked(Room->GetLevel()) || !Room->Room || !Room->Room->bEnable
        || Room->Room->AutoPost || Room->FindComponentByClass<UWandererAudioSourceComponent>())
    {
        return nullptr;
    }
    if (TemplateSource
        && (!IsValid(TemplateSource) || TemplateSource->GetWorld() != Room->GetWorld()
            || TemplateSource->ResolveRoom() != Room || !TemplateSource->GetProfile()
            || TemplateSource->GetProfile()->Mode != EWandererWorldAudioMode::Room))
    {
        return nullptr;
    }
    const FScopedTransaction Transaction(
        NSLOCTEXT("WorldAudio", "AttachRoom", "Attach Room ambience component"));
    Room->Modify();
    auto* Source = NewObject<UWandererAudioSourceComponent>(Room, NAME_None, RF_Transactional);
    if (TemplateSource)
    {
        Source->Profile = TemplateSource->Profile;
        Source->Area = TemplateSource->Area;
        Source->ResourceGroup = TemplateSource->ResourceGroup;
        Source->bEnabled = TemplateSource->bEnabled;
        Source->PriorityBias = TemplateSource->PriorityBias;
    }
    Source->AcousticRoom = Room;
    Room->AddInstanceComponent(Source);
    Source->OnComponentCreated();
    Source->RegisterComponent();
    Room->MarkPackageDirty();
    return Source;
}

TArray<FString> UAudioAuthoringLibrary::FindRoomAudioActorReferences(AActor* Target)
{
    TArray<FString> Result;
    if (!IsValid(Target) || !GEditor || GEditor->PlayWorld
        || Target->GetWorld() != GEditor->GetEditorWorldContext().World())
    {
        Result.Add(TEXT("Invalid editor target"));
        return Result;
    }
    const FString TargetPath = Target->GetPathName();
    for (TActorIterator<AActor> It(Target->GetWorld()); It; ++It)
    {
        if (*It == Target)
        {
            continue;
        }
        TArray<UObject*> Objects;
        Objects.Add(*It);
        GetObjectsWithOuter(*It, Objects, true);
        for (auto* Object : Objects)
        {
            for (TFieldIterator<FProperty> Property(Object->GetClass()); Property; ++Property)
            {
                if (Property->HasAnyPropertyFlags(CPF_Transient))
                {
                    continue;
                }
                FString Value;
                Property->ExportText_InContainer(0, Value, Object, Object, Object, PPF_None);
                if (Value.Contains(TargetPath))
                {
                    Result.AddUnique(Object->GetPathName() + TEXT(".") + Property->GetName());
                }
            }
        }
    }
    return Result;
}
