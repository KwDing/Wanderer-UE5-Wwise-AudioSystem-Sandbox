#pragma once

// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"

#include "Interfaces/CharacterPresentationProvider.h"
#include "WandererCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UInputComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config = Game)
class AWandererCharacter : public ACharacter, public ICharacterPresentationProvider
{
    GENERATED_BODY()

  public:
    AWandererCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual UCameraComponent* GetCharacterCamera() const override;
    virtual USkeletalMeshComponent* GetPresentationMesh() const override;
    virtual const TArray<FWandererAppearancePart>& GetAppearanceParts() const override;
    virtual void PrepareForMenu() override;

    /** Returns CameraBoom subobject **/
    FORCEINLINE USpringArmComponent* GetCameraBoom() const
    {
        return CameraBoom;
    }
    /** Returns FollowCamera subobject **/
    FORCEINLINE UCameraComponent* GetFollowCamera() const
    {
        return FollowCamera;
    }

  protected:
    /** Called for movement input */
    void Move(const FInputActionValue& Value);

    /** Called for looking input */
    void Look(const FInputActionValue& Value);

    virtual void NotifyControllerChanged() override;
    virtual void UnPossessed() override;

    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

  private:
    /** Camera boom positioning the camera behind the character */
    UPROPERTY(
        VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<USpringArmComponent> CameraBoom;

    /** Follow camera */
    UPROPERTY(
        VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UCameraComponent> FollowCamera;

    /** MappingContext */
    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    /** Jump Input Action */
    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputAction> JumpAction;

    /** Move Input Action */
    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputAction> MoveAction;

    /** Look Input Action */
    UPROPERTY(
        EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UInputAction> LookAction;
};
