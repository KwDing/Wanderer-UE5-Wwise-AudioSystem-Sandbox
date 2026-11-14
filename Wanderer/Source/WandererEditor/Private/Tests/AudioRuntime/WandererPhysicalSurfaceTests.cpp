#if WITH_DEV_AUTOMATION_TESTS
#include "AkAudioDevice.h"
#include "AkComponent.h"
#include "AkSwitchValue.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Components/CharacterAudio/WandererCharacterAudioComponent.h"
#include "Data/Audio/WandererCharacterAudioProfile.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"

namespace
{
    // The test map owns the actor/components; the reflected Profile owns both
    // transient tables. The latent command observes them weakly on the game thread.
    class FNativeSurfaceSwitchProbe : public IAutomationLatentCommand
    {
    public:
        FNativeSurfaceSwitchProbe(FAutomationTestBase* InTest, UWandererCharacterAudioComponent* InAudio,
            UAkComponent* InEmitter, UDataTable* InTable)
            : Test(InTest), Audio(InAudio), Emitter(InEmitter), Table(InTable)
        {
            for (const auto& Entry : InTable->GetRowMap())
                Cases.Add(*reinterpret_cast<const FWandererCharacterAudioSwitchRow*>(Entry.Value));
            const auto* Default = InTable->FindRow<FWandererCharacterAudioSwitchRow>(TEXT("default"), TEXT("Probe"));
            if (Default)
            {
                auto Missing = *Default;
                Missing.PhysicalSurface = SurfaceType62;
                Cases.Add(Missing);
            }
        }

        virtual bool Update() override
        {
            if (!Audio.IsValid() || !Emitter.IsValid() || !Table.IsValid())
            { Test->AddError(TEXT("Surface probe owner expired")); return true; }
            if (Index == Cases.Num())
            {
                Audio->GetOwner()->Destroy();
                return true;
            }
            auto* Engine = IWwiseSoundEngineAPI::Get();
            if (!Engine) { Test->AddError(TEXT("Wwise became unavailable")); return true; }
            const auto& Expected = Cases[Index];
            if (!bPending)
            {
                FWandererCharacterAudioContext Context;
                Context.PhysicalSurface = Expected.PhysicalSurface;
                Context.Surface = TEXT("DeliberatelyWrongSemanticName");
                const auto Result = Audio->ExecuteAction(TEXT("Probe"), Context);
                Test->TestEqual(TEXT("Native key accepted without semantic name"), Result.Status, EWandererAudioExecutionStatus::Accepted);
                Deadline = FPlatformTime::Seconds() + 3;
                bPending = true;
                Engine->RenderAudio();
                return false;
            }
            AkSwitchStateID Actual = 0;
            const auto Result = Engine->Query->GetSwitch(Expected.SwitchValue->GetGroupID(), Emitter->GetAkGameObjectID(), Actual);
            if ((Result != AK_Success || Actual != Expected.SwitchValue->GetShortID()) && FPlatformTime::Seconds() < Deadline) return false;
            Test->TestEqual(FString::Printf(TEXT("Surface %d native Wwise query"), static_cast<int32>(Expected.PhysicalSurface)), Result, AK_Success);
            Test->TestEqual(TEXT("Wwise state equals DT asset ID"), Actual, Expected.SwitchValue->GetShortID());
            ++Index;
            bPending = false;
            if (Index == Cases.Num() && !bRemapped)
            {
                // Audio-only remap: physical wood -> metal Switch; no art change.
                auto* Wood = Table->FindRow<FWandererCharacterAudioSwitchRow>(TEXT("wood"), TEXT("Probe"));
                const auto* Metal = Table->FindRow<FWandererCharacterAudioSwitchRow>(TEXT("metal"), TEXT("Probe"));
                if (Wood && Metal)
                {
                    Wood->SwitchValue = Metal->SwitchValue;
                    Cases.Add(*Wood);
                    Test->TestTrue(TEXT("Audio-only DT remap initializes"), Audio->InitializeAudio(Emitter.Get()));
                }
                else Test->AddError(TEXT("Existing wood/metal mapping missing"));
                bRemapped = true;
            }
            return false;
        }
    private:
        FAutomationTestBase* Test;
        TWeakObjectPtr<UWandererCharacterAudioComponent> Audio;
        TWeakObjectPtr<UAkComponent> Emitter;
        TWeakObjectPtr<UDataTable> Table;
        TArray<FWandererCharacterAudioSwitchRow> Cases;
        int32 Index = 0;
        double Deadline = 0;
        bool bPending = false;
        bool bRemapped = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeSurfaceSwitchTest, "Wanderer.Audio.NativePhysicalSurface.SwitchMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeSurfaceSwitchTest::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Real Wwise initialized"), FAkAudioDevice::IsInitialized())) return false;
    auto* Source = LoadObject<UDataTable>(nullptr, TEXT("/Game/Config/Audio/Characters/Generic/DT_FootstepSurface_Generic"));
    if (!TestNotNull(TEXT("Saved native DT"), Source)) return false;
    auto* World = FAutomationEditorCommonUtils::CreateNewMap();
    auto* Actor = World->SpawnActor<AActor>();
    auto* Emitter = NewObject<UAkComponent>(Actor);
    Actor->AddInstanceComponent(Emitter); Actor->SetRootComponent(Emitter); Emitter->RegisterComponent();
    auto* Audio = NewObject<UWandererCharacterAudioComponent>(Actor);
    Actor->AddInstanceComponent(Audio); Audio->RegisterComponent(); Audio->Activate();
    auto* Profile = NewObject<UWandererCharacterAudioProfile>(Audio);
    auto* Switches = DuplicateObject<UDataTable>(Source, Profile);
    auto* Actions = NewObject<UDataTable>(Profile);
    Actions->RowStruct = FWandererCharacterAudioActionRow::StaticStruct();
    FWandererCharacterAudioActionRow Row;
    auto& Action = Row.InlineAudioActions.AddDefaulted_GetRef();
    Action.AudioActionID = TEXT("SetNativeSurface");
    Action.AudioActionType = EWandererAudioActionType::SetSwitch;
    Action.ValueSource = EWandererAudioValueSource::ProfileSurface;
    Action.bUseFallback = true;
    Actions->AddRow(TEXT("Probe"), Row);
    Profile->ActionTables.Add(Actions); Profile->SwitchTables.Add(Switches);
    auto* Property = FindFProperty<FObjectProperty>(Audio->GetClass(), TEXT("Profile"));
    if (!TestNotNull(TEXT("Profile property"), Property)) { Actor->Destroy(); return false; }
    Property->SetObjectPropertyValue_InContainer(Audio, Profile);
    if (!TestTrue(TEXT("Native profile initializes"), Audio->InitializeAudio(Emitter))) { Actor->Destroy(); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FNativeSurfaceSwitchProbe(this, Audio, Emitter, Switches));
    return true;
}
#endif
