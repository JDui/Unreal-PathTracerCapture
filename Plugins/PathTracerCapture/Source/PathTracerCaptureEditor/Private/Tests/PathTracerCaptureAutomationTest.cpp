#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "PathTracerCaptureEditorSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPathTracerCaptureSubsystemAvailabilityTest,
    "PathTracerCapture.Subsystem.Availability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPathTracerCaptureSubsystemAvailabilityTest::RunTest(const FString& Parameters)
{
    if (!GEditor)
    {
        AddError(TEXT("GEditor is null."));
        return false;
    }

    UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>();
    TestNotNull(TEXT("PathTracerCaptureEditorSubsystem should exist"), Subsystem);
    return Subsystem != nullptr;
}

#endif
