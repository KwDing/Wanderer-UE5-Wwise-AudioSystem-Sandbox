#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "GameFramework/Pawn.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"

#include "Data/GameStartupSettings.h"
#include "Data/WorldPresentationSettings.h"
#include "Editing/MaterialAuthoringLibrary.h"
#include "Editing/VolumeBrushUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldPresentationRulesTest,
    "Wanderer.Framework.WorldPresentationRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldPresentationRulesTest::RunTest(const FString& InParameters)
{
    const TArray<FName> Maps = {TEXT("L_AudioSystemDemo")};
    TestTrue(TEXT("Authored Demo matches"),
        UWorldPresentationSettings::MatchesMap(TEXT("L_AudioSystemDemo"), Maps));
    TestTrue(TEXT("PIE prefix matches"),
        UWorldPresentationSettings::MatchesMap(TEXT("UEDPIE_1_L_AudioSystemDemo"), Maps));
    TestFalse(TEXT("Unrelated map is opt-in"),
        UWorldPresentationSettings::MatchesMap(TEXT("City"), Maps));
    TestFalse(TEXT("Arbitrary matching suffix rejected"),
        UWorldPresentationSettings::MatchesMap(TEXT("Other_L_AudioSystemDemo"), Maps));
    TestFalse(TEXT("Empty rule list disables fallback"),
        UWorldPresentationSettings::MatchesMap(TEXT("L_AudioSystemDemo"), {}));
    TestNotNull(TEXT("Configured default Pawn resolves"),
        GetDefault<UGameStartupSettings>()->DefaultPawnClassSoftPtr.LoadSynchronous());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMigratedMaterialUndoTest,
    "Wanderer.EditorTools.MaterialUndoContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMigratedMaterialUndoTest::RunTest(const FString& InParameters)
{
    const TStrongObjectPtr<UMaterial> Material(
        NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transactional));
    const TStrongObjectPtr<UMaterialExpressionConstant> Expression(
        NewObject<UMaterialExpressionConstant>(Material.Get()));
    TestFalse(TEXT("Missing output rejected"),
        UMaterialAuthoringLibrary::ConnectMaterialAttributes(Expression.Get(), TEXT("Missing")));
    {
        const FScopedTransaction Transaction(
            FText::FromString(TEXT("Material caller transaction")));
        TestTrue(TEXT("Known output connected"),
            UMaterialAuthoringLibrary::ConnectMaterialAttributes(Expression.Get(), NAME_None));
    }
    TestTrue(TEXT("Material attributes enabled"), Material->bUseMaterialAttributes);
    GEditor->UndoTransaction();
    TestFalse(TEXT("Caller undo restores material"), Material->bUseMaterialAttributes);
    TestFalse(TEXT("Invalid prism rejected"), WandererVolumeBrush::IsValidVolumeOutline({}, 1.0f));
    TestTrue(TEXT("Counter-clockwise triangle accepted"),
        WandererVolumeBrush::IsValidVolumeOutline({{0, 0}, {100, 0}, {0, 100}}, 10.0f));
    return true;
}
#endif
