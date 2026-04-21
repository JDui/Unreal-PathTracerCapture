#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "PathTracerCaptureTypes.h"
#include "UObject/Object.h"
#include "PathTracerCaptureSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings, BlueprintType)
class PATHTRACERCAPTUREEDITOR_API UPathTracerCaptureSettings : public UObject
{
    GENERATED_BODY()

public:
    UPathTracerCaptureSettings();
    static FSoftObjectPath GetDefaultAlphaPostProcessMaterialPath();
    void ResetToDefaults();

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPathTracerCaptureBackend Backend = EPathTracerCaptureBackend::Viewport;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionX = 1920;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionY = 1080;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1", ClampMax = "8192"))
    int32 TargetSPP = 128;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FDirectoryPath OutputDirectory;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FString FilenamePattern = TEXT("AXi_{date}{seq}");

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bEnableDenoiser = false;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPathTracerCaptureAlphaMode AlphaMode = EPathTracerCaptureAlphaMode::None;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPathTracerCaptureAlphaSource AlphaSource = EPathTracerCaptureAlphaSource::PostProcessMaterial;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (AllowedClasses = "MaterialInterface"))
    FSoftObjectPath AlphaPostProcessMaterial = FSoftObjectPath(TEXT("/PathTracerCapture/Ref/MP_ALPHA.MP_ALPHA"));

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Raw Alpha (Skip Remap)"))
    bool bUseRawAlphaMask = false;

    FPathTracerCaptureRequest MakeRequest() const;
};
