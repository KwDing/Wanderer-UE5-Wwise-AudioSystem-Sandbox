#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"

#include "ActorFactories/ActorFactory.h"
#include "AkAcousticPortal.h"
#include "AkAudioDevice.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "BSPOps.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Editing/VolumeBrushUtils.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/Polys.h"
#include "Engine/World.h"
#include "GeomTools.h"
#include "Model.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"

namespace
{
    void FinishRoomBrush(AAkSpatialAudioVolume& Room)
    {
        Room.Brush->BuildBound();
        FBSPOps::csgPrepMovingBrush(&Room);
        Room.PostEditChange();
        Room.PostRebuildBrush();
        Room.MarkPackageDirty();
    }
} // namespace

AVolume* UAudioAuthoringLibrary::CreateAcousticBox(UObject* WorldContext,
    TSubclassOf<AVolume> VolumeClass,
    FVector HalfExtent,
    FTransform Transform)
{
    UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    if (!IsInGameThread() || !World || World->WorldType != EWorldType::Editor
        || (VolumeClass != AAkSpatialAudioVolume::StaticClass()
            && VolumeClass != AAkAcousticPortal::StaticClass())
        || HalfExtent.ContainsNaN() || HalfExtent.GetMin() <= 0.0 || Transform.ContainsNaN())
    {
        return nullptr;
    }
    AVolume* Volume = World->SpawnActor<AVolume>(VolumeClass);
    if (!Volume)
    {
        return nullptr;
    }
    UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume);
    UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
    const TArray<FVector2D> Outline = {{-HalfExtent.X, -HalfExtent.Y},
        {HalfExtent.X, -HalfExtent.Y},
        {HalfExtent.X, HalfExtent.Y},
        {-HalfExtent.X, HalfExtent.Y}};
    WandererVolumeBrush::BuildVolumePrism(*Volume, Outline, HalfExtent.Z * 2.0, HalfExtent.Z);
    Volume->SetActorTransform(Transform);
    Volume->PostEditChange();
    if (auto* Room = Cast<AAkSpatialAudioVolume>(Volume))
    {
        Room->PostRebuildBrush();
    }
    Volume->MarkPackageDirty();
    return Volume;
}

FString UAudioAuthoringLibrary::GetAcousticRoomAtLocation(UObject* WorldContext, FVector Position)
{
    if (!IsInGameThread() || !IsValid(WorldContext) || !WorldContext->GetWorld()
        || Position.ContainsNaN())
    {
        return TEXT("InvalidWorld");
    }
    if (FAkAudioDevice* Device = FAkAudioDevice::Get())
    {
        const auto Rooms = Device->FindRoomComponentsAtLocation(Position, WorldContext->GetWorld());
        if (!Rooms.IsEmpty() && IsValid(Rooms[0]) && IsValid(Rooms[0]->GetOwner()))
        {
            return Rooms[0]->GetOwner()->GetActorLabel();
        }
        return TEXT("Outdoors");
    }
    return TEXT("Unavailable");
}

int32 UAudioAuthoringLibrary::MoveAudioActorsToPersistent(const TArray<AActor*>& Actors)
{
    if (!GEditor || GEditor->PlayWorld || Actors.IsEmpty() || !IsValid(Actors[0]))
    {
        return 0;
    }
    UWorld* World = Actors[0]->GetWorld();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        return 0;
    }
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor) || Actor->GetWorld() != World
            || !Actor->FindComponentByClass<UWandererAudioSourceComponent>())
        {
            return 0;
        }
    }
    return UEditorLevelUtils::MoveActorsToLevel(Actors, World->PersistentLevel, false, false, true);
}

bool UAudioAuthoringLibrary::RebuildAcousticFloorRoom(
    AVolume* Volume, const TArray<FVector2D>& Outline, float HalfHeight)
{
    auto* Room = Cast<AAkSpatialAudioVolume>(Volume);
    if (!IsInGameThread() || !IsValid(Room) || !Room->GetWorld()
        || Room->GetWorld()->WorldType != EWorldType::Editor || !Room->Brush || !Room->Brush->Polys
        || !FMath::IsFinite(HalfHeight) || HalfHeight <= 0 || Outline.Num() < 3
        || Outline.Num() > 128)
    {
        return false;
    }
    for (const FVector2D& Point : Outline)
    {
        if (Point.ContainsNaN())
        {
            return false;
        }
    }
    TArray<FVector2D> Vertices;
    FGeomTools2D::CorrectPolygonWinding(Vertices, Outline, false);
    TArray<FVector2D> Triangles;
    if (!FGeomTools2D::ArePolygonsValid({Vertices})
        || !FGeomTools2D::TriangulatePoly(Triangles, Vertices))
    {
        return false;
    }
    // Triangulate before mutation. World/actor retain all ownership; no retained pointers or async
    // work.
    Room->Modify();
    UModel& Model = *Room->Brush;
    Model.Modify();
    Model.Polys->Modify();
    Model.Polys->Element.Reset();
    for (int32 Index = 0; Index < Vertices.Num(); ++Index)
    {
        const FVector2D& Point = Vertices[Index];
        const FVector2D& Next = Vertices[(Index + 1) % Vertices.Num()];
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(Point.X, Point.Y, -HalfHeight),
                FVector3f(Next.X, Next.Y, -HalfHeight),
                FVector3f(Next.X, Next.Y, HalfHeight),
                FVector3f(Point.X, Point.Y, HalfHeight)});
    }
    for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
    {
        const FVector2D& A = Triangles[Index];
        const FVector2D& B = Triangles[Index + 1];
        const FVector2D& C = Triangles[Index + 2];
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(A.X, A.Y, HalfHeight),
                FVector3f(B.X, B.Y, HalfHeight),
                FVector3f(C.X, C.Y, HalfHeight)});
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(C.X, C.Y, -HalfHeight),
                FVector3f(B.X, B.Y, -HalfHeight),
                FVector3f(A.X, A.Y, -HalfHeight)});
    }
    FinishRoomBrush(*Room);
    return true;
}

bool UAudioAuthoringLibrary::RebuildConvexAcousticRoom(
    AVolume* Volume, const TArray<FVector2D>& Outline, float HalfLength)
{
    auto* Room = Cast<AAkSpatialAudioVolume>(Volume);
    if (!IsInGameThread() || !IsValid(Room) || !Room->GetWorld()
        || Room->GetWorld()->WorldType != EWorldType::Editor || !Room->Brush || !Room->Brush->Polys
        || !WandererVolumeBrush::IsValidVolumeOutline(Outline, HalfLength))
    {
        UE_LOG(LogTemp,
            Warning,
            TEXT("Acoustic prism requires an editor Room, positive half l"
                 "ength and a convex counterclockwise XZ outline."));
        return false;
    }
    // Existing Room owns Model/Polys; synchronous editor mutation retains no borrowed pointers.
    Room->Modify();
    UModel& Model = *Room->Brush;
    Model.Modify();
    Model.Polys->Modify();
    Model.Polys->Element.Reset();
    for (int32 Index = 0; Index < Outline.Num(); ++Index)
    {
        const FVector2D& Point = Outline[Index];
        const FVector2D& Next = Outline[(Index + 1) % Outline.Num()];
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(Point.X, HalfLength, Point.Y),
                FVector3f(Next.X, HalfLength, Next.Y),
                FVector3f(Next.X, -HalfLength, Next.Y),
                FVector3f(Point.X, -HalfLength, Point.Y)});
        if (Index > 0 && Index < Outline.Num() - 1)
        {
            const FVector2D& First = Outline[0];
            WandererVolumeBrush::AddFace(Model,
                {FVector3f(First.X, -HalfLength, First.Y),
                    FVector3f(Point.X, -HalfLength, Point.Y),
                    FVector3f(Next.X, -HalfLength, Next.Y)});
            WandererVolumeBrush::AddFace(Model,
                {FVector3f(First.X, HalfLength, First.Y),
                    FVector3f(Next.X, HalfLength, Next.Y),
                    FVector3f(Point.X, HalfLength, Point.Y)});
        }
    }
    FinishRoomBrush(*Room);
    return true;
}

bool UAudioAuthoringLibrary::RebuildSteppedAcousticRoom(
    AVolume* Volume, FVector HalfExtent, float Extension, float StepZ)
{
    auto* Room = Cast<AAkSpatialAudioVolume>(Volume);
    if (!IsInGameThread() || !IsValid(Room) || !Room->GetWorld()
        || Room->GetWorld()->WorldType != EWorldType::Editor || !Room->Brush || !Room->Brush->Polys
        || HalfExtent.ContainsNaN() || HalfExtent.GetMin() <= 0 || !FMath::IsFinite(Extension)
        || Extension <= 0 || !FMath::IsFinite(StepZ) || StepZ <= -HalfExtent.Z
        || StepZ >= HalfExtent.Z)
    {
        return false;
    }
    // Borrow existing actor-owned Model/Polys synchronously; no new registrations or retained
    // objects.
    Room->Modify();
    UModel& Model = *Room->Brush;
    Model.Modify();
    Model.Polys->Modify();
    Model.Polys->Element.Reset();
    const TArray<FVector2D> Outline = {{-HalfExtent.Y, -HalfExtent.Z},
        {HalfExtent.Y, -HalfExtent.Z},
        {HalfExtent.Y, StepZ},
        {HalfExtent.Y + Extension, StepZ},
        {HalfExtent.Y + Extension, HalfExtent.Z},
        {-HalfExtent.Y, HalfExtent.Z}};
    for (int32 Index = 0; Index < Outline.Num(); ++Index)
    {
        const FVector2D& Point = Outline[Index];
        const FVector2D& Next = Outline[(Index + 1) % Outline.Num()];
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(-HalfExtent.X, Point.X, Point.Y),
                FVector3f(-HalfExtent.X, Next.X, Next.Y),
                FVector3f(HalfExtent.X, Next.X, Next.Y),
                FVector3f(HalfExtent.X, Point.X, Point.Y)});
    }
    // Explicit triangulation retains the re-entrant corner; a convex hull would include outdoor
    // ground.
    const int32 Triangles[4][3] = {{0, 1, 2}, {0, 2, 5}, {2, 3, 4}, {2, 4, 5}};
    for (const auto& Triangle : Triangles)
    {
        const FVector2D& A = Outline[Triangle[0]];
        const FVector2D& B = Outline[Triangle[1]];
        const FVector2D& C = Outline[Triangle[2]];
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(HalfExtent.X, A.X, A.Y),
                FVector3f(HalfExtent.X, B.X, B.Y),
                FVector3f(HalfExtent.X, C.X, C.Y)});
        WandererVolumeBrush::AddFace(Model,
            {FVector3f(-HalfExtent.X, C.X, C.Y),
                FVector3f(-HalfExtent.X, B.X, B.Y),
                FVector3f(-HalfExtent.X, A.X, A.Y)});
    }
    FinishRoomBrush(*Room);
    return true;
}
