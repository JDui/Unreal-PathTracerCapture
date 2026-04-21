#include "PathTracerCaptureBlueprintLibrary.h"

#include "Editor.h"
#include "PathTracerCaptureEditorSubsystem.h"

namespace
{
    UPathTracerCaptureEditorSubsystem* GetSubsystem()
    {
        return GEditor ? GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>() : nullptr;
    }
}

bool UPathTracerCaptureBlueprintLibrary::StartPathTracerCapture(const FPathTracerCaptureRequest& Request)
{
    if (UPathTracerCaptureEditorSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->StartCapture(Request);
    }
    return false;
}

void UPathTracerCaptureBlueprintLibrary::CancelPathTracerCapture()
{
    if (UPathTracerCaptureEditorSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->CancelCapture();
    }
}

FPathTracerCaptureResult UPathTracerCaptureBlueprintLibrary::GetLastCaptureResult()
{
    if (UPathTracerCaptureEditorSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->GetLastResult();
    }
    return FPathTracerCaptureResult();
}

FPathTracerCaptureProgress UPathTracerCaptureBlueprintLibrary::GetCaptureProgress()
{
    if (UPathTracerCaptureEditorSubsystem* Subsystem = GetSubsystem())
    {
        return Subsystem->GetProgress();
    }
    return FPathTracerCaptureProgress();
}
