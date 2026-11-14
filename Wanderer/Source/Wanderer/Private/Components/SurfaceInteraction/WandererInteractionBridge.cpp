#include "Components/SurfaceInteraction/WandererInteractionBridge.h"

#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
    bool IsIWSWaterActor(const AActor* Actor)
    {
        return IsValid(Actor) && Actor->ActorHasTag(TEXT("TrialIWSWater"))
               && Actor->GetClass()->GetPathName().StartsWith(
                   TEXT("/Game/Blueprints/Systems/Water/BP_WaterSurface."));
    }

    bool SetIWSNumber(UObject* Object, FName Name, double Value)
    {
        FNumericProperty* Property =
            Object ? FindFProperty<FNumericProperty>(Object->GetClass(), Name) : nullptr;
        if (!Property || !Property->IsFloatingPoint())
        {
            return false;
        }
        Property->SetFloatingPointPropertyValue(
            Property->ContainerPtrToValuePtr<void>(Object), Value);
        return true;
    }

    bool CallWaterMeshEvent(
        AActor* WaterActor, ACharacter* Character, FName EventName, FName ParameterName)
    {
        if (!IsInGameThread() || !IsValid(WaterActor) || !IsValid(Character)
            || WaterActor->GetWorld() != Character->GetWorld()
            || !WaterActor->ActorHasTag(TEXT("TrialWaterInteraction")))
        {
            return false;
        }
        const FString ClassPath = WaterActor->GetClass()->GetPathName();
        if (!IsIWSWaterActor(WaterActor)
            && !(
                ClassPath
                    == TEXT(
                        "/Game/Blueprints/Systems/Water/BP_GlobalUpdater_PureWater.BP_GlobalUpdater_PureWater_C")
                || ClassPath
                       == TEXT(
                           "/Game/Blueprints/Systems/Water/BP_WaterBody_PureWater.BP_WaterBody_PureWater_C")))
        {
            return false;
        }
        UFunction* Function = WaterActor->FindFunction(EventName);
        FObjectPropertyBase* Parameter =
            Function ? FindFProperty<FObjectPropertyBase>(Function, ParameterName) : nullptr;
        USkeletalMeshComponent* Mesh = Character->GetMesh();
        if (!Function || !Parameter || !Mesh || !Mesh->IsA(Parameter->PropertyClass))
        {
            return false;
        }
        FStructOnScope Parameters(Function);
        Parameter->SetObjectPropertyValue_InContainer(Parameters.GetStructMemory(), Mesh);
        WaterActor->ProcessEvent(Function, Parameters.GetStructMemory());
        return true;
    }
} // namespace

bool UWandererInteractionBridge::ApplyIWSWaterImpact(
    AActor* WaterActor, const FVector& Location, float Strength, float Radius)
{
    if (!IsInGameThread() || !IsIWSWaterActor(WaterActor))
    {
        return false;
    }
    UFunction* Function = WaterActor->FindFunction(TEXT("ManualImpact"));
    FStructProperty* Point =
        FindFProperty<FStructProperty>(WaterActor->GetClass(), TEXT("Impact Location"));
    if (!Function || !Point || Point->Struct != TBaseStructure<FVector>::Get())
    {
        return false;
    }
    if (!FMath::IsFinite(Strength) || !FMath::IsFinite(Radius) || Location.ContainsNaN())
    {
        return false;
    }
    if (!SetIWSNumber(WaterActor, TEXT("Impact Strength"), FMath::Clamp(Strength, 0.01f, 0.18f))
        || !SetIWSNumber(WaterActor, TEXT("Impact Size"), FMath::Clamp(Radius, 2.0f, 12.0f)))
    {
        return false;
    }
    Point->CopyCompleteValue(Point->ContainerPtrToValuePtr<void>(WaterActor), &Location);
    FStructOnScope Parameters(Function);
    WaterActor->ProcessEvent(Function, Parameters.GetStructMemory());
    return true;
}

bool UWandererInteractionBridge::CreateIWSWaterSplash(
    AActor* WaterActor, const FVector& Location, float Power)
{
    if (!IsInGameThread() || !IsIWSWaterActor(WaterActor))
    {
        return false;
    }
    if (!FMath::IsFinite(Power) || Location.ContainsNaN())
    {
        return false;
    }
    UFunction* Function = WaterActor->FindFunction(TEXT("Splash"));
    FStructProperty* Point =
        Function ? FindFProperty<FStructProperty>(Function, TEXT("Location")) : nullptr;
    if (!Point || Point->Struct != TBaseStructure<FVector>::Get())
    {
        return false;
    }
    FStructOnScope Parameters(Function);
    Point->CopyCompleteValue(
        Point->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), &Location);
    for (const TPair<FName, double>& Setting :
        {TPair<FName, double>(TEXT("Power"), FMath::Clamp(Power, 0.05f, 0.3f)),
            TPair<FName, double>(TEXT("Radius"), 0.08),
            TPair<FName, double>(TEXT("Amount"), 0.08)})
    {
        FNumericProperty* Property = FindFProperty<FNumericProperty>(Function, Setting.Key);
        if (!Property || !Property->IsFloatingPoint())
        {
            return false;
        }
        Property->SetFloatingPointPropertyValue(
            Property->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), Setting.Value);
    }
    WaterActor->ProcessEvent(Function, Parameters.GetStructMemory());
    return true;
}

bool UWandererInteractionBridge::RefreshWaterCharacter(AActor* WaterActor, ACharacter* Character)
{
    if (!IsInGameThread() || !IsValid(WaterActor) || !IsValid(Character)
        || WaterActor->GetWorld() != Character->GetWorld())
    {
        return false;
    }
    if (!WaterActor->ActorHasTag(TEXT("TrialWaterInteraction")))
    {
        return false;
    }
    if (IsIWSWaterActor(WaterActor))
    {
        return true;
    }
    const FString ClassPath = WaterActor->GetClass()->GetPathName();
    if (!(ClassPath
                == TEXT(
                    "/Game/Blueprints/Systems/Water/BP_GlobalUpdater_PureWater.BP_GlobalUpdater_PureWater_C")
            || ClassPath
                   == TEXT(
                       "/Game/Blueprints/Systems/Water/BP_WaterBody_PureWater.BP_WaterBody_PureWater_C")))
    {
        return false;
    }
    FObjectPropertyBase* PlayerProperty =
        FindFProperty<FObjectPropertyBase>(WaterActor->GetClass(), TEXT("PlayerCharacter"));
    FObjectPropertyBase* CapsuleProperty =
        FindFProperty<FObjectPropertyBase>(WaterActor->GetClass(), TEXT("CharacterCapsule"));
    UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    if (!PlayerProperty || !CapsuleProperty || !Capsule
        || !Character->IsA(PlayerProperty->PropertyClass)
        || !Capsule->IsA(CapsuleProperty->PropertyClass))
    {
        return false;
    }
    PlayerProperty->SetObjectPropertyValue_InContainer(WaterActor, Character);
    CapsuleProperty->SetObjectPropertyValue_InContainer(WaterActor, Capsule);
    return true;
}

bool UWandererInteractionBridge::RegisterWaterCharacterMesh(
    AActor* WaterActor, ACharacter* Character)
{
    if (!IsInGameThread())
    {
        return false;
    }
    const FBoolProperty* ActiveProperty =
        IsValid(WaterActor)
            ? FindFProperty<FBoolProperty>(WaterActor->GetClass(),
                  IsIWSWaterActor(WaterActor) ? TEXT("Is Active") : TEXT("SetActive"))
            : nullptr;
    if (!ActiveProperty || !ActiveProperty->GetPropertyValue_InContainer(WaterActor))
    {
        return false;
    }
    return CallWaterMeshEvent(
        WaterActor, Character, TEXT("UpdateCharMeshAdd"), TEXT("AddCharMesh"));
}

bool UWandererInteractionBridge::UnregisterWaterCharacterMesh(
    AActor* WaterActor, ACharacter* Character)
{
    return CallWaterMeshEvent(
        WaterActor, Character, TEXT("UpdateCharMeshRemove"), TEXT("RemoveCharMesh"));
}

UActorComponent* UWandererInteractionBridge::CreateSnowDisplacement(ACharacter* Character)
{
    if (!IsInGameThread() || !IsValid(Character))
    {
        return nullptr;
    }
    UClass* ComponentClass = LoadClass<UActorComponent>(nullptr,
        TEXT(
            "/Game/Blueprints/Systems/Displacement/BP_InfinityDisplacementComponent.BP_InfinityDisplacementComponent_C"));
    TStrongObjectPtr<UClass> LoadedClass(ComponentClass);
    UObject* ShapeData = LoadObject<UObject>(
        nullptr, TEXT("/Game/Data/Displacement/General/DA_MannequinFoots.DA_MannequinFoots"));
    FObjectPropertyBase* ShapeProperty = ComponentClass
                                             ? FindFProperty<FObjectPropertyBase>(ComponentClass,
                                                   TEXT("DisplacementShapesDataAsset"))
                                             : nullptr;
    if (!ShapeProperty || !ShapeData || !ShapeData->IsA(ShapeProperty->PropertyClass))
    {
        UE_LOG(LogTemp,
            Warning,
            TEXT(
                "Snow interaction requires the localized displacement component and matching foot-shape data."));
        return nullptr;
    }
    if (!IsValid(Character) || Character->IsActorBeingDestroyed()
        || Character->FindComponentByClass(ComponentClass))
    {
        return nullptr;
    }
    UActorComponent* Component =
        NewObject<UActorComponent>(Character, ComponentClass, NAME_None, RF_Transient);
    ShapeProperty->SetObjectPropertyValue_InContainer(Component, ShapeData);
    Character->AddInstanceComponent(Component);
    Component->RegisterComponent();
    if (!IsValid(Character) || Character->IsActorBeingDestroyed() || !IsValid(Component)
        || !Component->IsRegistered())
    {
        if (IsValid(Component))
        {
            Component->DestroyComponent();
        }
        return nullptr;
    }
    return Component;
}
