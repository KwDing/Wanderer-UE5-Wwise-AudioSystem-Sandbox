#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/Audio/WandererWorldAudioTypes.h"
#include "AudioAuthoringLibrary.generated.h"

class AVolume;
class UAkAudioEvent;
class UWandererAudioSourceComponent;
class UWandererBuildingAmbienceComponent;

/** Editor-only synchronous entry points. No method saves assets or maps.
 * Building creation/deletion own an independent transaction and reject an active caller transaction.
 * Surface/geometry mutations retain their legacy caller-owned transaction contract; callers must
 * validate inputs, handle failure and decide compensation before saving. Queries never imply a save.
 */
UCLASS()
class UAudioAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Legacy migration primitive: joins the caller transaction, or opens a standalone one.
     * A null result aborts the caller's workflow; callers own recovery of other migration steps.
     */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static UWandererAudioSourceComponent* AttachRoomAudioComponent(
        AActor* RoomActor, UWandererAudioSourceComponent* TemplateSource);

    /** Caller owns the encompassing shape/profile transaction and handles partial failure.
     * ValidateSurfaceAudioSettings/Target are preflight only; recheck false before any save.
     */
    UFUNCTION(BlueprintCallable, Category = "World Audio|Editor")
    static bool ConfigureSurfaceAudio(UWandererAudioSourceComponent* Source,
        FDataTableRowHandle Template,
        UAkAudioEvent* Event,
        FName ResourceGroup,
        bool bEnabled);

    UFUNCTION(
        BlueprintCallable, Category = "Wanderer|Editor", meta = (WorldContext = "WorldContext"))
    static AVolume* CreateAcousticBox(UObject* WorldContext,
        TSubclassOf<AVolume> VolumeClass,
        FVector HalfExtent,
        FTransform Transform);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static UWandererBuildingAmbienceComponent* CreateBuildingAmbience(
        const TArray<AActor*>& Rooms, FName Name, UAkAudioEvent* Event);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool DeleteBuildingAmbience(UWandererBuildingAmbienceComponent* Building);

    /** Low-level write: caller owns the transaction and recovery of newly created rows. */
    static FDataTableRowHandle FindOrAddWorldAudioProfile(
        UDataTable* Table, const FWandererWorldAudioProfileRow& Row, UAkAudioEvent* Event,
        FName InRowName = NAME_None);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static TArray<FString> FindRoomAudioActorReferences(AActor* Target);

    UFUNCTION(
        BlueprintCallable, Category = "Wanderer|Editor", meta = (WorldContext = "WorldContext"))
    static FString GetAcousticRoomAtLocation(UObject* WorldContext, FVector Position);

    /** Returns the number actually moved; partial completion is possible. Caller owns Undo/save. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static int32 MoveAudioActorsToPersistent(const TArray<AActor*>& Actors);

    /** Brush rebuild primitives require caller-owned Undo/recovery; false never authorizes a save. */
    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool RebuildAcousticFloorRoom(
        AVolume* Volume, const TArray<FVector2D>& Outline, float HalfHeight);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool RebuildConvexAcousticRoom(
        AVolume* Volume, const TArray<FVector2D>& Outline, float HalfLength);

    UFUNCTION(BlueprintCallable, Category = "Wanderer|Editor")
    static bool RebuildSteppedAcousticRoom(
        AVolume* Volume, FVector HalfExtent, float Extension, float StepZ);

    UFUNCTION(BlueprintCallable, Category = "World Audio|Editor")
    static FString ValidateSurfaceAudioSettings(
        FDataTableRowHandle Template, UAkAudioEvent* Event, bool bSpline);

    UFUNCTION(BlueprintCallable, Category = "World Audio|Editor")
    static FString ValidateSurfaceAudioTarget(AActor* Target);
};
