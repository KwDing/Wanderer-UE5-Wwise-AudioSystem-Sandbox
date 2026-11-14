#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "Systems/WandererEnvironmentSubsystem.h"
#include "UObject/CoreRedirects.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/MemberReference.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSubsystemRedirectTest, "Wanderer.Audio.Subsystems.Redirects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSubsystemRedirectTest::RunTest(const FString& Parameters)
{
    struct FClassCase { const TCHAR* OldPath; UClass* Expected; };
    const FClassCase Classes[] = {
        {TEXT("/Script/Wanderer.WandererAmbienceSubsystem"), UWandererEnvironmentSubsystem::StaticClass()},
        {TEXT("/Script/Wanderer.WandererWorldAudioSubsystem"), UWandererAudioSubsystem::StaticClass()},
        {TEXT("/Script/Wanderer.WandererAudioEnvironmentSubsystem"), UWandererAudioSubsystem::StaticClass()}
    };
    for (const FClassCase& Entry : Classes)
    {
        const auto Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(FString(Entry.OldPath)));
        TestEqual(Entry.OldPath, Redirected.ToString(), Entry.Expected->GetPathName());
        FSoftClassPath ClassPath(Entry.OldPath);
        TestTrue(TEXT("Old soft class path applies its core redirect"), ClassPath.FixupCoreRedirects());
        TestEqual(TEXT("Redirected soft class path loads the new native class"), ClassPath.TryLoadClass<UWorldSubsystem>(), Entry.Expected);
    }
    FMemberReference EnvironmentGetter;
    EnvironmentGetter.SetExternalMember(TEXT("GetAmbienceContext"), UWandererEnvironmentSubsystem::StaticClass());
    TestEqual(TEXT("Old getter resolves after its parent class redirect"), EnvironmentGetter.ResolveMember<UFunction>(UWandererEnvironmentSubsystem::StaticClass()),
        UWandererEnvironmentSubsystem::StaticClass()->FindFunctionByName(TEXT("GetEnvironmentSubsystem")));
    FMemberReference AudioGetter;
    AudioGetter.SetExternalMember(TEXT("GetAudioEnvironment"), UWandererAudioSubsystem::StaticClass());
    TestEqual(TEXT("Merged service getter resolves after its parent class redirect"), AudioGetter.ResolveMember<UFunction>(UWandererAudioSubsystem::StaticClass()),
        UWandererAudioSubsystem::StaticClass()->FindFunctionByName(TEXT("GetAudioSubsystem")));
    TestNotNull(TEXT("Merged reflected capture function exists"), UWandererAudioSubsystem::StaticClass()->FindFunctionByName(TEXT("CaptureSpaceInfo")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioSubsystemWorldIsolationTest, "Wanderer.Audio.Subsystems.WorldIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAudioSubsystemWorldIsolationTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
    UWorld* First = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnvironmentIsolationA"), nullptr, true, ERHIFeatureLevel::Num, &Values);
    UWorld* Second = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnvironmentIsolationB"), nullptr, true, ERHIFeatureLevel::Num, &Values);
    ON_SCOPE_EXIT { if (Second) Second->DestroyWorld(false); if (First) First->DestroyWorld(false); };
    if (!TestNotNull(TEXT("First world"), First) || !TestNotNull(TEXT("Second world"), Second)) return false;
    auto* FirstEnvironment = UWandererEnvironmentSubsystem::GetEnvironmentSubsystem(First);
    auto* SecondEnvironment = UWandererEnvironmentSubsystem::GetEnvironmentSubsystem(Second);
    auto* FirstAudio = UWandererAudioSubsystem::GetAudioSubsystem(First);
    auto* SecondAudio = UWandererAudioSubsystem::GetAudioSubsystem(Second);
    if (!TestNotNull(TEXT("First environment"), FirstEnvironment) || !TestNotNull(TEXT("Second environment"), SecondEnvironment)
        || !TestNotNull(TEXT("First audio"), FirstAudio) || !TestNotNull(TEXT("Second audio"), SecondAudio)) return false;
    TestTrue(TEXT("Environment instances are world-local"), FirstEnvironment != SecondEnvironment);
    TestTrue(TEXT("Audio instances are world-local"), FirstAudio != SecondAudio);
    TestFalse(TEXT("Default environment is day"), FirstEnvironment->IsNight());
    FirstEnvironment->SetNight(true);
    TestTrue(TEXT("Night is retained in its world"), FirstEnvironment->IsNight());
    TestFalse(TEXT("Night does not leak into the other world"), SecondEnvironment->IsNight());
    TestEqual(TEXT("Static audio accessor returns the existing sole instance"), FirstAudio, First->GetSubsystem<UWandererAudioSubsystem>());
    TArray<FWandererSpaceDirection> Directions;
    Directions.AddDefaulted();
    TestFalse(TEXT("Invalid pawn rejected by merged query"), FirstAudio->CaptureSpaceInfo(nullptr, 20, Directions));
    TestEqual(TEXT("Invalid query clears stale output"), Directions.Num(), 0);
    return true;
}
#endif
