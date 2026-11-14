#include "Features/AudioManager/Authoring/Geometry/AudioGeometryUtils.h"

#include "Editor.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryWriter.h"

#include "InteriorFitArea.h"

#include "Actors/Audio/WandererAudioAreaEmitter.h"
#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"

namespace AudioGeometryUtils
{
    constexpr int32 SplinePointCountMax = 256;
    constexpr int32 ClosedSplinePointCountMin = 3;
    constexpr int32 OpenSplinePointCountMin = 2;
    constexpr float StandardLoopKeyOffset = 1.0f;

    FString ValidateTransform(const FTransform& InTransform)
    {
        return InTransform.ContainsNaN() || !InTransform.GetRotation().IsNormalized()
                       || InTransform.GetScale3D().GetMin() <= UE_SMALL_NUMBER
                   ? TEXT("几何变换必须有限、旋转有效且缩放为正。")
                   : FString();
    }

    FString ReadSplinePoints(const USplineComponent& InSpline, FAudioGeometrySnapshot& OutGeometry)
    {
        const int32 Count = InSpline.GetNumberOfSplinePoints();
        if (Count < (OutGeometry.bClosed ? ClosedSplinePointCountMin : OpenSplinePointCountMin)
            || Count > SplinePointCountMax)
        {
            return TEXT("样条需要 2–256 个点；闭合形状至少 3 点。");
        }
        if (OutGeometry.UpVector.ContainsNaN())
        {
            return TEXT("样条 Up Vector 无效。");
        }
        if (OutGeometry.bClosed
            && !FMath::IsNearlyEqual(InSpline.SplineCurves.Position.LoopKeyOffset,
                                     StandardLoopKeyOffset))
        {
            return TEXT("不支持自定义闭合输入键；请先恢复标准闭合键。");
        }
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FSplinePoint Point = InSpline.GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
            // GetSplinePointAt returns the tangent-oriented rotation, not the stored rotation key.
            Point.Rotation = InSpline.GetSplinePointsRotation().Points[Index].OutVal.Rotator();
            if (Point.InputKey != Index || Point.Position.ContainsNaN()
                || Point.ArriveTangent.ContainsNaN() || Point.LeaveTangent.ContainsNaN()
                || Point.Rotation.ContainsNaN() || Point.Scale.ContainsNaN()
                || Point.Type == ESplinePointType::Constant)
            {
                return TEXT("样条点/切线无效或使用不支持的 Constant/非连续输入键。");
            }
            OutGeometry.Points.Add(Point);
        }
        return FString();
    }

    FString Read(AActor* InActor, FAudioGeometrySnapshot& OutGeometry)
    {
        OutGeometry = FAudioGeometrySnapshot();
        if (!IsValid(InActor) || InActor->IsActorBeingDestroyed())
        {
            return TEXT("几何对象已失效。");
        }
        if (const auto* Area = Cast<AInteriorFitArea>(InActor))
        {
            if (!Area->IsShapeValid())
            {
                return TEXT("Area 采样格或上下边界无效。");
            }
            OutGeometry.bArea = true;
            OutGeometry.Transform = Area->GetActorTransform();
            OutGeometry.Cells = Area->GetSurfaceCells();
            OutGeometry.CellSize = Area->CellSize;
            OutGeometry.Above = Area->HeightAboveSurface;
            OutGeometry.Below = Area->DepthBelowSurface;
            return ValidateTransform(OutGeometry.Transform);
        }
        TArray<USplineComponent*> Splines;
        InActor->GetComponents(Splines);
        if (Splines.Num() != 1)
        {
            return TEXT("来源必须只有一条样条。");
        }
        const USplineComponent* Spline = Splines[0];
        OutGeometry.Transform = Spline->GetComponentTransform();
        OutGeometry.bClosed = Spline->IsClosedLoop();
        OutGeometry.bStationary = Spline->bStationaryEndpoints;
        OutGeometry.bSplineEdited = Spline->bSplineHasBeenEdited;
        OutGeometry.UpVector = Spline->DefaultUpVector;
        if (const auto* Emitter = Cast<AWandererAudioSplineEmitter>(InActor))
        {
            OutGeometry.AudioShape = static_cast<uint8>(Emitter->Shape);
            OutGeometry.Above = Emitter->HeightAboveSurface;
            OutGeometry.Below = Emitter->DepthBelowSurface;
        }
        const FString Error = ReadSplinePoints(*Spline, OutGeometry);
        if (!Error.IsEmpty())
        {
            return Error;
        }
        return ValidateTransform(OutGeometry.Transform);
    }

    bool Write(AActor* InTarget, const FAudioGeometrySnapshot& InGeometry)
    {
        if (!IsValid(InTarget) || InTarget->IsActorBeingDestroyed()
            || !InTarget->GetRootComponent())
        {
            return false;
        }

        if (auto* Area = Cast<AWandererAudioAreaEmitter>(InTarget))
        {
            Area->CellSize = InGeometry.CellSize;
            Area->HeightAboveSurface = InGeometry.Above;
            Area->DepthBelowSurface = InGeometry.Below;
            Area->SetSurfaceCells(InGeometry.Cells);
            Area->SetActorTransform(InGeometry.Transform);
            if (!IsValid(Area) || Area->IsActorBeingDestroyed())
            {
                return false;
            }
            Area->RefreshEditorFocusBounds();
            return IsValid(Area) && !Area->IsActorBeingDestroyed();
        }
        auto* Emitter = CastChecked<AWandererAudioSplineEmitter>(InTarget);
        Emitter->Shape
            = InGeometry.AudioShape.IsSet()
                  ? static_cast<EWandererAmbientSplineShape>(InGeometry.AudioShape.GetValue())
              : InGeometry.bClosed ? EWandererAmbientSplineShape::Lake
                                   : EWandererAmbientSplineShape::Line;
        Emitter->HeightAboveSurface = InGeometry.Above;
        Emitter->DepthBelowSurface = InGeometry.Below;
        USplineComponent* Spline = Emitter->Spline.Get();
        Spline->ClearSplinePoints(false);
        Spline->AddPoints(InGeometry.Points, false);
        Spline->DefaultUpVector = InGeometry.UpVector;
        Spline->bStationaryEndpoints = InGeometry.bStationary;
        Spline->bSplineHasBeenEdited = InGeometry.bSplineEdited;
        Spline->SetClosedLoop(InGeometry.bClosed, false);
        Spline->UpdateSpline();
        InTarget->SetActorTransform(InGeometry.Transform);
        return IsValid(InTarget) && !InTarget->IsActorBeingDestroyed();
    }

    FString ValidateResult(AActor* InActor)
    {
        if (!IsValid(InActor) || InActor->IsActorBeingDestroyed())
        {
            return TEXT("声源几何已失效。");
        }
        if (const auto* Area = Cast<AWandererAudioAreaEmitter>(InActor))
        {
            return Area->IsShapeValid() ? FString() : TEXT("导入后 Area 无效。");
        }
        return CastChecked<AWandererAudioSplineEmitter>(InActor)->GetGeometryError();
    }

    bool MatchesGeometry(const FAudioGeometrySnapshot& InLeft,
                         const FAudioGeometrySnapshot& InRight)
    {
        // UE spline keys round-trip through quaternion/rotator conversion and transform
        // normalization.
        constexpr double RoundTripTolerance = 0.0001;
        const uint8 LeftShape = InLeft.AudioShape.Get(
            static_cast<uint8>(InLeft.bClosed ? EWandererAmbientSplineShape::Lake
                                              : EWandererAmbientSplineShape::Line));
        const uint8 RightShape = InRight.AudioShape.Get(
            static_cast<uint8>(InRight.bClosed ? EWandererAmbientSplineShape::Lake
                                               : EWandererAmbientSplineShape::Line));
        if (!InLeft.Transform.Equals(InRight.Transform, RoundTripTolerance)
            || InLeft.bArea != InRight.bArea || InLeft.bClosed != InRight.bClosed
            || InLeft.bStationary != InRight.bStationary || InLeft.CellSize != InRight.CellSize
            || InLeft.Above != InRight.Above || InLeft.Below != InRight.Below
            || InLeft.Cells != InRight.Cells || InLeft.Points.Num() != InRight.Points.Num()
            || !InLeft.UpVector.Equals(InRight.UpVector, RoundTripTolerance)
            || (!InLeft.bArea
                && (LeftShape != RightShape || InLeft.bSplineEdited != InRight.bSplineEdited)))
        {
            return false;
        }
        for (int32 Index = 0; Index < InLeft.Points.Num(); ++Index)
        {
            const FSplinePoint& Left = InLeft.Points[Index];
            const FSplinePoint& Right = InRight.Points[Index];
            if (Left.InputKey != Right.InputKey || Left.Type != Right.Type
                || !Left.Position.Equals(Right.Position, RoundTripTolerance)
                || !Left.ArriveTangent.Equals(Right.ArriveTangent, RoundTripTolerance)
                || !Left.LeaveTangent.Equals(Right.LeaveTangent, RoundTripTolerance)
                || !Left.Rotation.Equals(Right.Rotation, RoundTripTolerance)
                || !Left.Scale.Equals(Right.Scale, RoundTripTolerance))
            {
                return false;
            }
        }
        return true;
    }

    FString Digest(FAudioGeometrySnapshot InGeometry)
    {
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes);
        Writer << InGeometry.Transform << InGeometry.Cells << InGeometry.CellSize
               << InGeometry.Above << InGeometry.Below;
        Writer << InGeometry.bArea << InGeometry.bClosed << InGeometry.bStationary
               << InGeometry.UpVector;
        for (FSplinePoint& Point : InGeometry.Points)
        {
            Writer << Point.InputKey << Point.Position << Point.ArriveTangent << Point.LeaveTangent
                   << Point.Rotation << Point.Scale;
            uint8 Type = Point.Type;
            Writer << Type;
        }
        return FString::Printf(TEXT("v1-%08x"), FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()));
    }

    FAudioGeometryMetadata ReadMetadata(const UWandererAudioSourceComponent& InAudio)
    {
        FAudioGeometryMetadata Result;
        Result.Source = InAudio.GeometryImportSource;
        Result.Digest = InAudio.GeometryImportDigest;
        Result.Group = InAudio.GeometryImportGroup;
        Result.Batch = InAudio.GeometryImportBatch;
        Result.Status = InAudio.GeometrySyncStatus;
        Result.bEnabled = InAudio.bEnabled;
        Result.bAutoSync = InAudio.bGeometryAutoSync;
        Result.bSuspended = InAudio.bGeometrySyncSuspended;
        Result.bWasEnabled = InAudio.bGeometrySyncWasEnabled;
        return Result;
    }

    void WriteMetadata(UWandererAudioSourceComponent& InAudio,
                       const FAudioGeometryMetadata& InMetadata)
    {
        InAudio.GeometryImportSource = InMetadata.Source;
        InAudio.GeometryImportDigest = InMetadata.Digest;
        InAudio.GeometryImportGroup = InMetadata.Group;
        InAudio.GeometryImportBatch = InMetadata.Batch;
        InAudio.GeometrySyncStatus = InMetadata.Status;
        InAudio.bEnabled = InMetadata.bEnabled;
        InAudio.bGeometryAutoSync = InMetadata.bAutoSync;
        InAudio.bGeometrySyncSuspended = InMetadata.bSuspended;
        InAudio.bGeometrySyncWasEnabled = InMetadata.bWasEnabled;
        InAudio.MarkPackageDirty();
    }

    bool Matches(const FAudioGeometryMetadata& InLeft, const FAudioGeometryMetadata& InRight)
    {
        return InLeft.Source == InRight.Source && InLeft.Digest == InRight.Digest
               && InLeft.Group == InRight.Group && InLeft.Batch == InRight.Batch
               && InLeft.Status == InRight.Status && InLeft.bEnabled == InRight.bEnabled
               && InLeft.bAutoSync == InRight.bAutoSync && InLeft.bSuspended == InRight.bSuspended
               && InLeft.bWasEnabled == InRight.bWasEnabled;
    }
    void SetLabel(AActor& InTarget, const AActor& InSource, bool bInArea)
    {
        FString Name = InSource.GetActorLabel();
        bool bGenerated = false;
        for (const TCHAR* Prefix : {TEXT("Fit_lake_"), TEXT("Fit_river_"), TEXT("Fit_area_")})
        {
            if (Name.RemoveFromStart(Prefix))
            {
                bGenerated = true;
                break;
            }
        }
        if (bGenerated)
        {
            int32 Separator;
            // Strip only IF's first-result suffix; preserve numbered mesh names and extra river
            // branches.
            if (Name.FindLastChar(TEXT('_'), Separator) && Name.Mid(Separator + 1) == TEXT("1"))
            {
                Name.LeftInline(Separator);
            }
        }
        else
        {
            Name.RemoveFromStart(TEXT("Fit_"));
        }
        // Labels are presentation only. Import paths/proof tags remain the pairing identity.
        FActorLabelUtilities::SetActorLabelUnique(
            &InTarget, (bInArea ? TEXT("AUDIO_AREA_") : TEXT("AUDIO_SPLINE_")) + Name);
    }

} // namespace AudioGeometryUtils
