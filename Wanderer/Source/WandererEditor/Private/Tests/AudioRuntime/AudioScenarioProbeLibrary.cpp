#include "Tests/AudioRuntime/AudioScenarioProbeLibrary.h"

#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/JsonSerializer.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Systems/Audio/WandererAudioSubsystem.h"

namespace
{
    TArray<TSharedPtr<FJsonValue>> VectorValues(const FVector& InValue)
    {
        return {MakeShared<FJsonValueNumber>(InValue.X),
            MakeShared<FJsonValueNumber>(InValue.Y),
            MakeShared<FJsonValueNumber>(InValue.Z)};
    }

    TSharedRef<FJsonObject> CaptureGroup(
        FWandererAudioResourceGroup InGroup, UWandererAudioSubsystem& InSystem)
    {
        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        FString Error;
        Result->SetBoolField(TEXT("policy_valid"), InGroup.ApplyPolicy(Error));
        Result->SetStringField(TEXT("error"), Error);
        Result->SetStringField(TEXT("name"), InGroup.Name.ToString());
        Result->SetBoolField(TEXT("active"), InSystem.IsResourceGroupActive(InGroup.Name));
        Result->SetStringField(TEXT("status"), InSystem.GetResourceGroupStatus(InGroup.Name));
        Result->SetNumberField(TEXT("admitted"), InSystem.GetResourceGroupAdmitted(InGroup.Name));
        Result->SetNumberField(TEXT("limit"), InSystem.GetResourceGroupLimit(InGroup.Name));
        Result->SetBoolField(TEXT("always"), InGroup.bAlwaysResident);
        Result->SetNumberField(TEXT("load_m"), InGroup.LoadDistanceM);
        Result->SetNumberField(TEXT("unload_m"), InGroup.UnloadDistanceM);
        Result->SetNumberField(TEXT("unload_delay_s"), InGroup.UnloadDelaySec);
        Result->SetArrayField(TEXT("center"), VectorValues(InGroup.BoundsCenter));
        Result->SetArrayField(TEXT("extent"), VectorValues(InGroup.BoundsExtent));
        TArray<TSharedPtr<FJsonValue>> Events;
        for (const TSoftObjectPtr<UAkAudioEvent>& EventSoftPtr : InGroup.PreloadEvents)
        {
            TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
            Event->SetStringField(TEXT("path"), EventSoftPtr.ToString());
            Event->SetBoolField(TEXT("resident"), InSystem.IsEventDataResident(EventSoftPtr.Get()));
            Events.Add(MakeShared<FJsonValueObject>(Event));
        }
        Result->SetArrayField(TEXT("events"), Events);
        return Result;
    }

    TArray<TSharedPtr<FJsonValue>> CaptureEmitters(
        UWorld& InWorld, TArray<TSharedPtr<FJsonValue>>& OutErrors)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        IWwiseSoundEngineAPI* Engine = IWwiseSoundEngineAPI::Get();
        if (!Engine || !Engine->Query)
        {
            OutErrors.Add(MakeShared<FJsonValueString>(TEXT("Sound engine unavailable")));
            return Result;
        }
        for (TActorIterator<AActor> Actor(&InWorld); Actor; ++Actor)
        {
            TInlineComponentArray<UAkComponent*> Components(*Actor);
            for (UAkComponent* Component : Components)
            {
                if (!Component->IsRegisteredWithWwise())
                {
                    continue;
                }
                AkUInt32 Count = 0;
                const AKRESULT CountResult = Engine->Query->GetPlayingIDsFromGameObject(
                    Component->GetAkGameObjectID(), Count, nullptr);
                if (CountResult != AK_Success)
                {
                    OutErrors.Add(MakeShared<FJsonValueString>(Component->GetPathName()));
                    continue;
                }
                if (Count == 0)
                {
                    continue;
                }
                TArray<AkPlayingID> IDs;
                IDs.SetNumZeroed(Count);
                const AKRESULT ReadResult = Engine->Query->GetPlayingIDsFromGameObject(
                    Component->GetAkGameObjectID(), Count, IDs.GetData());
                if (ReadResult != AK_Success)
                {
                    OutErrors.Add(MakeShared<FJsonValueString>(Component->GetPathName()));
                    continue;
                }
                for (AkUInt32 Index = 0; Index < FMath::Min(Count, AkUInt32(IDs.Num())); ++Index)
                {
                    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
                    Item->SetStringField(TEXT("component"), Component->GetPathName());
                    Item->SetStringField(TEXT("owner"), Actor->GetActorLabel());
                    Item->SetNumberField(TEXT("id"), IDs[Index]);
                    Item->SetNumberField(
                        TEXT("event"), Engine->Query->GetEventIDFromPlayingID(IDs[Index]));
                    Result.Add(MakeShared<FJsonValueObject>(Item));
                }
            }
        }
        return Result;
    }
} // namespace

FString UAudioScenarioProbeLibrary::CaptureState(UWorld* InWorld)
{
    if (!IsInGameThread() || !IsValid(InWorld) || InWorld->WorldType != EWorldType::PIE)
    {
        return FString();
    }
    APlayerController* Controller = InWorld->GetFirstPlayerController();
    UWandererAudioSubsystem* System = InWorld->GetSubsystem<UWandererAudioSubsystem>();
    if (!IsValid(Controller) || !IsValid(System))
    {
        return FString();
    }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    FVector Position;
    FVector Front;
    FVector Right;
    Controller->GetAudioListenerPosition(Position, Front, Right);
    Root->SetArrayField(TEXT("listener"), VectorValues(Position));
    Root->SetNumberField(TEXT("time"), InWorld->GetTimeSeconds());
    Root->SetStringField(TEXT("world"), InWorld->GetPathName());
    Root->SetNumberField(TEXT("registered"), System->GetRegisteredCount());
    Root->SetNumberField(TEXT("admitted"), System->GetAdmittedCount());
    Root->SetNumberField(TEXT("protected_overflow"), System->GetProtectedOverflow());
    TArray<TSharedPtr<FJsonValue>> QueryErrors;
    Root->SetArrayField(TEXT("native_playback"), CaptureEmitters(*InWorld, QueryErrors));
    Root->SetArrayField(TEXT("query_errors"), QueryErrors);
    TArray<TSharedPtr<FJsonValue>> Rooms;
    if (FAkAudioDevice* Device = FAkAudioDevice::Get())
    {
        for (UAkRoomComponent* Room : Device->FindRoomComponentsAtLocation(Position, InWorld))
        {
            Rooms.Add(MakeShared<FJsonValueString>(Room->GetOwner()->GetActorLabel()));
        }
    }
    Root->SetArrayField(TEXT("listener_rooms"), Rooms);
    TArray<TSharedPtr<FJsonValue>> Groups;
    for (TActorIterator<AWandererWorldAudioBudget> Budget(InWorld); Budget; ++Budget)
    {
        for (const FWandererAudioResourceGroup& Group : Budget->ResourceGroups)
        {
            Groups.Add(MakeShared<FJsonValueObject>(CaptureGroup(Group, *System)));
        }
    }
    Root->SetArrayField(TEXT("groups"), Groups);
    FString Result;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
    return Result;
}
