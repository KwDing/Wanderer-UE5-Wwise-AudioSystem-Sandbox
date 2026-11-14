#include "Core/WaterContactUtils.h"

#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

#include "Data/SurfaceInteractionData.h"

namespace UE::Wanderer::Water
{
    static void AddSection(const FVector& Start,
        const FVector& End,
        float Radius,
        float Height,
        TArray<FWandererWaterBodySection>& OutSections)
    {
        if (OutSections.Num() >= MaxBodySections || Start.ContainsNaN() || End.ContainsNaN()
            || !FMath::IsFinite(Radius) || Radius <= 0.0f)
        {
            return;
        }
        // Small clothing/water-film allowance outside the authored collision hull.
        Radius += SurfaceInteraction::BodyFilmAllowance;
        if (Height < FMath::Min(Start.Z, End.Z) - Radius
            || Height > FMath::Max(Start.Z, End.Z) + Radius)
        {
            return;
        }
        OutSections.Add({Start, End, Radius});
    }

    static void SampleBodyGeometry(const FKAggregateGeom& Geometry,
        const FTransform& Bone,
        float Height,
        TArray<FWandererWaterBodySection>& OutSections)
    {
        const float Scale = Bone.GetScale3D().GetAbsMax();
        for (const FKSphylElem& Shape : Geometry.SphylElems)
        {
            const FTransform Local = Shape.GetTransform();
            const FVector Start = Bone.TransformPosition(
                Local.TransformPosition(FVector(0.0f, 0.0f, -Shape.Length * 0.5f)));
            const FVector End = Bone.TransformPosition(
                Local.TransformPosition(FVector(0.0f, 0.0f, Shape.Length * 0.5f)));
            AddSection(Start, End, Shape.Radius * Scale, Height, OutSections);
        }
        for (const FKSphereElem& Shape : Geometry.SphereElems)
        {
            const FVector Center = Bone.TransformPosition(Shape.Center);
            AddSection(Center, Center, Shape.Radius * Scale, Height, OutSections);
        }
        for (const FKBoxElem& Shape : Geometry.BoxElems)
        {
            // Rounded body-shape proxy for palms and feet; follows their authored axes.
            const FVector Dimensions(Shape.X, Shape.Y, Shape.Z);
            const int32 Axis = Shape.X >= Shape.Y && Shape.X >= Shape.Z ? 0
                               : Shape.Y >= Shape.Z                     ? 1
                                                                        : 2;
            const float Radius =
                (Dimensions.X + Dimensions.Y + Dimensions.Z - Dimensions[Axis]) * 0.25f;
            FVector Offset = FVector::ZeroVector;
            Offset[Axis] = FMath::Max(0.0f, Dimensions[Axis] * 0.5f - Radius);
            const FTransform Local = Shape.GetTransform();
            AddSection(Bone.TransformPosition(Local.TransformPosition(-Offset)),
                Bone.TransformPosition(Local.TransformPosition(Offset)),
                Radius * Scale,
                Height,
                OutSections);
        }
    }

    void SampleBodySections(const USkeletalMeshComponent& Mesh,
        float WaterHeight,
        TArray<FWandererWaterBodySection>& OutSections)
    {
        OutSections.Reset();
        const UPhysicsAsset* Asset = Mesh.GetPhysicsAsset();
        if (!Asset)
        {
            return;
        }
        for (const USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
        {
            if (!Body)
            {
                continue;
            }
            const int32 BoneIndex = Mesh.GetBoneIndex(Body->BoneName);
            if (BoneIndex == INDEX_NONE)
            {
                continue;
            }
            SampleBodyGeometry(
                Body->AggGeom, Mesh.GetBoneTransform(BoneIndex), WaterHeight, OutSections);
        }
    }

    FBox GetSectionBounds(const TArray<FWandererWaterBodySection>& Sections)
    {
        FBox Bounds(ForceInit);
        for (const FWandererWaterBodySection& Section : Sections)
        {
            const FVector Radius(Section.Radius);
            Bounds += Section.Start - Radius;
            Bounds += Section.Start + Radius;
            Bounds += Section.End - Radius;
            Bounds += Section.End + Radius;
        }
        return Bounds;
    }

    FVector ProjectToBodyEdge(
        const FVector& Point, const TArray<FWandererWaterBodySection>& Sections)
    {
        FVector Edge = Point;
        for (int32 Iteration = 0; Iteration < SurfaceInteraction::BodyEdgeIterations; ++Iteration)
        {
            float Distance = TNumericLimits<float>::Max();
            FVector Gradient = FVector::ZeroVector;
            for (const FWandererWaterBodySection& Section : Sections)
            {
                const FVector Delta =
                    Edge - FMath::ClosestPointOnSegment(Edge, Section.Start, Section.End);
                const float Candidate = Delta.Size() - Section.Radius;
                if (Candidate >= Distance)
                {
                    continue;
                }
                Distance = Candidate;
                Gradient = Delta.GetSafeNormal();
                Gradient.Z = 0.0f;
            }
            if (Sections.IsEmpty() || FMath::Abs(Distance) < SurfaceInteraction::BodyEdgeTolerance)
            {
                break;
            }
            if (Gradient.IsNearlyZero())
            {
                Gradient = FVector(1.0f, 0.0f, 0.0f);
            }
            Edge -= Gradient
                    * FMath::Clamp(Distance
                                       / FMath::Max(Gradient.SizeSquared(),
                                           SurfaceInteraction::BodyEdgeMinimumGradient),
                        -SurfaceInteraction::BodyEdgeMaximumStep,
                        SurfaceInteraction::BodyEdgeMaximumStep);
        }
        return Edge;
    }
} // namespace UE::Wanderer::Water
