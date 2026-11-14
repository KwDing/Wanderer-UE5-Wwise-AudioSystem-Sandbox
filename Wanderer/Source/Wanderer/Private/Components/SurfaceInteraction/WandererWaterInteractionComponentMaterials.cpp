#include "Components/SurfaceInteraction/WandererWaterInteractionComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Data/SurfaceInteractionData.h"

void UWandererWaterInteractionComponent::WriteBodyParameters(const FWandererIWSReceiver& InReceiver,
    UMaterialInstanceDynamic& InMaterial,
    const UStaticMeshComponent& InSurface)
{
    const float Height = InSurface.GetComponentLocation().Z;
    InMaterial.SetScalarParameterValue(TEXT("WaterHeight"), Height);
    InMaterial.SetScalarParameterValue(TEXT("SectionCount"), InReceiver.BodySections.Num());
    InMaterial.SetScalarParameterValue(TEXT("BodyMotion"), InReceiver.BodyMotion);
    InMaterial.SetScalarParameterValue(TEXT("EntryEnergy"), InReceiver.EntryEnergy);
    const FBox WaterBounds = InSurface.Bounds.GetBox();
    InMaterial.SetVectorParameterValue(TEXT("ReceiverBounds"),
        FLinearColor(WaterBounds.GetCenter().X,
            WaterBounds.GetCenter().Y,
            WaterBounds.GetExtent().X,
            WaterBounds.GetExtent().Y));
    InMaterial.SetScalarParameterValue(TEXT("ReceiverKind"),
        InSurface.ComponentHasTag(TEXT("IWSFrontPool"))  ? 1.0f
        : InSurface.ComponentHasTag(TEXT("IWSRearPool")) ? 2.0f
                                                         : 0.0f);
    for (int32 Index = 0; Index < InReceiver.BodySections.Num(); ++Index)
    {
        const FWandererWaterBodySection& Section = InReceiver.BodySections[Index];
        InMaterial.SetVectorParameterValue(BodyStartParameters[Index],
            FLinearColor(Section.Start.X, Section.Start.Y, Section.Start.Z));
        InMaterial.SetVectorParameterValue(
            BodyEndParameters[Index], FLinearColor(Section.End.X, Section.End.Y, Section.End.Z));
        InMaterial.SetScalarParameterValue(BodyRadiusParameters[Index], Section.Radius);
    }
}

void UWandererWaterInteractionComponent::DrawBodyWaterline(
    FWandererIWSReceiver& InOutReceiver, UStaticMeshComponent& InSurface)
{
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    if (!CanUpdate() || !Solver || Solver->IsActorBeingDestroyed() || !IsValid(&InSurface))
    {
        return;
    }
    const float Height = InSurface.GetComponentLocation().Z;
    UStaticMeshComponent* Waterline = InOutReceiver.WaterlineWeakPtr.Get();
    if (!Waterline)
    {
        Waterline = CreateBodyWaterline(*Solver);
        InOutReceiver.WaterlineWeakPtr = Waterline;
    }
    if (!Waterline || !CanUpdate())
    {
        return;
    }
    UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Waterline->GetMaterial(0));
    if (!Material)
    {
        ReleaseBodyWaterline(InOutReceiver);
        return;
    }
    FBox Bounds = UE::Wanderer::Water::GetSectionBounds(InOutReceiver.BodySections);
    if (Bounds.IsValid)
    {
        Bounds = Bounds.ExpandBy(UE::Wanderer::SurfaceInteraction::WaterlineBoundsMargin);
    }
    UpdateWaveParameters(InOutReceiver, *Material, Bounds);
    Waterline->SetWorldLocation(FVector(Bounds.GetCenter().X,
        Bounds.GetCenter().Y,
        Height + UE::Wanderer::SurfaceInteraction::WaterlineHeightOffset));
    Waterline->SetWorldScale3D(
        FVector(Bounds.GetSize().X / UE::Wanderer::SurfaceInteraction::WaterlineMeshSize,
            Bounds.GetSize().Y / UE::Wanderer::SurfaceInteraction::WaterlineMeshSize,
            1.0f));
    WriteBodyParameters(InOutReceiver, *Material, InSurface);
}

bool UWandererWaterInteractionComponent::BindSurfaceMaterial(
    FWandererIWSReceiver& InOutReceiver, UMaterialInstanceDynamic& InOutSolverMaterial)
{
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    if (!Surface || !InOutReceiver.SurfaceTemplate)
    {
        return false;
    }
    UTexture* Normals = InOutSolverMaterial.K2_GetTextureParameterValue(TEXT("HeightfieldNormal"));
    const float Distance =
        InOutSolverMaterial.K2_GetScalarParameterValue(TEXT("SimulationDistance"));
    if (!Normals || !FMath::IsFinite(Distance) || Distance <= 0.0f)
    {
        return false;
    }
    if (!InOutReceiver.SurfaceMaterial)
    {
        InOutReceiver.SurfaceMaterial =
            UMaterialInstanceDynamic::Create(InOutReceiver.SurfaceTemplate, Surface);
        if (!InOutReceiver.SurfaceMaterial)
        {
            return false;
        }
    }
    // The vendor reconstructs its own material at BeginPlay. Borrow only simulation
    // outputs; never replace the receiver's authored optical material with that MID.
    InOutReceiver.SurfaceMaterial->SetTextureParameterValue(TEXT("HeightfieldNormal"), Normals);
    InOutReceiver.SurfaceMaterial->SetVectorParameterValue(TEXT("CaptureLocation"),
        InOutSolverMaterial.K2_GetVectorParameterValue(TEXT("CaptureLocation")));
    InOutReceiver.SurfaceMaterial->SetScalarParameterValue(TEXT("SimulationDistance"), Distance);
    InOutReceiver.SurfaceMaterial->SetScalarParameterValue(
        TEXT("CalcWavesNormalPower"), UE::Wanderer::SurfaceInteraction::WaterNormalPower);
    if (Surface->GetMaterial(0) != InOutReceiver.SurfaceMaterial)
    {
        Surface->SetMaterial(0, InOutReceiver.SurfaceMaterial);
    }
    return true;
}

void UWandererWaterInteractionComponent::ReleaseSurfaceMaterial(FWandererIWSReceiver& InOutReceiver)
{
    if (UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get())
    {
        if (InOutReceiver.SurfaceMaterial
            && Surface->GetMaterial(0) == InOutReceiver.SurfaceMaterial)
        {
            Surface->SetMaterial(0, InOutReceiver.SurfaceTemplate);
        }
    }
    InOutReceiver.SurfaceMaterial = nullptr;
}

void UWandererWaterInteractionComponent::UpdateBodyWaterline(
    FWandererIWSReceiver& InOutReceiver, ACharacter& InOutCharacter)
{
    USkeletalMeshComponent* Mesh = InOutCharacter.GetMesh();
    UStaticMeshComponent* Surface = InOutReceiver.SurfaceWeakPtr.Get();
    AActor* Solver = InOutReceiver.SolverWeakPtr.Get();
    if (!Mesh || !Surface || !Solver)
    {
        ReleaseBodyWaterline(InOutReceiver);
        return;
    }
    const float Height = Surface->GetComponentLocation().Z;
    InOutReceiver.Waves.RemoveAll(
        [Now = GetWorld()->GetTimeSeconds()](const FWandererWaterWave& Wave)
        {
            return Now - Wave.StartTime > UE::Wanderer::SurfaceInteraction::ContactWaveLifetime;
        });
    if (InOutReceiver.bWasWet)
    {
        UE::Wanderer::Water::SampleBodySections(*Mesh, Height, InOutReceiver.BodySections);
    }
    else
    {
        InOutReceiver.BodySections.Reset();
    }
    const bool bHasBodyContact = !InOutReceiver.BodySections.IsEmpty();
    if (!bHasBodyContact && InOutReceiver.Waves.IsEmpty())
    {
        ReleaseBodyWaterline(InOutReceiver);
        return;
    }
    if (bHasBodyContact && !InOutReceiver.bHadBodyContact
        && InOutReceiver.PendingFallSpeed > UE::Wanderer::SurfaceInteraction::MinimumEntryFallSpeed)
    {
        EmitEntrySplash(InOutReceiver);
    }
    if (!CanUpdate() || !InOutReceiver.SurfaceWeakPtr.IsValid()
        || !InOutReceiver.SolverWeakPtr.IsValid())
    {
        return;
    }
    InOutReceiver.bHadBodyContact = bHasBodyContact;
    InOutReceiver.PendingFallSpeed = 0.0f;
    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    InOutReceiver.EntryEnergy = FMath::FInterpTo(InOutReceiver.EntryEnergy,
        0.0f,
        DeltaTime,
        UE::Wanderer::SurfaceInteraction::EntryEnergyDecayRate);
    const float Motion =
        FMath::Clamp(InOutCharacter.GetVelocity().Size2D()
                             / UE::Wanderer::SurfaceInteraction::MotionReferenceSpeed
                         + InOutReceiver.AnimatedMotion,
            0.0f,
            1.0f);
    InOutReceiver.BodyMotion = FMath::FInterpTo(InOutReceiver.BodyMotion,
        Motion,
        DeltaTime,
        UE::Wanderer::SurfaceInteraction::BodyMotionResponseRate);
    DrawBodyWaterline(InOutReceiver, *Surface);
}

void UWandererWaterInteractionComponent::UpdateWaveParameters(
    FWandererIWSReceiver& InOutReceiver, UMaterialInstanceDynamic& InOutMaterial, FBox& InOutBounds)
{
    InOutMaterial.SetScalarParameterValue(TEXT("WaveCount"), InOutReceiver.Waves.Num());
    for (int32 Index = 0; Index < InOutReceiver.Waves.Num(); ++Index)
    {
        const FWandererWaterWave& Wave = InOutReceiver.Waves[Index];
        const float Age = GetWorld()->GetTimeSeconds() - Wave.StartTime;
        InOutMaterial.SetVectorParameterValue(WavePointParameters[Index],
            FLinearColor(Wave.Position.X, Wave.Position.Y, Wave.Position.Z, 1.0f));
        InOutMaterial.SetVectorParameterValue(
            WaveDataParameters[Index], FLinearColor(Age, Wave.Strength, Wave.Radius, 1.0f));
        const FVector Extent(Wave.Radius + Age * UE::Wanderer::SurfaceInteraction::ContactWaveSpeed
                                 + UE::Wanderer::SurfaceInteraction::WaveBoundsMargin,
            Wave.Radius + Age * UE::Wanderer::SurfaceInteraction::ContactWaveSpeed
                + UE::Wanderer::SurfaceInteraction::WaveBoundsMargin,
            0.0f);
        InOutBounds += Wave.Position - Extent;
        InOutBounds += Wave.Position + Extent;
    }
}

void UWandererWaterInteractionComponent::AddWaterWave(
    FWandererIWSReceiver& InOutReceiver, const FVector& InPoint, float InStrength, float InRadius)
{
    if (InPoint.ContainsNaN() || !FMath::IsFinite(InStrength) || !FMath::IsFinite(InRadius))
    {
        return;
    }
    if (InOutReceiver.Waves.Num() >= UE::Wanderer::SurfaceInteraction::MaximumContactWaves)
    {
        InOutReceiver.Waves.RemoveAt(0, 1, EAllowShrinking::No);
    }
    FWandererWaterWave& Wave = InOutReceiver.Waves.AddDefaulted_GetRef();
    Wave.Position = InPoint;
    Wave.StartTime = GetWorld()->GetTimeSeconds();
    Wave.Strength = FMath::Clamp(InStrength, 0.0f, 1.0f);
    Wave.Radius = FMath::Clamp(InRadius,
        UE::Wanderer::SurfaceInteraction::MinimumWaveRadius,
        UE::Wanderer::SurfaceInteraction::MaximumWaveRadius);
}

UStaticMeshComponent* UWandererWaterInteractionComponent::CreateBodyWaterline(AActor& InOutSolver)
{
    if (!WaterlineMesh || !WaterlineTemplate)
    {
        return nullptr;
    }
    UStaticMeshComponent* Waterline =
        NewObject<UStaticMeshComponent>(&InOutSolver, NAME_None, RF_Transient);
    Waterline->SetMobility(EComponentMobility::Movable);
    Waterline->SetStaticMesh(WaterlineMesh);
    Waterline->SetMaterial(0, WaterlineTemplate);
    Waterline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Waterline->SetGenerateOverlapEvents(false);
    Waterline->SetCastShadow(false);
    Waterline->SetTranslucentSortPriority(1);
    Waterline->SetCanEverAffectNavigation(false);
    Waterline->RegisterComponent();
    if (!CanUpdate() || !Waterline->IsRegistered() || !Waterline->CreateDynamicMaterialInstance(0))
    {
        Waterline->DestroyComponent();
        return nullptr;
    }
    return Waterline;
}

void UWandererWaterInteractionComponent::ReleaseBodyWaterline(FWandererIWSReceiver& InOutReceiver)
{
    UStaticMeshComponent* Waterline = InOutReceiver.WaterlineWeakPtr.Get();
    InOutReceiver.WaterlineWeakPtr.Reset();
    if (Waterline)
    {
        Waterline->DestroyComponent();
    }
    InOutReceiver.BodySections.Reset();
    InOutReceiver.Waves.Reset();
    InOutReceiver.bHadBodyContact = false;
    InOutReceiver.BodyMotion = 0.0f;
    InOutReceiver.AnimatedMotion = 0.0f;
    InOutReceiver.EntryEnergy = 0.0f;
}
