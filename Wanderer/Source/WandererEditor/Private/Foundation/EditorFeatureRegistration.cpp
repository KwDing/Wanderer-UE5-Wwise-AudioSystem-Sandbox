#include "Foundation/EditorFeatureRegistration.h"

namespace
{
    TArray<FEditorFeatureRegistration*>& Registrations()
    {
        static TArray<FEditorFeatureRegistration*> Entries;
        return Entries;
    }
} // namespace

FEditorFeatureRegistration::FEditorFeatureRegistration(
    FName InName, TFunction<void()> InStart, TFunction<void()> InStop, bool bInCommandlet)
    : Name(InName), StartCallback(MoveTemp(InStart)), StopCallback(MoveTemp(InStop)),
      bCommandlet(bInCommandlet)
{
    Registrations().Add(this);
}

FEditorFeatureRegistration::~FEditorFeatureRegistration()
{
    Stop();
    Registrations().Remove(this);
}

void FEditorFeatureRegistration::StartAll()
{
    check(IsInGameThread());
    Registrations().Sort(
        [](const FEditorFeatureRegistration& Left, const FEditorFeatureRegistration& Right)
        {
            return Left.Name.LexicalLess(Right.Name);
        });
    for (FEditorFeatureRegistration* Entry : Registrations())
    {
        Entry->Start();
    }
}

void FEditorFeatureRegistration::StopAll()
{
    check(IsInGameThread());
    for (int32 Index = Registrations().Num() - 1; Index >= 0; --Index)
    {
        Registrations()[Index]->Stop();
    }
}

void FEditorFeatureRegistration::Start()
{
    if (bStarted || (IsRunningCommandlet() && !bCommandlet))
    {
        return;
    }
    bStarted = true;
    StartCallback();
}

void FEditorFeatureRegistration::Stop()
{
    if (!bStarted)
    {
        return;
    }
    bStarted = false;
    StopCallback();
}
