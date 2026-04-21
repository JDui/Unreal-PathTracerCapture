#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "PathTracerCaptureTypes.generated.h"

UENUM(BlueprintType)
enum class EPathTracerCaptureBackend : uint8
{
    Viewport UMETA(DisplayName = "Viewport"),
    MovieRenderQueue UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EPathTracerCaptureAlphaMode : uint8
{
    None UMETA(DisplayName = "No Alpha"),
    MergeAlpha UMETA(DisplayName = "Merge Alpha Into RGBA"),
    SeparateAlphaTexture UMETA(DisplayName = "Separate Alpha Texture")
};

UENUM(BlueprintType)
enum class EPathTracerCaptureAlphaSource : uint8
{
    WorldNormalMask UMETA(DisplayName = "WorldNormal Binary"),
    PostProcessMaterial UMETA(DisplayName = "Post Process Material")
};

UENUM(BlueprintType)
enum class EPathTracerCapturePhase : uint8
{
    Idle UMETA(DisplayName = "Idle"),
    Preparing UMETA(DisplayName = "Preparing"),
    Accumulating UMETA(DisplayName = "Accumulating"),
    Encoding UMETA(DisplayName = "Encoding"),
    Done UMETA(DisplayName = "Done"),
    Failed UMETA(DisplayName = "Failed"),
    Cancelled UMETA(DisplayName = "Cancelled")
};

USTRUCT(BlueprintType)
struct FPathTracerCaptureRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPathTracerCaptureBackend Backend = EPathTracerCaptureBackend::Viewport;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionX = 1920;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionY = 1080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1", ClampMax = "8192"))
    int32 TargetSPP = 128;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FString OutputDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    FString FilenamePattern = TEXT("AXi_{date}{seq}");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    bool bEnableDenoiser = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPathTracerCaptureAlphaMode AlphaMode = EPathTracerCaptureAlphaMode::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
    EPathTracerCaptureAlphaSource AlphaSource = EPathTracerCaptureAlphaSource::PostProcessMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (AllowedClasses = "MaterialInterface"))
    FSoftObjectPath AlphaPostProcessMaterial = FSoftObjectPath(TEXT("/PathTracerCapture/Ref/MP_ALPHA.MP_ALPHA"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Raw Alpha (Skip Remap)"))
    bool bUseRawAlphaMask = false;
};

USTRUCT(BlueprintType)
struct FPathTracerCaptureResult
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    bool bSuccess = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    EPathTracerCaptureBackend Backend = EPathTracerCaptureBackend::Viewport;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    FString OutputFile;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    FString ErrorMessage;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    float DurationSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct FPathTracerCaptureProgress
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    bool bIsRunning = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    EPathTracerCapturePhase Phase = EPathTracerCapturePhase::Idle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    int32 TargetSPP = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    int32 EstimatedCurrentSPP = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Capture")
    FString StatusMessage;
};
