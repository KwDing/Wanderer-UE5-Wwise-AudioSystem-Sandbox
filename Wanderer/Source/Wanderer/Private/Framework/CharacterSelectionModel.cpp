#include "Framework/CharacterSelectionModel.h"

#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Templates/UnrealTemplate.h"

#include "Data/WandererCharacterCatalog.h"
#include "Interfaces/CharacterPreviewProvider.h"
#include "Interfaces/SceneEntryProvider.h"

void UCharacterSelectionModel::Initialize(
    APlayerController* InPlayer, UWandererCharacterCatalog* InCatalog)
{
    Stop();
    PlayerWeakPtr = InPlayer;
    Catalog = InCatalog;
    bStopped = !IsValid(InPlayer);
}

void UCharacterSelectionModel::Stop()
{
    bStopped = true;
    bOpen = false;
    Portrait = nullptr;
    SelectedIndex = INDEX_NONE;
    Status = FText::GetEmpty();
    if (ICharacterPreviewProvider* Provider =
            Cast<ICharacterPreviewProvider>(ProviderWeakPtr.Get()))
    {
        Provider->SetPreviewActive(false);
    }
    ProviderWeakPtr.Reset();
    PlayerWeakPtr.Reset();
    Catalog = nullptr;
    OnChanged.Broadcast();
}

bool UCharacterSelectionModel::RegisterProvider(UObject* InProvider)
{
    if (bStopped || !IsValid(InProvider) || !Cast<ICharacterPreviewProvider>(InProvider))
    {
        return false;
    }
    const AActor* Actor = Cast<AActor>(InProvider);
    if (!PlayerWeakPtr.IsValid() || !Actor || Actor->GetOwner() != PlayerWeakPtr.Get()
        || Actor->GetWorld() != PlayerWeakPtr->GetWorld())
    {
        return false;
    }
    if (ProviderWeakPtr.IsValid() && ProviderWeakPtr.Get() != InProvider)
    {
        return false;
    }
    ProviderWeakPtr = InProvider;
    return true;
}

void UCharacterSelectionModel::UnregisterProvider(UObject* InProvider)
{
    if (!ProviderWeakPtr.HasSameIndexAndSerialNumber(TWeakObjectPtr<UObject>(InProvider)))
    {
        return;
    }
    ProviderWeakPtr.Reset();
    Portrait = nullptr;
    Status = FText::FromString(TEXT("Character preview is unavailable."));
    OnChanged.Broadcast();
}

bool UCharacterSelectionModel::Open()
{
    ISceneEntryProvider* Host = Cast<ISceneEntryProvider>(PlayerWeakPtr.Get());
    return !bStopped && Host && !Host->IsEntryLoading() && ProviderWeakPtr.IsValid()
           && Host->SetCharacterSelectionState(true);
}

bool UCharacterSelectionModel::Close()
{
    ISceneEntryProvider* Host = Cast<ISceneEntryProvider>(PlayerWeakPtr.Get());
    return !bStopped && Host && !Host->IsEntryLoading() && Host->SetCharacterSelectionState(false);
}

void UCharacterSelectionModel::SetOpen(bool bInOpen)
{
    if (bStopped || bOpen == bInOpen)
    {
        return;
    }
    bOpen = bInOpen;
    Portrait = nullptr;
    Status = FText::GetEmpty();
    if (bOpen)
    {
        const int32 Current = GetCurrentIndex();
        Preview(Current == INDEX_NONE ? 0 : Current);
        return;
    }
    if (ICharacterPreviewProvider* Provider =
            Cast<ICharacterPreviewProvider>(ProviderWeakPtr.Get()))
    {
        Provider->SetPreviewActive(false);
    }
    OnChanged.Broadcast();
}

bool UCharacterSelectionModel::Preview(int32 InIndex)
{
    if (!CanContinue())
    {
        return false;
    }
    SelectedIndex = InIndex;
    Portrait = nullptr;
    const FWandererCharacterEntry* Entry = FindOption(InIndex);
    ICharacterPreviewProvider* Provider = Cast<ICharacterPreviewProvider>(ProviderWeakPtr.Get());
    const TWeakObjectPtr<UObject> PreviousProvider = ProviderWeakPtr;
    UTextureRenderTarget2D* Result =
        Entry && Provider ? Provider->ShowPreview(Entry->CharacterClass, Entry->PortraitAnimation)
                          : nullptr;
    if (!CanContinue() || ProviderWeakPtr != PreviousProvider)
    {
        if (ICharacterPreviewProvider* Previous =
                Cast<ICharacterPreviewProvider>(PreviousProvider.Get()))
        {
            Previous->SetPreviewActive(false);
        }
        return false;
    }
    Portrait = IsValid(Result) ? Result : nullptr;
    Status = FText::FromString(
        Portrait ? TEXT("Esc to return")
                 : TEXT("This character has no valid preview. Check its catalog entry."));
    OnChanged.Broadcast();
    return Portrait != nullptr;
}

bool UCharacterSelectionModel::Choose(int32 InIndex)
{
    const FWandererCharacterEntry* Entry = FindOption(InIndex);
    if (!CanContinue() || bChoosing || !Entry || !IsOptionEnabled(InIndex)
        || !PlayerWeakPtr->HasAuthority())
    {
        return false;
    }
    TGuardValue<bool> Guard(bChoosing, true);
    const bool bSuccess = SwitchPawn(Entry->CharacterClass);
    if (!bSuccess && !bStopped)
    {
        Status =
            FText::FromString(TEXT("Could not switch character. Check the current pawn and log."));
        OnChanged.Broadcast();
    }
    return bSuccess;
}

bool UCharacterSelectionModel::AreResourcesReady() const
{
    const ICharacterPreviewProvider* Provider =
        Cast<ICharacterPreviewProvider>(ProviderWeakPtr.Get());
    if (bStopped || !Catalog || !PlayerWeakPtr.IsValid() || !Provider)
    {
        return false;
    }
    for (const FWandererCharacterEntry& Entry : Catalog->Characters)
    {
        if (!Provider->IsClassReady(Entry.CharacterClass))
        {
            return false;
        }
    }
    return Provider->IsPawnReady(PlayerWeakPtr->GetPawn());
}

int32 UCharacterSelectionModel::GetCurrentIndex() const
{
    const APawn* Pawn = PlayerWeakPtr.IsValid() ? PlayerWeakPtr->GetPawn() : nullptr;
    if (!Catalog || !Pawn)
    {
        return INDEX_NONE;
    }
    return Catalog->Characters.IndexOfByPredicate(
        [Pawn](const FWandererCharacterEntry& InEntry)
        {
            return Pawn->GetClass() == InEntry.CharacterClass;
        });
}

int32 UCharacterSelectionModel::GetOptionCount() const
{
    return Catalog ? Catalog->Characters.Num() : 0;
}

FText UCharacterSelectionModel::GetOptionName(int32 InIndex) const
{
    const FWandererCharacterEntry* Entry = FindOption(InIndex);
    return Entry ? Entry->DisplayName : FText::FromString(TEXT("Select a character"));
}

bool UCharacterSelectionModel::IsOptionEnabled(int32 InIndex) const
{
    const FWandererCharacterEntry* Entry = FindOption(InIndex);
    return Entry && Entry->CharacterClass && !Entry->ID.IsNone();
}

bool UCharacterSelectionModel::IsOpen() const
{
    return bOpen && !bStopped;
}
bool UCharacterSelectionModel::CanChoose() const
{
    return CanContinue() && Portrait && IsOptionEnabled(SelectedIndex) && !bChoosing;
}
int32 UCharacterSelectionModel::GetSelectedIndex() const
{
    return SelectedIndex;
}
FText UCharacterSelectionModel::GetStatus() const
{
    return Status;
}
UTextureRenderTarget2D* UCharacterSelectionModel::GetPortrait() const
{
    return !bStopped && bOpen && ProviderWeakPtr.IsValid() ? Portrait.Get() : nullptr;
}

const FWandererCharacterEntry* UCharacterSelectionModel::FindOption(int32 InIndex) const
{
    return Catalog && Catalog->Characters.IsValidIndex(InIndex) ? &Catalog->Characters[InIndex]
                                                                : nullptr;
}

bool UCharacterSelectionModel::CanContinue() const
{
    return !bStopped && bOpen && PlayerWeakPtr.IsValid() && PlayerWeakPtr->GetWorld()
           && ProviderWeakPtr.IsValid();
}
