#pragma once

#include "CoreMinimal.h"

#include "Types/Audio/AudioEmitterTypes.h"
#include "RadioControlTypes.generated.h"

UENUM(BlueprintType)
enum class EWandererSpeakerItem : uint8
{
    Automatic,
    Radio,
    Mounted
};

enum class ERadioPlaybackPhase : uint8
{
    Stopped,
    LoadingMedia,
    WaitingForEmitters,
    WaitingForListener,
    Ready
};

enum class ERadioContent : uint8
{
    Song,
    Noise,
    Hiss
};

enum class ERadioChannel : uint8
{
    None,
    Stereo,
    Left,
    Right,
    Center
};

USTRUCT(BlueprintType)
struct FWandererSpeakerOption
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaker")
    FName ID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speaker")
    FString Label;
};

/** Values only: UI never retains the world entity producing this snapshot. */
USTRUCT(BlueprintType)
struct FRadioDeviceSnapshot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FGuid ID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FName ContentID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    EWandererSpeakerItem Item = EWandererSpeakerItem::Radio;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    int32 Number = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bPlaying = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bRetrievable = false;
};

USTRUCT(BlueprintType)
struct FRadioControlSnapshot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    TArray<FRadioDeviceSnapshot> Devices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FGuid SelectedID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FName SelectedContentID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FString Status;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    int32 AvailableRadios = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    int32 AvailableMounted = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bAvailable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bVisible = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bOpen = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bPlacing = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    bool bCanConfirm = false;
};

UENUM(BlueprintType)
enum class ERadioControlAction : uint8
{
    BeginPlacement,
    CancelPlacement,
    ConfirmPlacement,
    ConfirmFromPointer,
    RotatePlacement,
    PreviewSurface,
    SelectAtCursor,
    CancelSelection,
    SelectDevice,
    SelectContent,
    TogglePlayback,
    RetrieveSelected,
    RetrieveAll,
    OpenPanel,
    ClosePanel,
    CursorEntered,
    CursorLeft
};

/** One synchronous command. Device identity never aliases a reused inventory slot. */
USTRUCT(BlueprintType)
struct FRadioControlRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    ERadioControlAction Action = ERadioControlAction::ClosePanel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FVector SurfacePoint = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FVector SurfaceNormal = FVector::UpVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FGuid DeviceID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    FName ContentID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio")
    EWandererSpeakerItem Item = EWandererSpeakerItem::Automatic;
};

/** Acoustic values supplied by an entity; the audio system knows no entity class. */
struct FRadioEmitterSnapshot
{
    FTransform Transform;
    FName ContentID;
    int32 Number = 0;
    bool bPlaying = false;
    FObjectAudioEmitterHandle EmitterHandle;
};
