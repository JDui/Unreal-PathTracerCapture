#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PathTracerCaptureTypes.h"
#include "PathTracerCaptureBlueprintLibrary.generated.h"

UCLASS()
class PATHTRACERCAPTUREEDITOR_API UPathTracerCaptureBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "PathTracerCapture", meta = (DevelopmentOnly))
    static bool StartPathTracerCapture(const FPathTracerCaptureRequest& Request);

    UFUNCTION(BlueprintCallable, Category = "PathTracerCapture", meta = (DevelopmentOnly))
    static void CancelPathTracerCapture();

    UFUNCTION(BlueprintPure, Category = "PathTracerCapture", meta = (DevelopmentOnly))
    static FPathTracerCaptureResult GetLastCaptureResult();

    UFUNCTION(BlueprintPure, Category = "PathTracerCapture", meta = (DevelopmentOnly))
    static FPathTracerCaptureProgress GetCaptureProgress();
};
