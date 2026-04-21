#pragma once

#include "CoreMinimal.h"
#include "PathTracerCaptureTypes.h"

class PATHTRACERCAPTUREEDITOR_API FPathTracerCaptureVersionCompat
{
public:
    static FString BackendToToken(EPathTracerCaptureBackend Backend);
    static FString MakeTimestampToken();
    static FString GetCurrentMapToken();
    static FString ResolveFilename(const FString& Pattern, const FPathTracerCaptureRequest& Request);
    static FIntPoint ClampResolution(int32 Width, int32 Height);
    static bool IsPathTracingSupported(FText& OutReason);
};
