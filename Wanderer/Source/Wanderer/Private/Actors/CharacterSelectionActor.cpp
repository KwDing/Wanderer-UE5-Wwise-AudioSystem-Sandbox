#include "Actors/CharacterSelectionActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "Actors/WandererPortraitActor.h"
#include "Framework/CharacterSelectionModel.h"
#include "Interfaces/CharacterPresentationProvider.h"

UTextureRenderTarget2D* ACharacterSelectionActor::ShowPreview(
    UClass* InClass, UAnimationAsset* InAnimation)
{
    if (!IsValid(PortraitActor))
    {
        constexpr double PreviewAltitude = 1000000.0;
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.ObjectFlags |= RF_Transient;
        PortraitActor = GetWorld()->SpawnActor<AWandererPortraitActor>(
            FVector(0.0, 0.0, PreviewAltitude), FRotator::ZeroRotator, Params);
    }
    return IsValid(PortraitActor) ? PortraitActor->ShowPreview(InClass, InAnimation) : nullptr;
}

void ACharacterSelectionActor::SetPreviewActive(bool bInActive)
{
    if (IsValid(PortraitActor))
    {
        PortraitActor->SetPreviewActive(bInActive);
    }
}

APawn* ACharacterSelectionActor::SpawnCandidate(UClass* InClass, const FTransform& InTransform)
{
    if (!InClass || !InClass->IsChildOf(APawn::StaticClass()) || !GetWorld())
    {
        return nullptr;
    }
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return GetWorld()->SpawnActor<APawn>(InClass, InTransform, Parameters);
}

bool ACharacterSelectionActor::IsClassReady(UClass* InClass) const
{
    const ICharacterPresentationProvider* Provider =
        InClass ? Cast<ICharacterPresentationProvider>(InClass->GetDefaultObject()) : nullptr;
    const USkeletalMeshComponent* Mesh = Provider ? Provider->GetPresentationMesh() : nullptr;
    const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    return Asset && !Asset->HasPendingInitOrStreaming();
}

bool ACharacterSelectionActor::IsPawnReady(const APawn* InPawn) const
{
    const ICharacterPresentationProvider* Provider = Cast<ICharacterPresentationProvider>(InPawn);
    const USkeletalMeshComponent* Mesh = Provider ? Provider->GetPresentationMesh() : nullptr;
    return Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetAnimInstance();
}

bool ACharacterSelectionActor::BindSelectionModel(UCharacterSelectionModel* InModel)
{
    if ((ModelWeakPtr.IsValid() && ModelWeakPtr.Get() != InModel) || !IsValid(InModel)
        || !InModel->RegisterProvider(this))
    {
        return false;
    }
    ModelWeakPtr = InModel;
    return true;
}

void ACharacterSelectionActor::EndPlay(const EEndPlayReason::Type InReason)
{
    SetPreviewActive(false);
    if (ModelWeakPtr.IsValid())
    {
        ModelWeakPtr->UnregisterProvider(this);
    }
    ModelWeakPtr.Reset();
    if (IsValid(PortraitActor))
    {
        PortraitActor->Destroy();
    }
    PortraitActor = nullptr;
    Super::EndPlay(InReason);
}
