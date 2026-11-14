#include "Systems/WandererGrassProjectionSettings.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PhysicsVolume.h"
#include "LandscapeProxy.h"

#include "Data/PCGPointData.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGPin.h"

#include "Data/SurfaceInteractionData.h"

namespace
{
    // Executor owns this context and its GC-tracked OutputData. No callbacks or retained world
    // pointers.
    struct FGrassProjectionContext : FPCGContext
    {
        int32 InputIndex = 0;
        int32 PointIndex = 0;
        TArray<FBox> WaterBounds;
        bool bInitialized = false;
        virtual bool TimeSliceIsEnabled() const override
        {
            return true;
        }
    };

    bool ProjectGrass(UWorld& World,
        const FCollisionQueryParams& Query,
        const TArray<FBox>& WaterBounds,
        const UWandererGrassProjectionSettings& Settings,
        FPCGPoint& Point)
    {
        FVector Position = Point.Transform.GetLocation();
        const double Radius = Settings.ClearanceRadius * Point.Transform.GetScale3D().GetAbsMax();
        for (const FBox& Bounds : WaterBounds)
        {
            if (Position.X >= Bounds.Min.X - Radius && Position.X <= Bounds.Max.X + Radius
                && Position.Y >= Bounds.Min.Y - Radius && Position.Y <= Bounds.Max.Y + Radius)
            {
                return false;
            }
        }
        const FVector Offsets[] = {FVector::ZeroVector,
            FVector(Radius, 0, 0),
            FVector(-Radius, 0, 0),
            FVector(0, Radius, 0),
            FVector(0, -Radius, 0)};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Offsets); ++Index)
        {
            FHitResult Hit;
            const FVector Sample = Position + Offsets[Index];
            // First blocking surface must be landscape: do not skip a roof/pool to find land
            // underneath.
            const bool bHit = World.LineTraceSingleByChannel(Hit,
                Sample + FVector(0, 0, UE::Wanderer::SurfaceInteraction::GrassTraceDistance),
                Sample - FVector(0, 0, UE::Wanderer::SurfaceInteraction::GrassTraceDistance),
                ECC_Visibility,
                Query);
            if (!bHit || !Cast<ALandscapeProxy>(Hit.GetActor()))
            {
                return false;
            }
            if (Index == 0)
            {
                Point.Transform.SetLocation(FVector(Position.X,
                    Position.Y,
                    Hit.ImpactPoint.Z + UE::Wanderer::SurfaceInteraction::GrassGroundOffset
                        - Settings.MeshBaseZ * Point.Transform.GetScale3D().Z));
            }
        }
        return true;
    }

    void CacheWaterBounds(FGrassProjectionContext& InContext, UWorld& InWorld)
    {
        if (InContext.bInitialized)
        {
            return;
        }
        for (TActorIterator<APhysicsVolume> Volume(&InWorld); Volume; ++Volume)
        {
            if (Volume->bWaterVolume)
            {
                InContext.WaterBounds.Add(Volume->GetComponentsBoundingBox(true));
            }
        }
        InContext.bInitialized = true;
    }

    UPCGPointData* PrepareOutput(FGrassProjectionContext& InContext,
        const FPCGTaggedData& InInput,
        const UPCGPointData& InPoints)
    {
        if (InContext.PointIndex == 0)
        {
            UPCGPointData* Output = FPCGContext::NewObject_AnyThread<UPCGPointData>(&InContext);
            Output->InitializeFromData(&InPoints);
            FPCGTaggedData& Tagged = InContext.OutputData.TaggedData.Add_GetRef(InInput);
            Tagged.Data = Output;
            Tagged.Pin = PCGPinConstants::DefaultOutputLabel;
        }
        // This is our context-owned output, not borrowed input data.
        return const_cast<UPCGPointData*>(
            CastChecked<UPCGPointData>(InContext.OutputData.TaggedData.Last().Data));
    }

    class FGrassProjectionElement : public IPCGElement
    {
      public:
        virtual FPCGContext* Initialize(const FPCGDataCollection& InputData,
            TWeakObjectPtr<UPCGComponent> SourceComponent,
            const UPCGNode* Node) override
        {
            // IPCGElement transfers this allocation to the engine executor.
            FGrassProjectionContext* Context = new FGrassProjectionContext();
            Context->InputData = InputData;
            Context->SourceComponent = SourceComponent;
            Context->Node = Node;
            return Context;
        }
        virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override
        {
            return true;
        }
        virtual bool IsCacheable(const UPCGSettings*) const override
        {
            return false;
        }

      protected:
        virtual bool ExecuteInternal(FPCGContext* InContext) const override
        {
            FGrassProjectionContext* Context = static_cast<FGrassProjectionContext*>(InContext);
            const UWandererGrassProjectionSettings* Settings =
                Context->GetInputSettings<UWandererGrassProjectionSettings>();
            UPCGComponent* Source = Context->SourceComponent.Get();
            UWorld* World = Source ? Source->GetWorld() : nullptr;
            if (!World || !Settings)
            {
                PCGE_LOG(Error,
                    GraphAndLog,
                    NSLOCTEXT("Wanderer",
                        "GrassWorldMissing",
                        "Grass projection needs a live PCG component/world."));
                return true;
            }
            CacheWaterBounds(*Context, *World);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(WandererGrassProjection), true);
            Query.AddIgnoredActor(
                Source->GetOwner()); // Ignore the old managed grass while regenerating.
            const TArray<FPCGTaggedData> Inputs =
                Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
            int32 Budget = UE::Wanderer::SurfaceInteraction::
                GrassProjectionBudget; // Bound main-thread trace work per executor invocation.
            while (Context->InputIndex < Inputs.Num())
            {
                const FPCGTaggedData& Input = Inputs[Context->InputIndex];
                const UPCGPointData* Points = Cast<UPCGPointData>(Input.Data);
                if (!Points)
                {
                    PCGE_LOG(Error,
                        GraphAndLog,
                        NSLOCTEXT("Wanderer",
                            "GrassPointsMissing",
                            "Grass projection expects point data."));
                    ++Context->InputIndex;
                    continue;
                }
                UPCGPointData* Output = PrepareOutput(*Context, Input, *Points);
                while (Context->PointIndex < Points->GetPoints().Num())
                {
                    FPCGPoint Point = Points->GetPoints()[Context->PointIndex++];
                    if (ProjectGrass(*World, Query, Context->WaterBounds, *Settings, Point))
                    {
                        Output->GetMutablePoints().Add(Point);
                    }
                    if (--Budget == 0 || Context->ShouldStop())
                    {
                        return false;
                    }
                }
                Context->PointIndex = 0;
                ++Context->InputIndex;
            }
            return true;
        }
    };
} // namespace

TArray<FPCGPinProperties> UWandererGrassProjectionSettings::InputPinProperties() const
{
    return {FPCGPinProperties(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point)};
}

TArray<FPCGPinProperties> UWandererGrassProjectionSettings::OutputPinProperties() const
{
    return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}

FPCGElementPtr UWandererGrassProjectionSettings::CreateElement() const
{
    return MakeShared<FGrassProjectionElement>();
}
