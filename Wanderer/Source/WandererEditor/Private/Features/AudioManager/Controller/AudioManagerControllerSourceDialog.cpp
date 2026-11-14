#include "Features/AudioManager/Controller/AudioManagerController.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#include "AkSpatialAudioVolume.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Features/AudioManager/AudioManagerInteraction.h"
#include "Features/AudioManager/AudioSourceAuthoringOperation.h"
#include "Features/AudioManager/Authoring/WorldAudioProfileUtils.h"
#include "Features/AudioManager/InteriorFitGeometryImportOperation.h"

void FAudioManagerController::OpenSourceDialog(EAudioSourceAction InAction)
{
    if (!CanEditRoomSources())
    {
        return;
    }
    if (State.ActivePage == 3)
    {
        return InAction == EAudioSourceAction::Create ? CreateBuildingDialog()
                                                      : EditBuildingSelection();
    }
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (Entry->Kind == TEXT("建筑底噪"))
        {
            return FinishRoomAction(TEXT(
                "建筑共用记录在音频对象页只读；请到“建筑底噪”页修改。Room 入口只编辑自身附加声。"));
        }
    }
    TSharedRef<FSourceDialogDraft> Draft = MakeShared<FSourceDialogDraft>();
    Draft->Data = MakeShared<FStructOnScope>(FWandererWorldAudioProfileRow::StaticStruct());
    Draft->Request.Action = InAction;
    Draft->Request.World = GetObservedWorld();
    Draft->Request.TablePath = State.RoomProfileTablePath;
    CollectSourceTargets(*Draft);
    if (InAction == EAudioSourceAction::Create
        && Draft->Request.Targets.ContainsByPredicate(
            [](const TWeakObjectPtr<AActor>& InTarget)
            {
                return FInteriorFitGeometryImportOperation::IsSource(InTarget.Get());
            }))
    {
        if (Draft->Request.Targets.ContainsByPredicate(
                [](const TWeakObjectPtr<AActor>& InTarget)
                {
                    return !FInteriorFitGeometryImportOperation::IsSource(InTarget.Get());
                }))
        {
            return FinishRoomAction(TEXT("请单独选择需要配置声音的 IF 几何。"));
        }
        Draft->Request.Action = EAudioSourceAction::ImportGeometry;
    }
    if ((InAction == EAudioSourceAction::Create && Draft->Request.Targets.IsEmpty())
        || (InAction == EAudioSourceAction::Edit && Draft->Request.Sources.IsEmpty()))
    {
        return FinishRoomAction(
            InAction == EAudioSourceAction::Create
                ? TEXT("请先选择创建位置的对象。")
                : TEXT("所选对象未配置自身声源；Room 特殊 Event 为空时仅播放建筑底噪。可在 Room "
                       "底噪设置中选择 Event 并应用。"));
    }
    InitializeSourceProfile(*Draft);
    if (const TSharedPtr<IAudioManagerInteraction> View = InteractionWeakPtr.Pin())
    {
        View->ShowSourceDialog(Draft);
    }
}

FString FAudioManagerController::GetSourceProfileName(const FAudioSourceDialogDraft& InDraft)
{
    if (InDraft.bCustomProfileRowName)
    {
        return InDraft.ProfileRowName;
    }
    const FWandererWorldAudioProfileRow* Row =
        reinterpret_cast<const FWandererWorldAudioProfileRow*>(InDraft.Data->GetStructMemory());
    return WorldAudioProfileUtils::SuggestRowName(Row->PlayEvent.GetAssetName());
}

bool FAudioManagerController::ApplySourceDraft(TSharedRef<FAudioSourceDialogDraft> InDraft)
{
    if (!CanEditRoomSources() || !InDraft->Request.World.IsValid()
        || GetObservedWorld() != InDraft->Request.World.Get())
    {
        InDraft->Result = FText::FromString(TEXT("管理器或编辑世界已失效。"));
        return false;
    }
    InDraft->Request.Profile =
        *reinterpret_cast<FWandererWorldAudioProfileRow*>(InDraft->Data->GetStructMemory());
    const FString Name = GetSourceProfileName(*InDraft);
    const FString Error = WorldAudioProfileUtils::ValidateRowName(Name);
    if (!Error.IsEmpty())
    {
        InDraft->Result = FText::FromString(Error);
        return false;
    }
    InDraft->Request.ProfileRowName = FName(*Name);
    const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
    InDraft->Request.CanContinue = [ControllerWeakPtr]
    {
        const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
        return Controller && Controller->CanEditRoomSources();
    };
    const FAudioSourceResult Result = FAudioSourceAuthoringOperation::Apply(InDraft->Request);
    InDraft->Result = FText::FromString(Result.Summary());
    FinishRoomAction(Result.Summary());
    return Result.Applied > 0;
}

void FAudioManagerController::UpdateImportedGeometry(EGeometryImportTransform InTransformPolicy)
{
    if (!CanEditRoomSources())
    {
        return;
    }
    TArray<AActor*> Selected;
    GEditor->GetSelectedActors()->GetSelectedObjects(Selected);
    if (Selected.Num() != 2)
    {
        return FinishRoomAction(TEXT("请在 Outliner 明确选择一个 IF "
                                     "来源和一个目标声源。河网增删不自动处理，请逐条确认配对。"));
    }
    if (!FInteriorFitGeometryImportOperation::IsSource(Selected[0]))
    {
        Swap(Selected[0], Selected[1]);
    }
    const TWeakPtr<FAudioManagerController> ControllerWeakPtr = AsShared();
    const FAudioSourceResult Result = FInteriorFitGeometryImportOperation::Update(
        Selected[0],
        Selected[1],
        InTransformPolicy,
        [ControllerWeakPtr]
        {
            const TSharedPtr<FAudioManagerController> Controller = ControllerWeakPtr.Pin();
            return Controller && Controller->CanEditRoomSources();
        });
    FinishRoomAction(Result.Summary());
}

void FAudioManagerController::CollectSourceTargets(FAudioSourceDialogDraft& InOutDraft)
{
    if (InOutDraft.Request.Action == EAudioSourceAction::Create)
    {
        TArray<AActor*> OutlinerActors;
        GEditor->GetSelectedActors()->GetSelectedObjects(OutlinerActors);
        const UWorld* World = InOutDraft.Request.World.Get();
        const bool bOnlyFitSources = !OutlinerActors.IsEmpty()
            && !OutlinerActors.ContainsByPredicate(
                [World](const AActor* InActor)
                {
                    return !FInteriorFitGeometryImportOperation::IsSource(InActor)
                        || InActor->GetWorld() != World;
                });
        if (bOnlyFitSources)
        {
            // The fit command selects its new results while the audio list may retain an old row.
            for (AActor* Actor : OutlinerActors)
            {
                InOutDraft.Request.Targets.Add(Actor);
            }
            return;
        }
    }
    TArray<TWeakObjectPtr<AActor>> ActorRows;
    for (const FEntryPtr& Entry : SelectedEntries)
    {
        if (Entry->ActorWeakPtr.IsValid())
        {
            InOutDraft.Request.Targets.AddUnique(Entry->ActorWeakPtr);
        }
        if (Entry->SourceWeakPtr.IsValid())
        {
            InOutDraft.Request.Sources.AddUnique(Entry->SourceWeakPtr);
        }
        else if (Entry->ActorWeakPtr.IsValid())
        {
            ActorRows.AddUnique(Entry->ActorWeakPtr);
        }
    }
    if (InOutDraft.Request.Action == EAudioSourceAction::Create
        && InOutDraft.Request.Targets.IsEmpty())
    {
        TArray<AActor*> Selected;
        GEditor->GetSelectedActors()->GetSelectedObjects(Selected);
        for (AActor* Actor : Selected)
        {
            if (Actor->GetWorld() == GetObservedWorld())
            {
                InOutDraft.Request.Targets.AddUnique(Actor);
            }
        }
    }
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& Source :
         FAudioSourceAuthoringOperation::GatherSources(GetObservedWorld(), ActorRows))
    {
        InOutDraft.Request.Sources.AddUnique(Source);
    }
}

void FAudioManagerController::InitializeSourceProfile(FAudioSourceDialogDraft& InOutDraft)
{
    FWandererWorldAudioProfileRow* Row =
        reinterpret_cast<FWandererWorldAudioProfileRow*>(InOutDraft.Data->GetStructMemory());
    if (InOutDraft.Request.Action == EAudioSourceAction::Edit)
    {
        if (const UWandererAudioSourceComponent* Source = InOutDraft.Request.Sources[0].Get())
        {
            if (const FWandererWorldAudioProfileRow* Profile = Source->GetProfile())
            {
                *Row = *Profile;
            }
            else if (Source->GetOwner()->IsA<AWandererAudioAreaEmitter>())
            {
                Row->Mode = EWandererWorldAudioMode::Area;
            }
            else if (Source->GetOwner()->IsA<AWandererAudioSplineEmitter>())
            {
                Row->Mode = EWandererWorldAudioMode::Spline;
            }
        }
    }
    else
    {
        if (InOutDraft.Request.Targets.Num() == 1)
        {
            const AActor* Target = InOutDraft.Request.Targets[0].Get();
            if (Target && Target->IsA<AWandererAudioAreaEmitter>())
            {
                Row->Mode = EWandererWorldAudioMode::Area;
                return;
            }
            if (Target && Target->IsA<AWandererAudioSplineEmitter>())
            {
                Row->Mode = EWandererWorldAudioMode::Spline;
                return;
            }
        }
        bool bAllRooms = true;
        for (const TWeakObjectPtr<AActor>& Target : InOutDraft.Request.Targets)
        {
            bAllRooms &= Target.IsValid() && Target->IsA<AAkSpatialAudioVolume>();
        }
        Row->Mode = bAllRooms ? EWandererWorldAudioMode::Room : EWandererWorldAudioMode::Point;
    }
}
