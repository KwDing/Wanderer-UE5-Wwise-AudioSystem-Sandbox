#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/DataTable.h"
#include "K2Node_Variable.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

#include "Data/Audio/WandererWorldAudioTypes.h"
#include "Data/Audio/WorldAudioBudgetData.h"

namespace
{
    class FLegacyAudioPropertyWriter : public FObjectAndNameAsStringProxyArchive
    {
    public:
        FLegacyAudioPropertyWriter(FArchive& InArchive, FName InCurrentName, FName InLegacyName)
            : FObjectAndNameAsStringProxyArchive(InArchive, false)
            , CurrentName(InCurrentName)
            , LegacyName(InLegacyName)
        {
        }

        FArchive& operator<<(FName& InOutName) override
        {
            FName SavedName = InOutName == CurrentName ? LegacyName : InOutName;
            return FNameAsStringProxyArchive::operator<<(SavedName);
        }

    private:
        FName CurrentName;
        FName LegacyName;
    };

    template <typename RowType>
    void VerifyPropertyRename(FAutomationTestBase& InOutTest,
        FName InLegacyName,
        FName InCurrentName,
        float RowType::* InMember)
    {
        UScriptStruct* Struct = RowType::StaticStruct();
        InOutTest.TestEqual(TEXT("Legacy property redirects to the native field"),
            FProperty::FindRedirectedPropertyName(Struct, InLegacyName), InCurrentName);
        InOutTest.TestTrue(TEXT("Blueprint struct pin matcher accepts the legacy name"),
            UK2Node_Variable::DoesRenamedVariableMatch(InLegacyName, InCurrentName, Struct));
        const FProperty* Property = FindFProperty<FProperty>(Struct, InCurrentName);
        if (!InOutTest.TestNotNull(TEXT("New native field exists"), Property))
        {
            return;
        }
        InOutTest.TestEqual(TEXT("Blueprint keeps its complete display name"),
            Property->GetDisplayNameText().ToString(), InLegacyName.ToString());

        constexpr float SavedValueSec = 1.75f;
        RowType Original;
        Original.*InMember = SavedValueSec;
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes);
        FLegacyAudioPropertyWriter LegacyArchive(Writer, InCurrentName, InLegacyName);
        Struct->SerializeItem(LegacyArchive, &Original, nullptr);

        RowType Restored;
        FMemoryReader Reader(Bytes);
        FObjectAndNameAsStringProxyArchive LoadArchive(Reader, false);
        Struct->SerializeItem(LoadArchive, &Restored, nullptr);
        InOutTest.TestEqual(TEXT("Loading an old property tag preserves its value"),
            Restored.*InMember, SavedValueSec);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioPropertyRedirectTest,
    "Wanderer.Audio.Authoring.PropertyRedirects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAudioPropertyRedirectTest::RunTest(const FString& InParameters)
{
    VerifyPropertyRename(*this,
        TEXT("FadeOutSeconds"),
        TEXT("FadeOutSec"),
        &FWandererWorldAudioProfileRow::FadeOutSec);
    VerifyPropertyRename(*this,
        TEXT("UnloadDelaySeconds"),
        TEXT("UnloadDelaySec"),
        &FWandererAudioResourcePolicyRow::UnloadDelaySec);
    VerifyPropertyRename(*this,
        TEXT("UnloadDelaySeconds"),
        TEXT("UnloadDelaySec"),
        &FWandererAudioResourceGroup::UnloadDelaySec);

    const UDataTable* Profiles =
        LoadObject<UDataTable>(nullptr, TEXT("/Game/Config/Audio/WorldAudio/DT_WorldAudioProfiles"));
    const UDataTable* Policies = LoadObject<UDataTable>(
        nullptr, TEXT("/Game/Config/Audio/WorldAudio/DT_AudioResourcePolicies"));
    TestTrue(TEXT("Existing profile table loads with its native row type"),
        Profiles && Profiles->GetRowStruct() == FWandererWorldAudioProfileRow::StaticStruct()
            && Profiles->GetRowMap().Num() > 0);
    TestTrue(TEXT("Existing policy table loads with its native row type"),
        Policies && Policies->GetRowStruct() == FWandererAudioResourcePolicyRow::StaticStruct()
            && Policies->GetRowMap().Num() > 0);
    return true;
}

#endif
