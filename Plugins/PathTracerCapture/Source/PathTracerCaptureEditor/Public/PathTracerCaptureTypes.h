#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "PathTracerCaptureTypes.generated.h"

UENUM(BlueprintType)
enum class EPathTracerCaptureBackend : uint8
{
    Viewport UMETA(DisplayName = "视口"),
    MovieRenderQueue UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EPathTracerCaptureAlphaMode : uint8
{
    None UMETA(DisplayName = "无Alpha"),
    MergeAlpha UMETA(DisplayName = "合并Alpha到RGBA"),
    SeparateAlphaTexture UMETA(DisplayName = "分离Alpha贴图")
};

UENUM(BlueprintType)
enum class EPathTracerCaptureAlphaSource : uint8
{
    WorldNormalMask UMETA(DisplayName = "世界法线二值"),
    PostProcessMaterial UMETA(DisplayName = "场景Alpha通道")
};

UENUM(BlueprintType)
enum class EPathTracerCapturePhase : uint8
{
    Idle UMETA(DisplayName = "空闲"),
    Preparing UMETA(DisplayName = "准备中"),
    Accumulating UMETA(DisplayName = "累积中"),
    Encoding UMETA(DisplayName = "编码中"),
    Done UMETA(DisplayName = "完成"),
    Failed UMETA(DisplayName = "失败"),
    Cancelled UMETA(DisplayName = "已取消")
};

USTRUCT(BlueprintType)
struct FPathTracerCaptureRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "渲染后端"))
    EPathTracerCaptureBackend Backend = EPathTracerCaptureBackend::Viewport;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "分辨率 X", ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionX = 1920;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "分辨率 Y", ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionY = 1080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "目标采样数 (SPP)", ClampMin = "1", ClampMax = "8192"))
    int32 TargetSPP = 128;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "输出目录"))
    FString OutputDirectory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "文件名格式"))
    FString FilenamePattern = TEXT("AXi_{date}{seq}");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "启用降噪器"))
    bool bEnableDenoiser = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Alpha模式"))
    EPathTracerCaptureAlphaMode AlphaMode = EPathTracerCaptureAlphaMode::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Alpha来源"))
    EPathTracerCaptureAlphaSource AlphaSource = EPathTracerCaptureAlphaSource::PostProcessMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "透明通道后处理材质", AllowedClasses = "/Script/Engine.MaterialInterface"))
    FSoftObjectPath AlphaPostProcessMaterial = FSoftObjectPath(TEXT("/PathTracerCapture/Ref/MP_ALPHA.MP_ALPHA"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "原始Alpha（跳过重映射）"))
    bool bUseRawAlphaMask = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Alpha Power", UIMin = "0.5", UIMax = "2.0"))
    float AlphaPower = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha Anti-Aliasing", meta = (DisplayName = "Alpha辅助屏幕采样百分比", ClampMin = "25.0", ClampMax = "400.0", UIMin = "100.0", UIMax = "200.0"))
    float AlphaAuxiliaryScreenPercentage = 110.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha Anti-Aliasing", meta = (DisplayName = "Alpha堆栈张数", ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "16"))
    int32 AlphaStackSampleCount = 4;
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
