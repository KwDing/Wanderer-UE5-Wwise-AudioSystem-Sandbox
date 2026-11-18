#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#include "AkAudioDevice.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"

#include "Actors/Audio/WandererWorldAudioBudget.h"
#include "Components/WorldAudio/WandererAudioSourceComponent.h"
#include "Data/Audio/WandererObjectAudioProfile.h"
#include "Systems/Audio/WandererAudioSubsystem.h"
#include "Systems/Audio/WwiseAudioBackend.h"

namespace
{
    const FName RuralProfileNames[] = {TEXT("AMB_GRASS_DAY"), TEXT("AMB_SNOW"),
        TEXT("AMB_POND_STILL"), TEXT("AMB_WATER_SPLINE"), TEXT("AMB_WAREHOUSE")};
    const FVector RuralSamplePositions[] = {FVector(-492, -5151, 110),
        FVector(-1250, -6100, 110), FVector(-350, -4341, 90),
        FVector(974, -8890, 110), FVector(1950, -5180, 110)};

    struct FRuralSourceSnapshot
    {
        TWeakObjectPtr<UWandererAudioSourceComponent> SourceWeakPtr;
        FDataTableRowHandle Profile;
        TStrongObjectPtr<const UDataTable> Table;
        float Bias = 0;
        bool bEnabled = false;
    };

    struct FRuralBudgetFixture
    {
        UWorld* World = nullptr;
        UWandererAudioSubsystem* Audio = nullptr;
        TWeakObjectPtr<AWandererWorldAudioBudget> BudgetWeakPtr;
        FAudioBudgetLimits SavedLimits;
        int32 RadioEntryPoolCapacity = 0;
        TWeakObjectPtr<APlayerController> PlayerWeakPtr;
        TWeakObjectPtr<APawn> PreviousPawnWeakPtr;
        TWeakObjectPtr<APawn> PawnWeakPtr;
        TWeakObjectPtr<UAkComponent> ListenerWeakPtr;
        TWeakObjectPtr<UAkComponent> PreviousListenerWeakPtr;
        TArray<FRuralSourceSnapshot> Snapshots;
        TArray<TWeakObjectPtr<UWandererAudioSourceComponent>> Rural;
        TStrongObjectPtr<UDataTable> TestProfiles{NewObject<UDataTable>()};
        TArray<FObjectAudioEmitterHandle> Objects;
        TArray<TWeakObjectPtr<AActor>> ObjectOwners;
        TWeakObjectPtr<UAkAudioEvent> OutgoingEventWeakPtr;
        uint32 OutgoingID = 0;
        double DeadlineSec = FPlatformTime::Seconds() + 20;
        double PhaseAtSec = 0;
        int32 Phase = 0;
        FString Evidence;
        TArray<FRadioEmitterSnapshot> RadioSources;
        int32 RadioPositionMs = 0;
        FObjectAudioEmitterHandle Emergency;
        FObjectAudioEmitterHandle Expiring;
        FObjectAudioEmitterHandle InvalidEmergency;
        bool bCheckedRegularEmergency = false;

        bool Initialize()
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::Game && Context.World()
                    && Context.World()->GetMapName().Contains(TEXT("L_AudioSystemDemo")))
                {
                    World = Context.World();
                    break;
                }
            }
            Audio = World ? World->GetSubsystem<UWandererAudioSubsystem>() : nullptr;
            APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
            if (!Audio || !Player || !FAkAudioDevice::Get())
            {
                return false;
            }
            for (TActorIterator<AWandererWorldAudioBudget> It(World); It; ++It)
            {
                BudgetWeakPtr = *It;
                SavedLimits = It->GetAudioBudgetLimits();
                It->RegularSlots = 3;
                It->TransitionSlots = 2;
                It->EmergencySlots = 1;
                break;
            }
            if (!BudgetWeakPtr.IsValid())
            {
                return false;
            }
            Rural.SetNum(UE_ARRAY_COUNT(RuralProfileNames));
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                TInlineComponentArray<UWandererAudioSourceComponent*> Sources(*It);
                for (UWandererAudioSourceComponent* Source : Sources)
                {
                    Snapshots.Add({Source, Source->Profile,
                        TStrongObjectPtr<const UDataTable>(Source->Profile.DataTable.Get()),
                        Source->PriorityBias, Source->bEnabled});
                    Source->ShutdownPlayback();
                    Source->bEnabled = false;
                    for (int32 Index = 0; Index < Rural.Num(); ++Index)
                    {
                        if (Source->Profile.RowName == RuralProfileNames[Index])
                        {
                            Rural[Index] = Source;
                            Source->bEnabled = true;
                        }
                    }
                }
            }
            if (Rural.ContainsByPredicate([](const auto& Source) { return !Source.IsValid(); }))
            {
                return false;
            }
            PlayerWeakPtr = Player;
            PreviousPawnWeakPtr = Player->GetPawn();
            APawn* Pawn = World->SpawnActor<APawn>();
            PawnWeakPtr = Pawn;
            USceneComponent* Root = NewObject<USceneComponent>(Pawn);
            Pawn->SetRootComponent(Root);
            Pawn->AddInstanceComponent(Root);
            Root->RegisterComponent();
            // Wwise's editor build expects a default listener to belong to the camera manager.
            UAkComponent* Listener = NewObject<UAkComponent>(Player->PlayerCameraManager);
            Player->PlayerCameraManager->AddInstanceComponent(Listener);
            Listener->RegisterComponent();
            ListenerWeakPtr = Listener;
            PreviousListenerWeakPtr = FAkAudioDevice::Get()->GetSpatialAudioListener();
            FAkAudioDevice::Get()->AddDefaultListener(Listener);
            FAkAudioDevice::Get()->SetSpatialAudioListener(Listener);
            Player->Possess(Pawn);
            Player->SetPause(false);
            TestProfiles->RowStruct = FWandererWorldAudioProfileRow::StaticStruct();
            return true;
        }

        ~FRuralBudgetFixture()
        {
            if (AWandererWorldAudioBudget* Budget = BudgetWeakPtr.Get())
            {
                Budget->RegularSlots = SavedLimits.RegularSlots;
                Budget->TransitionSlots = SavedLimits.TransitionSlots;
                Budget->EmergencySlots = SavedLimits.EmergencySlots;
            }
            if (Audio)
            {
                Audio->ReleaseRadioPlayback(PawnWeakPtr.Get());
                for (const FObjectAudioEmitterHandle& Handle : Objects)
                {
                    Audio->UnregisterObjectEmitter(Handle);
                }
            }
            for (FRuralSourceSnapshot& Snapshot : Snapshots)
            {
                if (UWandererAudioSourceComponent* Source = Snapshot.SourceWeakPtr.Get())
                {
                    Source->ShutdownPlayback();
                    Source->Profile = Snapshot.Profile;
                    Source->PriorityBias = Snapshot.Bias;
                    Source->bEnabled = Snapshot.bEnabled;
                }
            }
            if (FAkAudioDevice* Device = FAkAudioDevice::Get(); Device && ListenerWeakPtr.IsValid())
            {
                Device->RemoveDefaultListener(ListenerWeakPtr.Get());
                Device->SetSpatialAudioListener(PreviousListenerWeakPtr.Get());
                ListenerWeakPtr->GetOwner()->RemoveInstanceComponent(ListenerWeakPtr.Get());
                ListenerWeakPtr->DestroyComponent();
            }
            if (APlayerController* Player = PlayerWeakPtr.Get())
            {
                Player->UnPossess();
                if (PreviousPawnWeakPtr.IsValid())
                {
                    Player->Possess(PreviousPawnWeakPtr.Get());
                }
            }
            if (PawnWeakPtr.IsValid())
            {
                PawnWeakPtr->Destroy();
            }
            for (const auto& OwnerWeakPtr : ObjectOwners)
            {
                if (OwnerWeakPtr.IsValid())
                {
                    OwnerWeakPtr->Destroy();
                }
            }
            FString EvidencePath;
            if (FParse::Value(FCommandLine::Get(), TEXT("AudioBudgetEvidence="), EvidencePath))
            {
                FFileHelper::SaveStringToFile(Evidence, *EvidencePath);
            }
        }

        void Pump(const FVector& InPosition)
        {
            PawnWeakPtr->SetActorLocation(InPosition);
            if (PlayerWeakPtr->GetPawn() != PawnWeakPtr.Get())
            {
                PlayerWeakPtr->Possess(PawnWeakPtr.Get());
            }
            Audio->Tick(0.25f);
            if (!RadioSources.IsEmpty())
            {
                TArray<FName> Failed;
                Audio->UpdateRadioPlayback(PawnWeakPtr.Get(), RadioSources, 0.05f, Failed);
            }
        }

        void Record(FAutomationTestBase& InTest, const FString& InLabel)
        {
            FString Text = InLabel + TEXT(" | ") + Audio->GetSummary();
            for (const auto& SourceWeakPtr : Rural)
            {
                Text += FString::Printf(TEXT("\n  %s distance=%.2f priority=%.2f id=%d %s"),
                    *SourceWeakPtr->Profile.RowName.ToString(), SourceWeakPtr->GetDistanceM(),
                    SourceWeakPtr->GetEffectivePriority(), SourceWeakPtr->GetPlayingID(),
                    *SourceWeakPtr->GetStatus());
            }
            for (int32 Index = 0; Index < Objects.Num(); ++Index)
            {
                const auto Stats = Audio->GetObjectEmitterStats(Objects[Index]);
                Text += FString::Printf(TEXT("\n  Object %d valid=%d configured=%d pending=%d ")
                    TEXT("playing=%d slots=%d admitted=%d score=%.2f failure=%d"),
                    Index, Objects[Index].IsSet(), Stats.ConfiguredEvents, Stats.PendingInstances,
                    Stats.PlayingInstances, Stats.PhysicalSlots, Stats.bAdmitted,
                    Stats.Priority, static_cast<int32>(Stats.LastFailure));
            }
            Evidence += Text + TEXT("\n");
            InTest.AddInfo(Text);
        }

        void SetContentionProfiles()
        {
            for (int32 Index = 0; Index < Rural.Num(); ++Index)
            {
                FWandererWorldAudioProfileRow Row = *Rural[Index]->GetProfile();
                Row.AudibleDistanceM = 1000;
                Row.ActivationDistanceM = 1000;
                Row.ReleaseDistanceM = 1001;
                Row.ProtectNearM = 0;
                Row.bProtectInsideRoom = false;
                Row.DistancePriorityPenalty = 0;
                Row.Priority = 80 - Index * 10;
                Row.FadeOutSec = 0.8f;
                Row.FadeInSec = 0.35f;
                TestProfiles->AddRow(RuralProfileNames[Index], Row);
                Rural[Index]->Profile.DataTable = TestProfiles.Get();
                Rural[Index]->PriorityBias = 0;
            }
        }

        int32 PostedSources() const
        {
            int32 Count = 0;
            for (const auto& SourceWeakPtr : Rural)
            {
                Count += SourceWeakPtr->IsPlaying();
            }
            return Count;
        }

        void SaveOutgoing(int32 InIndex)
        {
            const auto* Source = Rural[InIndex].Get();
            const auto* Row = Source->GetProfile();
            UAkAudioEvent* Night = Row->NightPlayEvent.Get();
            OutgoingEventWeakPtr = Night && Night->GetName() == Source->GetActiveEventName()
                ? Night : Row->PlayEvent.Get();
            OutgoingID = Source->GetPlayingID();
        }

        void SpawnObjects()
        {
            for (int32 Index = 0; Index < 4; ++Index)
            {
                AActor* Owner = World->SpawnActor<AActor>();
                USceneComponent* Root = NewObject<USceneComponent>(Owner);
                Owner->SetRootComponent(Root);
                Owner->AddInstanceComponent(Root);
                Root->RegisterComponent();
                Owner->SetActorLocation(RuralSamplePositions[0]);
                auto* Profile = NewObject<UWandererObjectAudioProfile>();
                Profile->Priority = 50 + Index * 10;
                Profile->Events = {{TEXT("Loop"), Rural[Index]->GetProfile()->PlayEvent, true}};
                Profile->Events.Add({TEXT("InvalidUrgent"), Rural[1]->GetProfile()->PlayEvent,
                    false, true, 2});
                const auto Handle = Audio->RegisterObjectEmitter(FGuid::NewGuid(), *Owner,
                    {Root, NAME_None, FTransform::Identity, false}, Profile);
                Objects.Add(Handle);
                ObjectOwners.Add(Owner);
                const auto Result = Audio->PostObjectEvent(Handle, TEXT("Loop"));
                Evidence += FString::Printf(TEXT("Object registration %d profileValid=%d handle=%d post=%d event=%s\n"),
                    Index, Profile->Validate(), Handle.IsSet(), static_cast<int32>(Result),
                    *Profile->Events[0].EventSoftPtr.ToString());
            }
        }

        FObjectAudioEmitterHandle RequestEmergency(bool bInInfinite, float InDeadlineSec)
        {
            auto* Profile = NewObject<UWandererObjectAudioProfile>();
            FObjectAudioEvent Event;
            Event.EventID = TEXT("Urgent");
            Event.EventSoftPtr = bInInfinite ? Rural[1]->GetProfile()->PlayEvent
                : TSoftObjectPtr<UAkAudioEvent>(FSoftObjectPath(
                    TEXT("/Game/WwiseAudio/Events/UI/Play_Map_Open.Play_Map_Open")));
            Event.bEmergency = true;
            Event.StartDeadlineSec = InDeadlineSec;
            Profile->Events = {Event,
                {TEXT("DeferredLoop"), Rural[1]->GetProfile()->PlayEvent, true}};
            const auto Handle = Audio->RegisterObjectEmitter(FGuid::NewGuid(), *PawnWeakPtr.Get(),
                {PawnWeakPtr->GetRootComponent(), NAME_None, FTransform::Identity, false}, Profile);
            Objects.Add(Handle);
            Audio->PostObjectEvent(Handle, TEXT("DeferredLoop"));
            Audio->PostObjectEvent(Handle, Event.EventID);
            return Handle;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuralAudioBudgetTest,
    "Wanderer.Audio.Rural.PriorityAndTransitions",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRuralAudioBudgetTest::RunTest(const FString& InParameters)
{
    const auto Fixture = MakeShared<FRuralBudgetFixture>();
    if (!TestTrue(TEXT("Find Rural sources in the real demo map"), Fixture->Initialize()))
    {
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Fixture]()
    {
        auto& F = *Fixture;
        if (FPlatformTime::Seconds() > F.DeadlineSec)
        {
            F.Record(*this, FString::Printf(TEXT("TIMEOUT phase=%d"), F.Phase));
            AddError(TEXT("Rural integration phase timed out"));
            return true;
        }
        F.Pump(RuralSamplePositions[F.Phase < 5 ? F.Phase : 0]);
        constexpr int32 ProductionPoolCapacity = AudioSchedulingDefaults::RegularSlots
            + AudioSchedulingDefaults::TransitionSlots + AudioSchedulingDefaults::EmergencySlots;
        TestTrue(TEXT("Low-budget overrides do not grow the prewarmed production pool"),
            F.Audio->GetEmitterPoolStats().Capacity <= ProductionPoolCapacity);
        if (F.Phase < 5)
        {
            if (!F.Rural[F.Phase]->IsPlaying())
            {
                return false;
            }
            F.Record(*this, TEXT("Natural Rural sample: ") + RuralProfileNames[F.Phase].ToString());
            TestTrue(TEXT("Natural scene admits at most three sources"), F.PostedSources() <= 3);
            ++F.Phase;
            F.DeadlineSec = FPlatformTime::Seconds() + 20;
            if (F.Phase == 5)
            {
                F.SetContentionProfiles();
            }
            return false;
        }
        if (F.Phase == 5)
        {
            if (!F.Rural[0]->IsPlaying() || !F.Rural[1]->IsPlaying() || !F.Rural[2]->IsPlaying()
                || F.Rural[3]->IsPlaying() || F.Rural[4]->IsPlaying()
                || F.Audio->GetEmitterPoolStats().WorldRetiring)
            {
                return false;
            }
            F.Record(*this, TEXT("Contention: grass/snow/pond win; lake/Warehouse culled"));
            F.SaveOutgoing(2);
            F.Rural[3]->PriorityBias = 100;
            F.Phase = 6;
            return false;
        }
        if (F.Phase == 6)
        {
            if (!F.Rural[3]->IsPlaying())
            {
                return false;
            }
            F.Record(*this, TEXT("Lake replaces pond with native fade overlap"));
            TestFalse(TEXT("Pond loses admission"), F.Rural[2]->IsPlaying());
            TestTrue(TEXT("Outgoing pond ID remains alive during fade"),
                WandererAudioBackend::QueryPlaybackActive(F.OutgoingEventWeakPtr.Get(), F.OutgoingID).Get(false));
            TestEqual(TEXT("Three steady sources"), F.PostedSources(), 3);
            F.SaveOutgoing(1);
            F.Rural[4]->PriorityBias = 100;
            F.Phase = 7;
            return false;
        }
        if (F.Phase == 7)
        {
            if (!F.Rural[4]->IsPlaying())
            {
                return false;
            }
            F.Record(*this, TEXT("Warehouse replaces snow; Room consumes logical budget"));
            TestFalse(TEXT("Snow loses admission"), F.Rural[1]->IsPlaying());
            TestTrue(TEXT("Snow fade retains native ID"),
                WandererAudioBackend::QueryPlaybackActive(F.OutgoingEventWeakPtr.Get(), F.OutgoingID).Get(false));
            TestEqual(TEXT("Warehouse does not bypass the logical quota"), F.PostedSources(), 3);
            F.PhaseAtSec = FPlatformTime::Seconds();
            F.Phase = 8;
            return false;
        }
        if (F.Phase == 8)
        {
            if (FPlatformTime::Seconds() - F.PhaseAtSec < 1.2)
            {
                return false;
            }
            TestEqual(TEXT("Native fades release outgoing slots"),
                F.Audio->GetEmitterPoolStats().WorldRetiring, 0);
            for (const auto& SourceWeakPtr : F.Rural)
            {
                SourceWeakPtr->bEnabled = false;
            }
            F.Phase = 9;
            return false;
        }
        if (F.Phase == 9)
        {
            if (F.Audio->GetEmitterPoolStats().WorldRetiring)
            {
                return false;
            }
            F.SpawnObjects();
            F.Phase = 10;
            return false;
        }
        if (F.Phase == 10)
        {
            const auto Low = F.Audio->GetObjectEmitterStats(F.Objects[0]);
            const auto Middle = F.Audio->GetObjectEmitterStats(F.Objects[1]);
            const auto High = F.Audio->GetObjectEmitterStats(F.Objects[2]);
            const auto Highest = F.Audio->GetObjectEmitterStats(F.Objects[3]);
            if (Middle.PlayingInstances != 1 || High.PlayingInstances != 1 || Highest.PlayingInstances != 1)
            {
                return false;
            }
            TestEqual(TEXT("Lowest object is pending without a component"), Low.PhysicalSlots, 0);
            TestEqual(TEXT("Lowest loop preserves its intent"), Low.PendingInstances, 1);
            F.Record(*this, TEXT("Objects compete for the same three regular slots"));
            F.Audio->StopObjectEvent(F.Objects[2], TEXT("Loop"));
            F.Rural[0]->bEnabled = true;
            F.Rural[0]->PriorityBias = 100;
            F.Rural[1]->bEnabled = true;
            F.Rural[1]->PriorityBias = 100;
            F.Phase = 11;
            return false;
        }
        if (F.Phase == 11)
        {
            if (!F.Rural[0]->IsPlaying() || !F.Rural[1]->IsPlaying()
                || F.Audio->GetObjectEmitterStats(F.Objects[1]).PhysicalSlots)
            {
                return false;
            }
            TestEqual(TEXT("Explicit stop does not survive as a resumable loop"),
                F.Audio->GetObjectEmitterStats(F.Objects[2]).PendingInstances, 0);
            F.Rural[0]->bEnabled = false;
            F.Rural[1]->bEnabled = false;
            F.Phase = 12;
            return false;
        }
        if (F.Phase == 12)
        {
            if (F.Audio->GetObjectEmitterStats(F.Objects[1]).PlayingInstances != 1)
            {
                return false;
            }
            TestEqual(TEXT("A stopped-and-culled loop never restarts"),
                F.Audio->GetObjectEmitterStats(F.Objects[2]).PlayingInstances, 0);
            F.Record(*this, TEXT("Culled object loop resumes after readmission"));
            for (int32 Index = 0; Index < 4; ++Index)
            {
                FRadioEmitterSnapshot Source;
                Source.Number = Index + 1;
                Source.ContentID = TEXT("RECORDER_SONG_DEFAULT");
                Source.bPlaying = true;
                Source.Transform.SetLocation(RuralSamplePositions[0] + FVector(Index * 10, 0, 0));
                F.RadioSources.Add(Source);
            }
            FString Status;
            F.RadioEntryPoolCapacity = F.Audio->GetEmitterPoolStats().Capacity;
            TestTrue(TEXT("World radio item queues asynchronous playback"),
                F.Audio->StartRadioPlayback(F.PawnWeakPtr.Get(), TEXT("RECORDER_SONG_DEFAULT"),
                    F.RadioSources, Status));
            F.Phase = 13;
            F.DeadlineSec = FPlatformTime::Seconds() + 15;
            return false;
        }
        if (F.Phase == 13)
        {
            F.RadioPositionMs = F.Audio->GetRadioPosition(
                F.PawnWeakPtr.Get(), 1, TEXT("RECORDER_SONG_DEFAULT"));
            if (F.RadioPositionMs < 100 || F.Audio->GetRadioPlaybackPhase(
                    F.PawnWeakPtr.Get(), TEXT("RECORDER_SONG_DEFAULT")) != ERadioPlaybackPhase::Ready)
            {
                return false;
            }
            TestTrue(TEXT("Radio shares regular capacity"), F.Audio->GetEmitterPoolStats().WorldActive <= 3);
            TestEqual(TEXT("Radio never expands the existing shared pool"),
                F.Audio->GetEmitterPoolStats().Capacity, F.RadioEntryPoolCapacity);
            F.Record(*this, TEXT("Four-position Radio item uses the shared regular budget"));
            F.Rural[0]->bEnabled = true;
            F.Rural[1]->bEnabled = true;
            F.Phase = 14;
            return false;
        }
        if (F.Phase == 14)
        {
            if (F.Audio->GetRadioPlaybackPhase(F.PawnWeakPtr.Get(), TEXT("RECORDER_SONG_DEFAULT"))
                == ERadioPlaybackPhase::Ready)
            {
                return false;
            }
            TestTrue(TEXT("Budget culling preserves the Radio bookmark"),
                F.Audio->HasRadioContent(F.PawnWeakPtr.Get(), TEXT("RECORDER_SONG_DEFAULT")));
            F.Record(*this, TEXT("Higher-priority ambience retires Radio through shared transition capacity"));
            F.Rural[0]->bEnabled = false;
            F.Rural[1]->bEnabled = false;
            F.Phase = 15;
            return false;
        }
        if (F.Phase == 15)
        {
            if (F.Audio->GetRadioPlaybackPhase(F.PawnWeakPtr.Get(), TEXT("RECORDER_SONG_DEFAULT"))
                != ERadioPlaybackPhase::Ready)
            {
                return false;
            }
            TestTrue(TEXT("Readmitted Radio restores its saved position"),
                F.Audio->GetRadioPosition(F.PawnWeakPtr.Get(), 1,
                    TEXT("RECORDER_SONG_DEFAULT")) >= F.RadioPositionMs - 50);
            F.Record(*this, TEXT("Radio resumes automatically after item readmission"));
            F.Emergency = F.RequestEmergency(false, 2);
            F.Phase = 16;
            return false;
        }
        if (F.Phase == 16)
        {
            if (!F.Audio->GetObjectEmitterStats(F.Emergency).PlayingInstances)
            {
                return false;
            }
            TestEqual(TEXT("A finite urgent Event uses exactly one reserve"),
                F.Audio->GetEmitterPoolStats().EmergencyActive, 1);
            TestEqual(TEXT("The object's loop stays pending outside emergency reserve"),
                F.Audio->GetObjectEmitterStats(F.Emergency).PendingInstances, 1);
            TestTrue(TEXT("Urgency does not expand regular capacity"),
                F.Audio->GetEmitterPoolStats().WorldActive <= 3);
            F.Expiring = F.RequestEmergency(false, 0.01f);
            F.Phase = 17;
            return false;
        }
        if (F.Phase == 17)
        {
            const auto Stats = F.Audio->GetObjectEmitterStats(F.Expiring);
            if (Stats.LastFailure != EObjectAudioFailure::Expired)
            {
                return false;
            }
            TestEqual(TEXT("An expired request never borrows an emitter"), Stats.PhysicalSlots, 0);
            F.InvalidEmergency = F.RequestEmergency(true, 2);
            F.Phase = 18;
            return false;
        }
        const auto Invalid = F.Audio->GetObjectEmitterStats(F.InvalidEmergency);
        if (Invalid.LastFailure != EObjectAudioFailure::MediaUnavailable)
        {
            return false;
        }
        TestEqual(TEXT("An infinite Event cannot occupy emergency reserve"), Invalid.PhysicalSlots, 0);
        if (!F.bCheckedRegularEmergency)
        {
            F.bCheckedRegularEmergency = true;
            TestTrue(TEXT("An existing regular object accepts an urgent request for validation"),
                F.Audio->PostObjectEvent(F.Objects[3], TEXT("InvalidUrgent"))
                    == EObjectAudioRequestResult::LoadingMedia);
            return false;
        }
        const auto Regular = F.Audio->GetObjectEmitterStats(F.Objects[3]);
        if (Regular.LastFailure != EObjectAudioFailure::MediaUnavailable)
        {
            return false;
        }
        TestEqual(TEXT("Invalid urgent content does not add a regular PlayingID"), Regular.PlayingInstances, 1);
        TestFalse(TEXT("Existing regular playback stays outside reserve"), Regular.bEmergency);
        F.Record(*this, TEXT("Emergency finite playback, expiry and infinite-event rejection verified"));
        return true;
    }));
    return true;
}
#endif
