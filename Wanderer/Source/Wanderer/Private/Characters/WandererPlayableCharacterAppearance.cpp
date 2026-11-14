#include "Characters/WandererPlayableCharacter.h"

#include "Components/SkeletalMeshComponent.h"

void AWandererPlayableCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    for (USkeletalMeshComponent* Component : AppearanceComponents)
    {
        if (IsValid(Component))
        {
            Component->DestroyComponent();
        }
    }
    AppearanceComponents.Reset();
    for (const FWandererAppearancePart& Part : AppearanceParts)
    {
        if (!Part.Mesh)
        {
            continue;
        }
        USkeletalMeshComponent* Component =
            NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
        AppearanceComponents.Add(Component);
        Component->SetupAttachment(GetMesh());
        Component->SetRelativeTransform(Part.RelativeTransform);
        Component->SetSkeletalMeshAsset(Part.Mesh);
        for (int32 Index = 0; Index < Part.Materials.Num(); ++Index)
        {
            Component->SetMaterial(Index, Part.Materials[Index]);
        }
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetLeaderPoseComponent(GetMesh());
        Component->RegisterComponent();
    }
    ConfigureCharacterLightingChannels();
}

void AWandererPlayableCharacter::ConfigureCharacterLightingChannels()
{
    // Retain authored scene illumination and body fill; camera fill must never hit the body.
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(this);
    for (USkeletalMeshComponent* CharacterMesh : Meshes)
    {
        CharacterMesh->SetLightingChannels(true, true, false);
    }
}
