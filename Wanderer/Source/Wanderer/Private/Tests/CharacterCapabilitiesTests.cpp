#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "Components/CharacterCameraZoomComponent.h"
#include "Components/CharacterFillLightingComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterCameraLifetimeTest,
    "Wanderer.Character.CameraLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterCameraLifetimeTest::RunTest(const FString& InParameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    APawn* Pawn = World->SpawnActor<APawn>();
    APlayerController* Player = World->SpawnActor<APlayerController>();
    Player->Possess(Pawn);
    USpringArmComponent* Arm = NewObject<USpringArmComponent>(Pawn);
    UCharacterCameraZoomComponent* Zoom = NewObject<UCharacterCameraZoomComponent>(Pawn);
    Arm->TargetArmLength = 400.0f;
    Zoom->Initialize(Arm, {200.0f, 800.0f, 40.0f, 12.0f});
    Zoom->ApplyWheelInput(1.0f);
    TestEqual(TEXT("Positive wheel moves closer"), Zoom->GetTargetDistance(), 360.0f);
    Zoom->Update(1.0f);
    TestEqual(TEXT("Actor-driven update reaches target"), Arm->TargetArmLength, 360.0f);
    Player->SetIgnoreLookInput(true);
    Zoom->ApplyWheelInput(1.0f);
    TestEqual(TEXT("Modal look suppression blocks zoom"), Zoom->GetTargetDistance(), 360.0f);
    Player->SetIgnoreLookInput(false);
    Zoom->ApplyWheelInput(100.0f);
    TestEqual(TEXT("Minimum clamp"), Zoom->GetTargetDistance(), 200.0f);
    Zoom->ApplyWheelInput(-100.0f);
    TestEqual(TEXT("Maximum clamp"), Zoom->GetTargetDistance(), 800.0f);
    Zoom->Stop();
    Zoom->ApplyWheelInput(1.0f);
    Zoom->Update(1.0f);
    TestEqual(TEXT("Stop releases the arm"), Arm->TargetArmLength, 360.0f);
    Zoom->Initialize(Arm, {-1.0f, -2.0f, -3.0f, -4.0f});
    TestEqual(TEXT("Invalid bounds sanitized"), Zoom->GetTargetDistance(), 50.0f);
    TestEqual(TEXT("Invalid speed sanitized"), Zoom->GetConfig().InterpolationSpeed, 1.0f);
    APawn* Other = World->SpawnActor<APawn>();
    USpringArmComponent* OtherArm = NewObject<USpringArmComponent>(Other);
    OtherArm->TargetArmLength = 300.0f;
    Zoom->Initialize(OtherArm, {});
    Zoom->Update(1.0f);
    TestEqual(TEXT("Foreign arm untouched"), OtherArm->TargetArmLength, 300.0f);
    Zoom->Initialize(Arm, {});
    Player->UnPossess();
    const float Before = Arm->TargetArmLength;
    Zoom->ApplyWheelInput(1.0f);
    Zoom->Update(1.0f);
    TestEqual(TEXT("Unpossessed pawn cannot update zoom"), Arm->TargetArmLength, Before);
    Arm->DestroyComponent();
    Zoom->Update(1.0f);
    TestTrue(TEXT("Destroyed observed arm is safe"), IsValid(Zoom));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterFillLifetimeTest,
    "Wanderer.Character.FillLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterFillLifetimeTest::RunTest(const FString& InParameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    APawn* Pawn = World->SpawnActor<APawn>();
    UCameraComponent* Camera = NewObject<UCameraComponent>(Pawn);
    UPointLightComponent* Key = NewObject<UPointLightComponent>(Pawn);
    UPointLightComponent* Fill = NewObject<UPointLightComponent>(Pawn);
    USpotLightComponent* View = NewObject<USpotLightComponent>(Pawn);
    UCharacterFillLightingComponent* Lighting = NewObject<UCharacterFillLightingComponent>(Pawn);
    Lighting->Initialize(Camera, Key, Fill, View);
    Lighting->SetEnabled(true);
    Lighting->Update(1.0f, true, 48.0f, 35.0f);
    TestEqual(TEXT("Dark scene requests full body fill"), Key->Intensity, 48.0f);
    TestEqual(TEXT("Secondary fill ratio preserved"), Fill->Intensity, 24.0f);
    TestEqual(TEXT("Dark scene requests view fill"), View->Intensity, 35.0f);
    Lighting->Update(1.0f, false, 48.0f, 35.0f);
    TestFalse(TEXT("Losing local control hides fill"), Key->IsVisible());
    Lighting->Update(1.0f, true, 48.0f, 35.0f);
    TestTrue(TEXT("Repossessing restores fill"), Key->IsVisible());
    APawn* Other = World->SpawnActor<APawn>();
    UPointLightComponent* Foreign = NewObject<UPointLightComponent>(Other);
    Foreign->SetIntensity(123.0f);
    Lighting->Initialize(Camera, Foreign, Fill, View);
    Lighting->Update(1.0f, true, 48.0f, 35.0f);
    TestEqual(TEXT("Foreign light not adopted"), Foreign->Intensity, 123.0f);
    TestFalse(TEXT("Rebinding releases old lights"), Key->IsVisible());
    Lighting->Initialize(Camera, Key, Fill, View);
    Key->DestroyComponent();
    Lighting->Update(1.0f, true, 48.0f, 35.0f);
    TestFalse(TEXT("Missing key disables remaining fill"), Fill->IsVisible());
    Lighting->SetEnabled(false);
    TestFalse(TEXT("Disable hides view light"), View->IsVisible());
    return true;
}

#endif
