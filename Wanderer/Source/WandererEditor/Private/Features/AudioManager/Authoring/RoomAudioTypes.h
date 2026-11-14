#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

class UWandererAudioSourceComponent;

enum class ERoomAudioEdit : uint8
{
    Create,
    Profile,
    Events,
    CreateOrApply,
    Associate
};

struct FRoomAudioEditRequest
{
    ERoomAudioEdit Action = ERoomAudioEdit::Create;
    TWeakObjectPtr<UWorld> WorldWeakPtr;
    TArray<TWeakObjectPtr<AActor>> RoomsWeakPtr;
    TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> SourcesWeakPtr;
    FDataTableRowHandle Profile;
    FSoftObjectPath TableSoftRef;
    FSoftObjectPath EventSoftRef;
    FSoftObjectPath NightEventSoftRef;
    TFunction<bool()> CanContinue;
};
