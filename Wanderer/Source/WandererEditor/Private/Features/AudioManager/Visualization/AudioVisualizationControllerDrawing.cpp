#include "Features/AudioManager/Visualization/AudioVisualizationController.h"

#include "Components/LineBatchComponent.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SceneManagement.h"

#include "Actors/Audio/WandererAudioSplineEmitter.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Interfaces/Audio/AudioGeometryProvider.h"

void FAudioVisualizationController::DrawNow()
{
    if (bStopped || !bEnabled)
    {
        return;
    }
    ClearDrawing();
    UWorld* World = ResolveWorld();
    if (!IsValid(World) || World->bIsTearingDown || !World->PersistentLineBatcher)
    {
        return;
    }
    VisualizationWorldWeakPtr = World;
    ULineBatchComponent* Batch = World->PersistentLineBatcher.Get();
    if (World->WorldType == EWorldType::PIE)
    {
        DrawPlaybackRelations(*World, *Batch);
        return;
    }
    const APlayerController* Player = World->GetFirstPlayerController();
    const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
    int32 Drawn = 0;
    for (const TWeakObjectPtr<UWandererAudioSourceComponent>& SourceWeakPtr : SourcesWeakPtr)
    {
        const AWandererAudioSplineEmitter* Actor =
            SourceWeakPtr.IsValid() ? Cast<AWandererAudioSplineEmitter>(SourceWeakPtr->GetOwner())
                                    : nullptr;
        if (!Actor || !Actor->Spline)
        {
            continue;
        }
        if (Drawn++ >= AudioVisualizationSettings::RelationCountMax)
        {
            break;
        }
        DrawPlaybackSplineOutline(*Batch, *Actor, DrawBatchID);
        FVector Position;
        if (!Actor->AudioSource || !Actor->AudioSource->TryGetPlaybackPosition(Position))
        {
            continue;
        }
        DrawSplinePlaybackLink(
            *Batch, Position, Pawn, AudioVisualizationSettings::EditorPlaybackColor, DrawBatchID);
    }
    if (World->WorldType == EWorldType::Editor && GEditor)
    {
        GEditor->RedrawLevelEditingViewports();
    }
}

void FAudioVisualizationController::DrawPlaybackRelations(
    UWorld& InWorld, ULineBatchComponent& InBatch)
{
    const APlayerController* Player = InWorld.GetFirstPlayerController();
    const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
    if (!Pawn)
    {
        Summary = TEXT("\n无受控角色，未绘制声源关系。");
        return;
    }
    const TArray<FPlaybackRelation> Relations =
        GatherPlaybackRelations(SourcesWeakPtr, *Pawn, RangeM);
    FVector ViewPosition;
    FRotator ViewRotation;
    Player->GetPlayerViewPoint(ViewPosition, ViewRotation);
    int32 Width = 0;
    int32 Height = 0;
    Player->GetViewportSize(Width, Height);
    const FPlaybackView View{FTransform(ViewRotation, ViewPosition),
        double(Width > 0 ? Width : AudioVisualizationSettings::ReferenceViewportWidth)};
    int32 LabelIndex = 0;
    for (const FPlaybackRelation& Relation : Relations)
    {
        LabelIndex += DrawRelation(InBatch, Relation, *Pawn, View, LabelIndex);
    }
    if (Relations.IsEmpty())
    {
        Summary = TEXT("\n范围内无Point/Area/Spline来源；调大附近范围，或检查来源配置/关卡加载。");
    }
}

bool FAudioVisualizationController::DrawRelation(ULineBatchComponent& InBatch,
    const FPlaybackRelation& InRelation,
    const APawn& InPawn,
    const FPlaybackView& InView,
    int32 InLabelIndex)
{
    const UWandererAudioSourceComponent* Source = InRelation.SourceWeakPtr.Get();
    if (!Source || !Source->GetOwner() || !Source->GetProfile())
    {
        return false;
    }
    const FWandererWorldAudioProfileRow* Row = Source->GetProfile();
    const FString Kind = Row->Mode == EWandererWorldAudioMode::Area     ? TEXT("Area")
                         : Row->Mode == EWandererWorldAudioMode::Spline ? TEXT("Spline")
                                                                        : TEXT("Point");
    const FString Name = FString::Printf(
        TEXT("[%02d] %s (%s)"), InRelation.Index + 1, *Source->GetOwner()->GetActorLabel(), *Kind);
    if (const auto* Spline = Cast<AWandererAudioSplineEmitter>(Source->GetOwner()))
    {
        DrawPlaybackSplineOutline(InBatch, *Spline, DrawBatchID);
    }
    if (!InRelation.bPosted)
    {
        Summary += TEXT("\n") + Name + TEXT(" 未播放/无实际发声点: ") + Source->GetStatus();
        return false;
    }
    const FLinearColor Color =
        FLinearColor::MakeFromHSV8(uint8(InRelation.Index * AudioVisualizationSettings::HueStride),
            AudioVisualizationSettings::ColorSaturation,
            AudioVisualizationSettings::ColorValue);
    DrawSplinePlaybackLink(InBatch, InRelation.Position, &InPawn, Color, DrawBatchID);
    DrawPlaybackSourceIndex(InBatch,
        InRelation.Position
            + InView.Transform.GetUnitAxis(EAxis::Z) * InLabelIndex
                  * AudioVisualizationSettings::LabelSpacing,
        InRelation.Index + 1,
        Color,
        InView,
        DrawBatchID);
    const FString Label = FString::Printf(TEXT("%s  ID=%d  d=%.2fm%s"),
        *Name,
        Source->GetPlayingID(),
        Source->GetDistanceM(),
        Source->GetDistanceM() < AudioVisualizationSettings::NearZeroDistanceM
            ? TEXT(" [与听者重合/近零]")
            : TEXT(""));
    // WorldSettings is the text base, so native HUD text never retains a streamed source actor.
    DrawDebugString(InPawn.GetWorld(),
        InRelation.Position
            + FVector(0,
                0,
                AudioVisualizationSettings::TextHeight
                    + InLabelIndex * AudioVisualizationSettings::LabelSpacing),
        Label,
        nullptr,
        Color.ToFColor(true),
        AudioVisualizationSettings::TextLifetimeSec,
        true,
        AudioVisualizationSettings::TextScale);
    Summary += TEXT("\n") + Label;
    return true;
}

void FAudioVisualizationController::DrawSplinePlaybackLink(ULineBatchComponent& InBatch,
    const FVector& InPosition,
    const APawn* InPawn,
    FLinearColor InColor,
    uint32 InBatchID)
{
    InBatch.DrawPoint(InPosition,
        InColor,
        AudioVisualizationSettings::EmitterPointSize,
        SDPG_Foreground,
        AudioVisualizationSettings::LineLifetimeSec,
        InBatchID);
    if (!InPawn)
    {
        return;
    }
    const FVector CharacterPosition = InPawn->GetActorLocation();
    if (CharacterPosition.ContainsNaN())
    {
        return;
    }
    InBatch.DrawPoint(CharacterPosition,
        AudioVisualizationSettings::PawnColor,
        AudioVisualizationSettings::PawnPointSize,
        SDPG_Foreground,
        AudioVisualizationSettings::LineLifetimeSec,
        InBatchID);
    InBatch.DrawLine(InPosition,
        CharacterPosition,
        InColor,
        SDPG_Foreground,
        AudioVisualizationSettings::LineThickness,
        AudioVisualizationSettings::LineLifetimeSec,
        InBatchID);
}

void FAudioVisualizationController::DrawPlaybackSourceIndex(ULineBatchComponent& InBatch,
    const FVector& InPosition,
    int32 InIndex,
    FLinearColor InColor,
    const FPlaybackView& InView,
    uint32 InBatchID)
{
    const FVector Right = InView.Transform.GetUnitAxis(EAxis::Y);
    const FVector Up = InView.Transform.GetUnitAxis(EAxis::Z);
    const double Size =
        FMath::Clamp(FVector::Distance(InPosition, InView.Transform.GetLocation())
                         * AudioVisualizationSettings::LabelPerspectiveScale
                         * AudioVisualizationSettings::ReferenceViewportWidth / InView.PixelWidth,
            AudioVisualizationSettings::LabelSizeMin,
            AudioVisualizationSettings::LabelSizeMax);
    const FVector Origin = InPosition + Right * Size * AudioVisualizationSettings::LabelRightOffset
                           + Up * Size * AudioVisualizationSettings::LabelUpOffset;
    const FString Text = FString::Printf(TEXT("%02d"), InIndex);
    for (int32 Character = 0; Character < Text.Len(); ++Character)
    {
        const uint8 Mask = AudioVisualizationSettings::DigitMasks[Text[Character] - TCHAR('0')];
        for (int32 Segment = 0; Segment < UE_ARRAY_COUNT(AudioVisualizationSettings::SegmentStarts);
            ++Segment)
        {
            if (Mask & (1 << Segment))
            {
                const FVector2D A =
                    AudioVisualizationSettings::DigitPoints
                        [AudioVisualizationSettings::SegmentStarts[Segment]]
                    + FVector2D(Character * AudioVisualizationSettings::DigitSpacing, 0);
                const FVector2D B =
                    AudioVisualizationSettings::DigitPoints
                        [AudioVisualizationSettings::SegmentEnds[Segment]]
                    + FVector2D(Character * AudioVisualizationSettings::DigitSpacing, 0);
                InBatch.DrawLine(Origin + (Right * A.X + Up * A.Y) * Size,
                    Origin + (Right * B.X + Up * B.Y) * Size,
                    InColor,
                    SDPG_Foreground,
                    AudioVisualizationSettings::LineThickness,
                    AudioVisualizationSettings::LineLifetimeSec,
                    InBatchID);
            }
        }
    }
}

float FAudioVisualizationController::GetPlaybackDistance(
    const UWandererAudioSourceComponent& InSource, const FVector& InPosition, float InRangeM)
{
    const FWandererWorldAudioProfileRow* Row = InSource.GetProfile();
    const AActor* Owner = InSource.GetOwner();
    constexpr float OutOfRange = TNumericLimits<float>::Max();
    if (!Row || !Owner)
    {
        return OutOfRange;
    }
    if (Row->Mode == EWandererWorldAudioMode::Point)
    {
        return FVector::Distance(InPosition, Owner->GetActorLocation())
               / AudioVisualizationSettings::CentimetersPerMeter;
    }
    if (Row->Mode != EWandererWorldAudioMode::Area && Row->Mode != EWandererWorldAudioMode::Spline)
    {
        return OutOfRange;
    }

    const IAudioGeometryProvider* Shape = Cast<IAudioGeometryProvider>(Owner);
    const EAudioGeometryKind ExpectedKind = Row->Mode == EWandererWorldAudioMode::Area
                                               ? EAudioGeometryKind::Area
                                               : EAudioGeometryKind::Spline;
    if (!Shape || Shape->GetAudioGeometryKind() != ExpectedKind
        || !(Shape->GetAudioBoundsDistanceM(InPosition) <= InRangeM))
    {
        return OutOfRange;
    }
    FAudioGeometrySample Sample;
    return Shape->QueryAudioGeometry(InPosition, AudioVisualizationSettings::SplineBlendDistanceM,
                                    Sample) ? Sample.DistanceM : OutOfRange;
}

TArray<FAudioVisualizationController::FPlaybackRelation> FAudioVisualizationController::
    GatherPlaybackRelations(const TArray<TWeakObjectPtr<UWandererAudioSourceComponent>>& InSources,
        const APawn& InPawn,
        float InRangeM)
{
    TArray<FPlaybackRelation> Result;
    for (int32 Index = 0; Index < InSources.Num(); ++Index)
    {
        const UWandererAudioSourceComponent* Source = InSources[Index].Get();
        if (!Source || Source->GetWorld() != InPawn.GetWorld())
        {
            continue;
        }
        const float Distance = GetPlaybackDistance(*Source, InPawn.GetActorLocation(), InRangeM);
        if (!FMath::IsFinite(Distance) || Distance > InRangeM)
        {
            continue;
        }
        FPlaybackRelation Relation;
        Relation.SourceWeakPtr = InSources[Index];
        Relation.ShapeDistanceM = Distance;
        Relation.Index = Index;
        Relation.bPosted = Source->TryGetPlaybackPosition(Relation.Position);
        Result.Add(Relation);
    }
    Result.Sort(
        [](const FPlaybackRelation& A, const FPlaybackRelation& B)
        {
            if (AudioVisualizationSettings::bPrioritizePostedSources && A.bPosted != B.bPosted)
            {
                return A.bPosted;
            }
            return A.ShapeDistanceM == B.ShapeDistanceM ? A.Index < B.Index
                                                        : A.ShapeDistanceM < B.ShapeDistanceM;
        });
    if (Result.Num() > AudioVisualizationSettings::RelationCountMax)
    {
        Result.SetNum(AudioVisualizationSettings::RelationCountMax);
    }
    return Result;
}

void FAudioVisualizationController::DrawPlaybackSplineOutline(
    ULineBatchComponent& InBatch, const AWandererAudioSplineEmitter& InActor, uint32 InBatchID)
{
    const USplineComponent* Spline = InActor.Spline.Get();
    if (!Spline || InActor.GetActorTransform().ContainsNaN())
    {
        return;
    }
    const int32 Count = Spline->GetNumberOfSplinePoints();
    if (Count < AudioVisualizationSettings::SplinePointCountMin
        || Count > AudioVisualizationSettings::SplinePointCountMax)
    {
        return;
    }
    const int32 Segments = Spline->IsClosedLoop() ? Count : Count - 1;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FVector Point =
            Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
        if (Point.ContainsNaN())
        {
            continue;
        }
        InBatch.DrawPoint(Point,
            AudioVisualizationSettings::SplinePointColor,
            AudioVisualizationSettings::SplinePointSize,
            SDPG_Foreground,
            AudioVisualizationSettings::LineLifetimeSec,
            InBatchID);
        if (Index >= Segments)
        {
            continue;
        }
        const int32 Steps = Spline->GetSplinePointType(Index) == ESplinePointType::Linear
                                ? AudioVisualizationSettings::LinearSplineSteps
                                : AudioVisualizationSettings::CurveSteps;
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            const FVector Start = Spline->GetLocationAtSplineInputKey(
                Index + float(Step) / Steps, ESplineCoordinateSpace::World);
            const FVector End = Spline->GetLocationAtSplineInputKey(
                Index + float(Step + 1) / Steps, ESplineCoordinateSpace::World);
            if (!Start.ContainsNaN() && !End.ContainsNaN())
            {
                InBatch.DrawLine(Start,
                    End,
                    AudioVisualizationSettings::SplineLineColor,
                    SDPG_Foreground,
                    AudioVisualizationSettings::LineThickness,
                    AudioVisualizationSettings::LineLifetimeSec,
                    InBatchID);
            }
        }
    }
}
