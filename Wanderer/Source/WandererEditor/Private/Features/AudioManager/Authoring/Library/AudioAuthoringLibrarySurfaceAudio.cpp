#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"

#include "Features/AudioManager/Authoring/WorldAudioProfileRepository.h"

#include "AkAudioEvent.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "UObject/UnrealType.h"

bool UAudioAuthoringLibrary::ConfigureSurfaceAudio(UWandererAudioSourceComponent* Source,
    FDataTableRowHandle Template,
    UAkAudioEvent* Event,
    FName ResourceGroup,
    bool bEnabled)
{
    if (!IsInGameThread() || !IsValid(Source) || !GEditor || GEditor->PlayWorld)
    {
        return false;
    }
    auto* Owner = Source->GetOwner();
    if (!IsValid(Owner) || !ValidateSurfaceAudioTarget(Owner).IsEmpty())
    {
        return false;
    }
    const bool bSpline = Owner->IsA<AWandererAudioSplineEmitter>();
    if (!bSpline && !Owner->IsA<AWandererAudioAreaEmitter>())
    {
        return false;
    }
    if (!ValidateSurfaceAudioSettings(Template, Event, bSpline).IsEmpty())
    {
        return false;
    }
    auto Row = *Template.GetRow<FWandererWorldAudioProfileRow>(TEXT("Surface audio"));
    Row.Mode = bSpline ? EWandererWorldAudioMode::Spline : EWandererWorldAudioMode::Area;
    Row.PlayEvent = Event;
    Row.NightPlayEvent.Reset();
    // Caller owns the encompassing shape + profile transaction; no asynchronous work or save.
    Source->SetFlags(RF_Transactional);
    Source->Modify();
    Owner->Modify();
    Source->Profile =
        FindOrAddWorldAudioProfile(const_cast<UDataTable*>(Template.DataTable.Get()), Row, Event);
    Source->ResourceGroup = ResourceGroup;
    Source->Area = ResourceGroup;
    Source->bEnabled = bEnabled;
    Source->PostEditChange();
    Owner->MarkPackageDirty();
    return true;
}

FDataTableRowHandle UAudioAuthoringLibrary::FindOrAddWorldAudioProfile(
    UDataTable* Table, const FWandererWorldAudioProfileRow& Row, UAkAudioEvent* Event, FName InRowName)
{
    FName CreatedName;
    return FWorldAudioProfileRepository::FindOrAdd(Table, Row, Event, InRowName, CreatedName);
}

FString UAudioAuthoringLibrary::ValidateSurfaceAudioSettings(
    FDataTableRowHandle Template, UAkAudioEvent* Event, bool bSpline)
{
    if (!Event)
    {
        return TEXT("请选择 Play Event");
    }
    if (!Template.DataTable
        || Template.DataTable->GetRowStruct() != FWandererWorldAudioProfileRow::StaticStruct())
    {
        return TEXT("请选择 World Audio 参数模板表");
    }
    const auto* Original = Template.GetRow<FWandererWorldAudioProfileRow>(TEXT("Surface audio"));
    if (!Original)
    {
        return TEXT("请选择有效模板行");
    }
    auto Row = *Original;
    Row.Mode = bSpline ? EWandererWorldAudioMode::Spline : EWandererWorldAudioMode::Area;
    Row.PlayEvent = Event;
    Row.NightPlayEvent.Reset();
    return Row.Validate();
}

FString UAudioAuthoringLibrary::ValidateSurfaceAudioTarget(AActor* Target)
{
    if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
    {
        return TEXT("请先退出 PIE，在编辑器主线程制作。");
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World || (Target && (!IsValid(Target) || Target->GetWorld() != World)))
    {
        return TEXT("目标不属于当前编辑地图。");
    }
    ULevel* Level = Target ? Target->GetLevel() : World->GetCurrentLevel();
    if (!Level || FLevelUtils::IsLevelLocked(Level))
    {
        return TEXT("目标关卡已锁定，请先解锁。");
    }
    return FString();
}
