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

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "渲染后端", ToolTip = "捕获渲染后端（当前仅支持视口路径追踪）。"))
    EPathTracerCaptureBackend Backend = EPathTracerCaptureBackend::Viewport;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "分辨率 X", ToolTip = "输出图像宽度（像素）。", ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionX = 1920;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "分辨率 Y", ToolTip = "输出图像高度（像素）。", ClampMin = "16", ClampMax = "16384"))
    int32 ResolutionY = 1080;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "目标采样数 (SPP)", ToolTip = "路径追踪的目标每像素采样数，数值越高渲染越干净但耗时越长。", ClampMin = "1", ClampMax = "8192"))
    int32 TargetSPP = 128;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "输出目录", ToolTip = "渲染结果的保存目录。"))
    FDirectoryPath OutputDirectory;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "文件名格式", ToolTip = "输出文件名格式，支持 {date} {seq} {map} {backend} {spp} {timestamp} 等占位符。"))
    FString FilenamePattern = TEXT("AXi_{date}{seq}");

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "启用降噪器", ToolTip = "开启路径追踪降噪器，可在较低采样数下获得更干净的结果。"))
    bool bEnableDenoiser = false;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Alpha模式", ToolTip = "是否输出透明通道：无Alpha / 合并Alpha到RGBA / 分离Alpha贴图。"))
    EPathTracerCaptureAlphaMode AlphaMode = EPathTracerCaptureAlphaMode::None;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "Alpha来源", ToolTip = "Alpha通道的来源：世界法线二值掩码 或 后处理材质输出。"))
    EPathTracerCaptureAlphaSource AlphaSource = EPathTracerCaptureAlphaSource::PostProcessMaterial;

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "透明通道后处理材质", ToolTip = "用于输出透明通道的后处理材质，可点击【预热Alpha通道】提前加载以避免首次渲染卡顿。", AllowedClasses = "/Script/Engine.MaterialInterface"))
    FSoftObjectPath AlphaPostProcessMaterial = FSoftObjectPath(TEXT("/PathTracerCapture/Ref/MP_ALPHA.MP_ALPHA"));

    UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (DisplayName = "原始Alpha（跳过重映射）", ToolTip = "启用后跳过Alpha Levels 重映射，直接使用原始掩码值。"))
    bool bUseRawAlphaMask = false;

    FPathTracerCaptureRequest MakeRequest() const;
};
