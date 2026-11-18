#if WITH_DEV_AUTOMATION_TESTS
#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkSpatialAudioVolume.h"
#include "Editor.h"
#include "Features/AudioManager/Authoring/Library/AudioAuthoringLibrary.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/WorldAudio/WandererBuildingAmbienceComponent.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Tests/AutomationEditorCommon.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

namespace
{
    bool VerifyBuildingProbeMute(FAutomationTestBase& Test)
    {
        auto* Engine = IWwiseSoundEngineAPI::Get();
        if (!FParse::Param(FCommandLine::Get(), TEXT("BuildingAmbienceMutedProbe")) || !Engine
            || !FAkAudioDevice::IsInitialized() || Engine->SetOutputVolume(0, 0) != AK_Success)
        { Test.AddError(TEXT("Requires isolated initialized -BuildingAmbienceMutedProbe process; cannot verify native output mute")); return false; }
        return true;
    }

    TArray<AActor*> CreateBuildingProbeRooms(UWorld& World, UAkAudioEvent* Event)
    {
        TArray<AActor*> Rooms;
        for (float X : {0.f, 1000.f})
        {
            auto* Room = Cast<AAkSpatialAudioVolume>(UAudioAuthoringLibrary::CreateAcousticBox(&World, AAkSpatialAudioVolume::StaticClass(), FVector(250), FTransform(FVector(X, 0, 0))));
            if (!Room) return {};
            Room->Room->bEnable = true; Room->Room->AutoPost = false; Room->Room->AkAudioEvent = Event;
            Room->Room->UnregisterComponent(); Room->Room->RegisterComponent(); Rooms.Add(Room);
        }
        return Rooms;
    }

    // Framework owns this command and its test for the entire synchronous game-thread probe.
    class FBuildingPlaybackProbe : public IAutomationLatentCommand
    {
    public:
        explicit FBuildingPlaybackProbe(FAutomationTestBase* InTest) : Test(InTest), Deadline(FPlatformTime::Seconds() + 45) {}
        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Muted building playback probe timed out")); return true; }
            UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
            auto* Player = World ? World->GetFirstPlayerController() : nullptr;
            if (!Player || !Player->GetPawn()) return false;
            auto* System = World->GetSubsystem<UWandererAudioSubsystem>();
            if (Step == -1)
            {
                for (TActorIterator<AWandererWorldAudioBudget> It(World); It; ++It) Building = It->FindComponentByClass<UWandererBuildingAmbienceComponent>();
                if (!Building.IsValid()) { Test->AddError(TEXT("PIE building component missing")); return true; }
                Step = 0; Since = World->GetTimeSeconds();
            }
            if (Step < 5 && !Building.IsValid()) { Test->AddError(TEXT("Building destroyed before probe step")); return true; }
            const FVector Positions[] = { FVector(5000, 0, 0), FVector::ZeroVector, FVector(1000, 0, 0), FVector(5000, 0, 0), FVector::ZeroVector, FVector::ZeroVector };
            Player->GetPawn()->SetActorLocation(Positions[Step], false, nullptr, ETeleportType::TeleportPhysics);
            if (Step == 3 && FirstID && Building->GetPlayingID() == 0)
            {
                if (auto* Event = Building->BuildingEvent.Get())
                {
                    bObservedTailLease |= !FAkAudioDevice::Get()->IsPlayingIDActive(Event->GetShortID(), FirstID)
                        && System->IsEventDataResident(Event);
                }
            }
            if (World->GetTimeSeconds() - Since < 2) return false;
            if (Step == 0) Test->TestEqual(TEXT("Outside: no post"), Building->GetPlayingID(), 0);
            if (Step == 1)
            {
                FirstID = Building->GetPlayingID();
                Test->TestTrue(TEXT("Inside: one live post"), FirstID != 0 && Building->IsPlaying());
                Test->TestTrue(TEXT("PIE membership path resolved"), System->IsListenerInsideBuilding(Building.Get()));
                EmitterWeakPtr = FindEmitter(FirstID);
                Test->TestTrue(TEXT("Playback enables emitter Tick"),
                    EmitterWeakPtr.IsValid() && EmitterWeakPtr->IsComponentTickEnabled());
            }
            if (Step == 2) Test->TestEqual(TEXT("Member transition retains Playing ID"), Building->GetPlayingID(), FirstID);
            if (Step == 3)
            {
                Test->TestEqual(TEXT("Outside: fade completed"), Building->GetPlayingID(), 0);
                Test->TestTrue(TEXT("Lease survives Wwise playing ID during tail hold"), bObservedTailLease);
                auto* Event = LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_room.Play_Amb_room"));
                Test->TestFalse(TEXT("Last event lease released"), System->IsEventDataResident(Event));
                Test->TestTrue(TEXT("Exit retains the registered emitter with Tick disabled"),
                    EmitterWeakPtr.IsValid() && EmitterWeakPtr->IsRegistered()
                        && !EmitterWeakPtr->IsComponentTickEnabled());
            }
            if (Step == 4)
            {
                Test->TestTrue(TEXT("Re-entry starts a new live post"), Building->GetPlayingID() != 0 && Building->GetPlayingID() != FirstID && Building->IsPlaying());
                Test->TestTrue(TEXT("Re-entry uses the same emitter and restores Tick"),
                    EmitterWeakPtr.IsValid() && EmitterWeakPtr->IsComponentTickEnabled()
                        && FindEmitter(Building->GetPlayingID()) == EmitterWeakPtr.Get());
                LastID = Building->GetPlayingID();
                if (const auto* Event = Building->BuildingEvent.Get()) EventID = Event->GetShortID();
                Building->GetOwner()->Destroy();
            }
            if (Step == 5)
            {
                Test->TestEqual(TEXT("Native engine no longer has deleted owner playback"), IWwiseSoundEngineAPI::Get()->Query->GetEventIDFromPlayingID(LastID), AK_INVALID_UNIQUE_ID);
                Test->TestFalse(TEXT("Owner deletion stops playing ID"), FAkAudioDevice::Get()->IsPlayingIDActive(EventID, LastID));
                Test->TestEqual(TEXT("Owner deletion unregisters managed source"), System->GetRegisteredCount(), 0);
                Test->TestTrue(TEXT("Acoustic Rooms retained"), !FAkAudioDevice::Get()->FindRoomComponentsAtLocation(FVector::ZeroVector, World).IsEmpty());
                return true;
            }
            ++Step; Since = World->GetTimeSeconds(); return false;
        }
    private:
        static UAkComponent* FindEmitter(int32 InPlayingID)
        {
            const AkGameObjectID ID =
                IWwiseSoundEngineAPI::Get()->Query->GetGameObjectFromPlayingID(InPlayingID);
            return ID == AK_INVALID_GAME_OBJECT ? nullptr : UAkComponent::GetAkComponent(ID);
        }

        FAutomationTestBase* Test;
        TWeakObjectPtr<UWandererBuildingAmbienceComponent> Building;
        TWeakObjectPtr<UAkComponent> EmitterWeakPtr;
        double Deadline;
        float Since = 0;
        int32 Step = -1;
        int32 FirstID = 0;
        uint32 LastID = 0;
        uint32 EventID = 0;
        bool bObservedTailLease = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingAmbiencePlaybackTest, "Wanderer.Audio.Building.MutedPlayback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingAmbiencePlaybackTest::RunTest(const FString& Parameters)
{
    if (!VerifyBuildingProbeMute(*this)) return false;
    auto* Event = LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_room.Play_Amb_room"));
    if (!TestNotNull(TEXT("Original Room Event"), Event) || !TestFalse(TEXT("Manual resource ownership"), Event->bAutoLoad)) return false;
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    World->SpawnActor<AWandererWorldAudioBudget>();
    const auto Rooms = CreateBuildingProbeRooms(*World, Event);
    if (Rooms.Num() != 2) return false;
    auto* Building = UAudioAuthoringLibrary::CreateBuildingAmbience(Rooms, TEXT("TestBuilding"), Event);
    if (!Building) return false;
    Building->ReverbTailHoldSec = .75f;
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FBuildingPlaybackProbe(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    // This isolated automation process stays muted until exit; no global project settings changed.
    return true;
}

namespace
{
enum class ERoomLayerPlaybackCase { DistinctEvents, SharedLease };

class FRoomLayerPlaybackProbe : public IAutomationLatentCommand
{
public:
    FRoomLayerPlaybackProbe(FAutomationTestBase* InTest, ERoomLayerPlaybackCase InCase)
        : Test(InTest), ProbeCase(InCase), Deadline(FPlatformTime::Seconds() + 45) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Room additive playback timed out")); return true; }
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        auto* Player = World ? World->GetFirstPlayerController() : nullptr;
        if (!Player || !Player->GetPawn()) return false;
        if (Step == -1)
        {
            for (TActorIterator<AWandererWorldAudioBudget> It(World); It; ++It) Building = It->FindComponentByClass<UWandererBuildingAmbienceComponent>();
            for (TActorIterator<AAkSpatialAudioVolume> It(World); It; ++It)
                if (auto* Source = It->FindComponentByClass<UWandererAudioSourceComponent>()) Layer = Source;
            if (!Building.IsValid() || !Layer.IsValid()) { Test->AddError(TEXT("Additive PIE fixture missing")); return true; }
            CommonEvent = Building->BuildingEvent.Get(); SpecialEvent = Layer->GetProfile()->PlayEvent.Get();
            Step = 0; Since = World->GetTimeSeconds();
        }
        Player->GetPawn()->SetActorLocation(Step == 1 ? FVector(1000, 0, 0) : FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
        if (World->GetTimeSeconds() - Since < 2) return false;
        auto* System = World->GetSubsystem<UWandererAudioSubsystem>();
        auto* Engine = IWwiseSoundEngineAPI::Get();
        const bool bSharedLease = ProbeCase == ERoomLayerPlaybackCase::SharedLease;
        const bool bNeedBuilding = Step < 3 || (Step == 3 && !bSharedLease);
        const bool bNeedLayer = Step < 3 || (Step == 3 && bSharedLease);
        if (!System || !Engine || !CommonEvent.IsValid() || !SpecialEvent.IsValid()
            || (bNeedBuilding && !Building.IsValid()) || (bNeedLayer && !Layer.IsValid()))
        { Test->AddError(TEXT("Additive fixture lifetime ended unexpectedly")); return true; }
        switch (Step)
        {
        case 0:
            CommonID = Building->GetPlayingID(); SpecialID = Layer->GetPlayingID();
            Test->TestTrue(TEXT("Building and Room post concurrently"), CommonID != 0 && SpecialID != 0 && CommonID != SpecialID);
            Test->TestEqual(TEXT("Native common Event active"), Engine->Query->GetEventIDFromPlayingID(CommonID), CommonEvent->GetShortID());
            Test->TestEqual(TEXT("Native special Event active"), Engine->Query->GetEventIDFromPlayingID(SpecialID), SpecialEvent->GetShortID());
            Test->TestEqual(TEXT("Both sources registered"), System->GetRegisteredCount(), 2);
            break;
        case 1:
            Test->TestEqual(TEXT("Shared bed continuous in another member"), Building->GetPlayingID(), CommonID);
            Test->TestEqual(TEXT("Room layer stops outside its own range"), Layer->GetPlayingID(), 0);
            Test->TestTrue(TEXT("Common lease survives Room exit"), System->IsEventDataResident(CommonEvent.Get()));
            Test->TestEqual(TEXT("Room exit preserves only the surviving shared lease"), System->IsEventDataResident(SpecialEvent.Get()), bSharedLease);
            break;
        case 2:
            Test->TestEqual(TEXT("Re-entry does not restart shared bed"), Building->GetPlayingID(), CommonID);
            Test->TestTrue(TEXT("Special layer restarts independently"), Layer->GetPlayingID() != 0 && Layer->GetPlayingID() != SpecialID);
            SpecialID = Layer->GetPlayingID();
            if (bSharedLease) Building->GetOwner()->Destroy();
            else Layer->DestroyComponent();
            break;
        case 3:
            Test->TestEqual(TEXT("Only surviving source remains"), System->GetRegisteredCount(), 1);
            if (bSharedLease)
            {
                Test->TestEqual(TEXT("Building deletion leaves Room playback"), Layer->GetPlayingID(), SpecialID);
                Test->TestEqual(TEXT("Surviving Room native playback retains shared Event"), Engine->Query->GetEventIDFromPlayingID(SpecialID), CommonEvent->GetShortID());
                Test->TestEqual(TEXT("Deleted building native ID removed"), Engine->Query->GetEventIDFromPlayingID(CommonID), AK_INVALID_UNIQUE_ID);
                Test->TestTrue(TEXT("Shared Event remains resident for Room layer"), System->IsEventDataResident(CommonEvent.Get()));
                Layer->DestroyComponent();
            }
            else
            {
                Test->TestEqual(TEXT("Deleting Room layer leaves shared playback"), Building->GetPlayingID(), CommonID);
                Test->TestEqual(TEXT("Deleted special native ID removed"), Engine->Query->GetEventIDFromPlayingID(SpecialID), AK_INVALID_UNIQUE_ID);
                Test->TestFalse(TEXT("Deleted special integration ID removed"), FAkAudioDevice::Get()->IsPlayingIDActive(SpecialEvent->GetShortID(), SpecialID));
                Test->TestTrue(TEXT("Room layer deletion keeps building membership"), System->IsListenerInsideBuilding(Building.Get()));
                Building->GetOwner()->Destroy();
            }
            break;
        case 4:
            Test->TestEqual(TEXT("Building owner cleanup removes native playback"), Engine->Query->GetEventIDFromPlayingID(CommonID), AK_INVALID_UNIQUE_ID);
            Test->TestEqual(TEXT("Room layer cleanup removes native playback"), Engine->Query->GetEventIDFromPlayingID(SpecialID), AK_INVALID_UNIQUE_ID);
            Test->TestEqual(TEXT("All sources unregistered"), System->GetRegisteredCount(), 0);
            Test->TestFalse(TEXT("Common last lease released"), System->IsEventDataResident(CommonEvent.Get()));
            Test->TestFalse(TEXT("Special last lease released"), System->IsEventDataResident(SpecialEvent.Get()));
            Test->TestTrue(TEXT("Spatial Room survives both layers"), !FAkAudioDevice::Get()->FindRoomComponentsAtLocation(FVector::ZeroVector, World).IsEmpty());
            return true;
        }
        ++Step; Since = World->GetTimeSeconds();
        return false;
    }
private:
    FAutomationTestBase* Test; // Owned by the automation framework until the queued command finishes.
    ERoomLayerPlaybackCase ProbeCase;
    TWeakObjectPtr<UWandererBuildingAmbienceComponent> Building;
    TWeakObjectPtr<UWandererAudioSourceComponent> Layer;
    TWeakObjectPtr<UAkAudioEvent> CommonEvent;
    TWeakObjectPtr<UAkAudioEvent> SpecialEvent;
    double Deadline;
    float Since = 0;
    int32 Step = -1;
    int32 CommonID = 0;
    int32 SpecialID = 0;
};

bool StartRoomLayerPlaybackTest(FAutomationTestBase& Test, ERoomLayerPlaybackCase ProbeCase)
{
    if (!VerifyBuildingProbeMute(Test)) return false;
    auto* CommonEvent = LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_room.Play_Amb_room"));
    auto* SpecialEvent = ProbeCase == ERoomLayerPlaybackCase::SharedLease ? CommonEvent
        : LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_warehouse.Play_Amb_warehouse"));
    if (!CommonEvent || !SpecialEvent || CommonEvent->bAutoLoad || SpecialEvent->bAutoLoad)
    { Test.AddError(TEXT("Additive test requires existing manually loaded room/warehouse Events")); return false; }
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    World->SpawnActor<AWandererWorldAudioBudget>();
    const auto Rooms = CreateBuildingProbeRooms(*World, CommonEvent);
    if (Rooms.Num() != 2) return false;
    if (!UAudioAuthoringLibrary::CreateBuildingAmbience(Rooms, TEXT("AdditiveBuilding"), CommonEvent)) return false;
    auto* Table = NewObject<UDataTable>(); Table->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
    FWandererWorldAudioProfileRow Row; Row.Mode = EWandererWorldAudioMode::Room; Row.PlayEvent = SpecialEvent;
    Row.AudibleDistanceM = 1; Row.ActivationDistanceM = 1; Row.ReleaseDistanceM = 3;
    Table->AddRow(TEXT("RoomSpecial"), Row);
    auto* Layer = UAudioAuthoringLibrary::AttachRoomAudioComponent(Rooms[0], nullptr);
    if (!Layer) return false;
    Layer->Profile.DataTable = Table; Layer->Profile.RowName = TEXT("RoomSpecial");
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FRoomLayerPlaybackProbe(&Test, ProbeCase));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomAdditivePlaybackTest, "Wanderer.Audio.Building.MutedRoomAdditive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomAdditivePlaybackTest::RunTest(const FString& Parameters)
{
    return StartRoomLayerPlaybackTest(*this, ERoomLayerPlaybackCase::DistinctEvents);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSharedLeasePlaybackTest, "Wanderer.Audio.Building.MutedRoomSharedLease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomSharedLeasePlaybackTest::RunTest(const FString& Parameters)
{
    return StartRoomLayerPlaybackTest(*this, ERoomLayerPlaybackCase::SharedLease);
}

namespace
{
class FBuildingResourceProbe : public IAutomationLatentCommand
{
public:
    explicit FBuildingResourceProbe(FAutomationTestBase* InTest) : Test(InTest), Deadline(FPlatformTime::Seconds() + 45) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Building resource probe timed out")); return true; }
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        auto* Player = World ? World->GetFirstPlayerController() : nullptr;
        if (!Player || !Player->GetPawn()) return false;
        if (Step == -1)
        {
            for (TActorIterator<AWandererWorldAudioBudget> It(World); It; ++It) Building = It->FindComponentByClass<UWandererBuildingAmbienceComponent>();
            for (TActorIterator<AAkSpatialAudioVolume> It(World); It; ++It) if (It->EncompassesPoint(FVector(1000, 0, 0))) SecondRoom = *It;
            if (!Building.IsValid() || !SecondRoom.IsValid()) { Test->AddError(TEXT("Resource fixture missing")); return true; }
            Event = Building->BuildingEvent.Get(); Step = 0; Since = World->GetTimeSeconds();
        }
        if (!Building.IsValid() || !SecondRoom.IsValid() || !Event.IsValid()) { Test->AddError(TEXT("Resource fixture destroyed early")); return true; }
        const FVector Position = Step == 1 ? FVector(1000, 0, 0) : Step == 3 ? FVector(10000, 0, 0) : FVector::ZeroVector;
        Player->GetPawn()->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        if (World->GetTimeSeconds() - Since < 2) return false;
        auto* System = World->GetSubsystem<UWandererAudioSubsystem>();
        if (Step == 0 || Step == 2)
        {
            Test->TestEqual(TEXT("Group Active allows building"), System->GetResourceGroupStatus(TEXT("City")), FString(TEXT("Active")));
            Test->TestTrue(TEXT("Grouped building has live post"), Building->GetPlayingID() != 0 && Building->IsPlaying());
            Test->TestEqual(TEXT("Building consumes group admission"), System->GetResourceGroupAdmitted(TEXT("City")), 1);
            if (Step == 2) Test->TestTrue(TEXT("Reactivation creates fresh post"), Building->GetPlayingID() != FirstID);
            FirstID = Building->GetPlayingID();
        }
        else
        {
            Test->TestEqual(TEXT("Group drains to Dormant without circular wait"), System->GetResourceGroupStatus(TEXT("City")), FString(TEXT("Dormant")));
            Test->TestEqual(TEXT("Grouped building fully stopped"), Building->GetPlayingID(), 0);
            Test->TestFalse(TEXT("Building and preload leases fully released"), System->IsEventDataResident(Event.Get()));
            Test->TestFalse(TEXT("Member Room registration suspended"), SecondRoom->Room->IsRegistered());
            if (Step == 1) Test->TestTrue(TEXT("Gate works even while geometrically inside member"), SecondRoom->EncompassesPoint(Position));
            if (Step == 3)
            {
                Test->TestEqual(TEXT("Far departure removes native playback"), IWwiseSoundEngineAPI::Get()->Query->GetEventIDFromPlayingID(FirstID), AK_INVALID_UNIQUE_ID);
                return true;
            }
        }
        ++Step; Since = World->GetTimeSeconds(); return false;
    }
private:
    FAutomationTestBase* Test; // Framework owns the test until this bounded command completes.
    TWeakObjectPtr<UWandererBuildingAmbienceComponent> Building;
    TWeakObjectPtr<AAkSpatialAudioVolume> SecondRoom;
    TWeakObjectPtr<UAkAudioEvent> Event;
    double Deadline;
    float Since = 0;
    int32 Step = -1;
    int32 FirstID = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingResourcePlaybackTest, "Wanderer.Audio.Building.MutedResources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBuildingResourcePlaybackTest::RunTest(const FString& Parameters)
{
    if (!VerifyBuildingProbeMute(*this)) return false;
    auto* Event = LoadObject<UAkAudioEvent>(nullptr, TEXT("/Game/WwiseAudio/Events/Ambience/Play_Amb_room.Play_Amb_room"));
    if (!Event || Event->bAutoLoad) { AddError(TEXT("Manual Room Event required")); return false; }
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    auto* Budget = World->SpawnActor<AWandererWorldAudioBudget>();
    const auto Rooms = CreateBuildingProbeRooms(*World, Event);
    if (Rooms.Num() != 2) return false;
    FWandererAudioResourceGroup Group; Group.Name = TEXT("City"); Group.BoundsExtent = FVector(100);
    Group.LoadDistanceM = 3; Group.UnloadDistanceM = 4; Group.UnloadDelaySec = .1f; Group.MaxEvents = 1;
    for (auto* Room : Rooms) Group.Rooms.Add(Room);
    Group.PreloadEvents.Add(Event); Budget->ResourceGroups.Add(Group);
    auto* Building = UAudioAuthoringLibrary::CreateBuildingAmbience(Rooms, TEXT("GroupedBuilding"), Event);
    if (!Building) return false;
    Building->ResourceGroup = TEXT("City");
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FBuildingResourceProbe(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
