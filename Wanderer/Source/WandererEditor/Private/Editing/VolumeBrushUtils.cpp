#include "Editing/VolumeBrushUtils.h"

#include "BSPOps.h"
#include "Engine/Polys.h"
#include "GameFramework/Volume.h"
#include "Model.h"

namespace WandererVolumeBrush
{
    constexpr int32 MinimumOutlineVertices = 3;
    constexpr int32 MaximumOutlineVertices = 128;
    bool IsValidVolumeOutline(const TArray<FVector2D>& Outline, float Depth)
    {
        if (Outline.Num() < MinimumOutlineVertices || Outline.Num() > MaximumOutlineVertices
            || !FMath::IsFinite(Depth) || Depth <= 0.0f)
        {
            return false;
        }
        for (int32 Index = 0; Index < Outline.Num(); ++Index)
        {
            const FVector2D& Point = Outline[Index];
            if (Point.ContainsNaN())
            {
                return false;
            }
            const FVector2D Edge = Outline[(Index + 1) % Outline.Num()] - Point;
            const FVector2D NextEdge =
                Outline[(Index + 2) % Outline.Num()] - Outline[(Index + 1) % Outline.Num()];
            if (FVector2D::CrossProduct(Edge, NextEdge) <= 0.0)
            {
                return false;
            }
        }
        return true;
    }

    void AddFace(UModel& Model, const TArray<FVector3f>& Vertices)
    {
        FPoly Face;
        Face.Init();
        Face.Vertices = Vertices;
        Face.Base = Vertices[0];
        if (Face.Finalize(nullptr, 1) == 0)
        {
            Model.Polys->Element.Add(Face);
        }
    }

    void BuildVolumePrism(
        AVolume& Volume, const TArray<FVector2D>& Outline, float Depth, float TopZ)
    {
        if (!IsInGameThread() || !Volume.Brush || !Volume.Brush->Polys
            || !IsValidVolumeOutline(Outline, Depth) || !FMath::IsFinite(TopZ))
        {
            return;
        }
        Volume.Modify();
        Volume.Brush->Modify();
        Volume.Brush->Polys->Modify();
        UModel& Model = *Volume.Brush;
        Model.Polys->Element.Reset();
        for (int32 Index = 0; Index < Outline.Num(); ++Index)
        {
            const FVector2D& Point = Outline[Index];
            const FVector2D& Next = Outline[(Index + 1) % Outline.Num()];
            const FVector3f Top(Point.X, Point.Y, TopZ);
            const FVector3f NextTop(Next.X, Next.Y, TopZ);
            const FVector3f Bottom(Point.X, Point.Y, TopZ - Depth);
            const FVector3f NextBottom(Next.X, Next.Y, TopZ - Depth);
            AddFace(Model, {Bottom, NextBottom, NextTop, Top});
            if (Index > 0 && Index < Outline.Num() - 1)
            {
                const FVector3f First(Outline[0].X, Outline[0].Y, TopZ);
                AddFace(Model, {First, Top, NextTop});
                AddFace(Model, {FVector3f(First.X, First.Y, TopZ - Depth), NextBottom, Bottom});
            }
        }
        Model.BuildBound();
        FBSPOps::csgPrepMovingBrush(&Volume);
        Volume.PostEditChange();
    }
} // namespace WandererVolumeBrush
