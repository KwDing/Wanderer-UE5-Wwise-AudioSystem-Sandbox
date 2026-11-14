#include "Framework/RadioControlModel.h"

#include "Interfaces/RadioControlInterface.h"

bool URadioControlModel::Execute(const FRadioControlRequest& InRequest)
{
    check(IsInGameThread());
    IRadioControlInterface* Provider = Cast<IRadioControlInterface>(ProviderWeakPtr.Get());
    return !bStopped && Provider && Provider->ExecuteRadioRequest(InRequest);
}

FRadioControlSnapshot URadioControlModel::GetSnapshot() const
{
    const IRadioControlInterface* Provider = Cast<IRadioControlInterface>(ProviderWeakPtr.Get());
    return Provider ? Provider->ReadRadioState() : FRadioControlSnapshot();
}

UTextureRenderTarget2D* URadioControlModel::GetPreview(EWandererSpeakerItem InItem) const
{
    const IRadioControlInterface* Provider = Cast<IRadioControlInterface>(ProviderWeakPtr.Get());
    return Provider ? Provider->GetRadioPreview(InItem) : nullptr;
}

bool URadioControlModel::RegisterProvider(UObject* InProvider)
{
    check(IsInGameThread());
    if (bStopped || ProviderWeakPtr.IsValid() || !IsValid(InProvider)
        || !Cast<IRadioControlInterface>(InProvider))
    {
        return false;
    }
    ProviderWeakPtr = InProvider;
    return true;
}

void URadioControlModel::SetPreviewEnabled(bool bInEnabled)
{
    if (IRadioControlInterface* Provider = Cast<IRadioControlInterface>(ProviderWeakPtr.Get()))
    {
        Provider->SetRadioPreviewEnabled(bInEnabled);
    }
}

void URadioControlModel::UnregisterProvider(UObject* InProvider)
{
    check(IsInGameThread());
    if (ProviderWeakPtr.HasSameIndexAndSerialNumber(TWeakObjectPtr<UObject>(InProvider)))
    {
        ProviderWeakPtr.Reset();
    }
}

void URadioControlModel::Stop()
{
    if (bStopped)
    {
        return;
    }
    bStopped = true;
    IRadioControlInterface* Provider = Cast<IRadioControlInterface>(ProviderWeakPtr.Get());
    ProviderWeakPtr.Reset();
    if (Provider)
    {
        Provider->SetRadioPreviewEnabled(false);
    }
}
