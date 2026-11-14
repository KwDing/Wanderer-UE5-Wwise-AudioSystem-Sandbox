#include "Editing/MaterialAuthoringLibrary.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunctionInterface.h"


bool UMaterialAuthoringLibrary::ConnectMaterialAttributes(
    UMaterialExpression* Expression, FName OutputName)
{
    if (!IsInGameThread() || !IsValid(Expression))
    {
        return false;
    }
    UMaterial* Material = Cast<UMaterial>(Expression->GetOuter());
    if (!Material)
    {
        return false;
    }
    const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    const int32 Index = Outputs.IndexOfByPredicate(
        [OutputName](const FExpressionOutput& Output)
        {
            return Output.OutputName == OutputName;
        });
    FExpressionInput* Input = Material->GetExpressionInputForProperty(MP_MaterialAttributes);
    if (Index == INDEX_NONE || !Input)
    {
        return false;
    }
    Material->Modify();
    Material->bUseMaterialAttributes = true;
    Input->Connect(Index, Expression);
    Material->MarkPackageDirty();
    return true;
}

TArray<FString> UMaterialAuthoringLibrary::GetMaterialFunctionOutputs(
    UMaterialFunctionInterface* Function)
{
    TArray<FString> Names;
    if (!IsInGameThread() || !IsValid(Function))
    {
        return Names;
    }
    TArray<FFunctionExpressionInput> Inputs;
    TArray<FFunctionExpressionOutput> Outputs;
    Function->GetInputsAndOutputs(Inputs, Outputs);
    for (const FFunctionExpressionOutput& Output : Outputs)
    {
        if (Output.ExpressionOutput)
        {
            Names.Add(Output.ExpressionOutput->OutputName.ToString());
        }
    }
    return Names;
}
