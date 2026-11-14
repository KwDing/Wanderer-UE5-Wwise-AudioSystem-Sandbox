#include "Tests/CharacterFlow/CharacterFlowTestActors.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/SpectatorPawn.h"
#include "Framework/CharacterSelectionModel.h"
bool ACharacterFlowTestHost::IsEntryLoading() const
{
    return bLoading;
}
bool ACharacterFlowTestHost::AreEntryCharactersReady() const
{
    return false;
}
void ACharacterFlowTestHost::SetEntryMapScanEnabled(bool bInEnabled)
{
    bMapScanEnabled = bInEnabled;
}
void ACharacterFlowTestHost::StartEntryMapScan()
{
    bMapScanEnabled = true;
}
bool ACharacterFlowTestHost::IsEntryMapReady() const
{
    return true;
}
bool ACharacterFlowTestHost::SetEntryLoadingState(bool bInLoading)
{
    bLoading = bInLoading;
    return true;
}
bool ACharacterFlowTestHost::SetCharacterSelectionState(bool bInOpen)
{
    if (!Model || (!bInOpen && bRejectClose))
    {
        return false;
    }
    if (!bInOpen && bStopOnClose)
    {
        Model->Stop();
    }
    if (!bInOpen && TransferTo)
    {
        TransferTo->Possess(TransferPawn ? TransferPawn.Get() : GetPawn());
    }
    Model->SetOpen(bInOpen);
    return true;
}
void ACharacterFlowTestHost::OnPossess(APawn* InPawn)
{
    if (bRejectCandidate && InPawn && InPawn->IsA<ASpectatorPawn>())
    {
        return;
    }
    Super::OnPossess(InPawn);
}
UTextureRenderTarget2D* ACharacterPreviewTestActor::ShowPreview(
    UClass* InClass, UAnimationAsset* InAnimation)
{
    if (bStopDuringPreview && ModelWeakPtr.IsValid())
    {
        ModelWeakPtr->Stop();
    }
    if (!Portrait)
    {
        Portrait = NewObject<UTextureRenderTarget2D>(this);
    }
    bActive = true;
    return Portrait;
}
void ACharacterPreviewTestActor::SetPreviewActive(bool bInActive)
{
    bActive = bInActive;
}
APawn* ACharacterPreviewTestActor::SpawnCandidate(UClass* InClass, const FTransform& InTransform)
{
    if (bStopDuringSpawn && ModelWeakPtr.IsValid())
    {
        ModelWeakPtr->Stop();
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return GetWorld()->SpawnActor<APawn>(InClass, InTransform, Params);
}
bool ACharacterPreviewTestActor::IsClassReady(UClass* InClass) const
{
    return InClass != nullptr;
}
bool ACharacterPreviewTestActor::IsPawnReady(const APawn* InPawn) const
{
    return IsValid(InPawn);
}
