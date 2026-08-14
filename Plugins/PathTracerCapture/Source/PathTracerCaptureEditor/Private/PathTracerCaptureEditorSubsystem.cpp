#include "PathTracerCaptureEditorSubsystem.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HighResScreenshot.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PathTracerCaptureSettings.h"
#include "PathTracerCaptureVersionCompat.h"

namespace PathTracerCapture
{
    static const TCHAR* CVarPathTracing = TEXT("r.PathTracing");
    static const TCHAR* CVarPathTracingSPP = TEXT("r.PathTracing.SamplesPerPixel");
    static const TCHAR* CVarPathTracingBackgroundAlpha = TEXT("r.PathTracing.BackgroundAlpha");
    static const TCHAR* CVarPathTracingDenoiser = TEXT("r.PathTracing.Denoiser");
    static const TCHAR* CVarHighResDelay = TEXT("r.HighResScreenshotDelay");
    static const TCHAR* CVarScreenPercentage = TEXT("r.ScreenPercentage");
    static const TCHAR* CVarPostProcessAAQuality = TEXT("r.PostProcessAAQuality");
    static const TCHAR* CVarBloomQuality = TEXT("r.BloomQuality");
    static const TCHAR* CVarEyeAdaptationQuality = TEXT("r.EyeAdaptationQuality");
    static const TCHAR* CVarLocalExposure = TEXT("r.LocalExposure");
    static const TCHAR* CVarMotionBlurQuality = TEXT("r.MotionBlurQuality");
    static const TCHAR* CVarDepthOfFieldQuality = TEXT("r.DepthOfFieldQuality");
    static const TCHAR* CVarLensFlareQuality = TEXT("r.LensFlareQuality");
    static const TCHAR* CVarSceneColorFringeQuality = TEXT("r.SceneColorFringeQuality");
    static FText GetRealtimeOverrideName()
    {
        return FText::FromString(TEXT("PathTracerCaptureCaptureLock"));
    }

    static bool DecodePngToBgra8(const FString& PngFile, int32& OutWidth, int32& OutHeight, TArray64<uint8>& OutRawData)
    {
        TArray64<uint8> CompressedData;
        if (!FFileHelper::LoadFileToArray(CompressedData, *PngFile))
        {
            return false;
        }

        IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
        {
            return false;
        }

        if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutRawData))
        {
            return false;
        }

        OutWidth = Wrapper->GetWidth();
        OutHeight = Wrapper->GetHeight();
        return OutWidth > 0 && OutHeight > 0;
    }

    static bool EncodePngFromBgra8(const FString& PngFile, const int32 Width, const int32 Height, const TArray64<uint8>& RawData)
    {
        IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper.IsValid() || !Wrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, ERGBFormat::BGRA, 8))
        {
            return false;
        }

        const TArray64<uint8> CompressedData = Wrapper->GetCompressed(100);
        return FFileHelper::SaveArrayToFile(CompressedData, *PngFile);
    }

    static bool BuildAveragedMaskFromPngFiles(
        const TArray<FString>& PngFiles,
        const EPathTracerCaptureAlphaSource AlphaSource,
        int32& OutWidth,
        int32& OutHeight,
        TArray64<uint8>& OutRawData)
    {
        OutWidth = 0;
        OutHeight = 0;
        OutRawData.Reset();
        if (PngFiles.Num() <= 0)
        {
            return false;
        }

        TArray64<uint32> SumMaskValues;
        for (int32 FileIndex = 0; FileIndex < PngFiles.Num(); ++FileIndex)
        {
            int32 Width = 0;
            int32 Height = 0;
            TArray64<uint8> RawData;
            if (!DecodePngToBgra8(PngFiles[FileIndex], Width, Height, RawData)
                || Width <= 0
                || Height <= 0
                || RawData.Num() <= 0
                || (RawData.Num() % 4) != 0)
            {
                return false;
            }

            if (FileIndex == 0)
            {
                OutWidth = Width;
                OutHeight = Height;
                SumMaskValues.SetNumZeroed(static_cast<int64>(Width) * static_cast<int64>(Height));
            }
            else if (Width != OutWidth
                || Height != OutHeight
                || RawData.Num() != static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight) * 4)
            {
                return false;
            }

            const int64 PixelCount = static_cast<int64>(Width) * static_cast<int64>(Height);
            if (RawData.Num() != PixelCount * 4 || SumMaskValues.Num() != PixelCount)
            {
                return false;
            }

            // Convert each frame to a scalar mask before averaging. This preserves
            // per-frame coverage for WorldNormal edges instead of averaging RGB and
            // then collapsing the result to a binary value.
            for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
            {
                const int64 Base = PixelIndex * 4;
                uint8 MaskValue = 0;
                if (AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial)
                {
                    // PNG is decoded as BGRA; the post-process alpha material writes R.
                    MaskValue = RawData[Base + 2];
                }
                else
                {
                    const bool bIsBlack = RawData[Base + 0] == 0
                        && RawData[Base + 1] == 0
                        && RawData[Base + 2] == 0;
                    MaskValue = bIsBlack ? 0 : 255;
                }
                SumMaskValues[PixelIndex] += MaskValue;
            }
        }

        const uint32 Divisor = static_cast<uint32>(PngFiles.Num());
        const uint32 RoundingOffset = Divisor / 2;
        const int64 PixelCount = SumMaskValues.Num();
        OutRawData.SetNumUninitialized(PixelCount * 4);
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const uint8 AverageValue = static_cast<uint8>((SumMaskValues[PixelIndex] + RoundingOffset) / Divisor);
            const int64 Base = PixelIndex * 4;
            OutRawData[Base + 0] = AverageValue;
            OutRawData[Base + 1] = AverageValue;
            OutRawData[Base + 2] = AverageValue;
            OutRawData[Base + 3] = 255;
        }

        return OutWidth > 0 && OutHeight > 0 && OutRawData.Num() > 0;
    }

    static FString BuildAlphaMaskFilePath(const FString& SourcePngFile, const FString& Suffix)
    {
        const FString Directory = FPaths::GetPath(SourcePngFile);
        const FString BaseName = FPaths::GetBaseFilename(SourcePngFile);
        return Directory / (BaseName + Suffix + TEXT(".png"));
    }

    static void ApplyMaskToMainAlpha(TArray64<uint8>& InOutMainRawData, const TArray64<uint8>& MaskRawData)
    {
        const int64 PixelCount = FMath::Min<int64>(InOutMainRawData.Num(), MaskRawData.Num()) / 4;
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const int64 Base = PixelIndex * 4;
            InOutMainRawData[Base + 3] = MaskRawData[Base + 0];
        }
    }

    static bool ApplyPhotoshopLikeLevels(
        TArray64<uint8>& InOutMaskRawData,
        const uint8 InputBlack,
        const uint8 InputWhite,
        const float Gamma)
    {
        const int64 PixelCount = InOutMaskRawData.Num() / 4;
        if (PixelCount <= 0)
        {
            return false;
        }

        const int32 Black = FMath::Clamp(static_cast<int32>(InputBlack), 0, 255);
        const int32 White = FMath::Clamp(static_cast<int32>(InputWhite), Black + 1, 255);
        const float SafeGamma = FMath::Max(0.001f, Gamma);
        const float InvRange = 1.0f / static_cast<float>(White - Black);
        const float InvGamma = 1.0f / SafeGamma;
        bool bChanged = false;
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const int64 Base = PixelIndex * 4;
            const uint8 SourceValue = InOutMaskRawData[Base + 0];
            float Normalized = (static_cast<float>(SourceValue) - static_cast<float>(Black)) * InvRange;
            Normalized = FMath::Clamp(Normalized, 0.0f, 1.0f);
            const float GammaAdjusted = FMath::Pow(Normalized, InvGamma);
            const uint8 ResultValue = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(GammaAdjusted * 255.0f), 0, 255));

            if (ResultValue != SourceValue)
            {
                bChanged = true;
            }
            InOutMaskRawData[Base + 0] = ResultValue;
            InOutMaskRawData[Base + 1] = ResultValue;
            InOutMaskRawData[Base + 2] = ResultValue;
            InOutMaskRawData[Base + 3] = 255;
        }

        return bChanged;
    }

    static bool ApplyPowerToMask(TArray64<uint8>& InOutMaskRawData, const float AlphaPower)
    {
        const int64 PixelCount = InOutMaskRawData.Num() / 4;
        if (PixelCount <= 0)
        {
            return false;
        }

        const float SafePower = FMath::IsFinite(AlphaPower) ? FMath::Max(0.001f, AlphaPower) : 1.5f;
        bool bChanged = false;
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const int64 Base = PixelIndex * 4;
            const uint8 SourceValue = InOutMaskRawData[Base + 0];
            const float Normalized = static_cast<float>(SourceValue) / 255.0f;
            const float Powered = FMath::Pow(Normalized, SafePower);
            const uint8 ResultValue = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Powered * 255.0f), 0, 255));

            if (ResultValue != SourceValue)
            {
                bChanged = true;
            }
            InOutMaskRawData[Base + 0] = ResultValue;
            InOutMaskRawData[Base + 1] = ResultValue;
            InOutMaskRawData[Base + 2] = ResultValue;
            InOutMaskRawData[Base + 3] = 255;
        }

        return bChanged;
    }
}

void UPathTracerCaptureEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Progress = FPathTracerCaptureProgress();
    LastResult = FPathTracerCaptureResult();
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPathTracerCaptureEditorSubsystem::Tick), 0.1f);
}

void UPathTracerCaptureEditorSubsystem::Deinitialize()
{
    CancelCapture();
    EndAlphaPreWarm();
    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }
    Super::Deinitialize();
}

bool UPathTracerCaptureEditorSubsystem::CanStartCapture(FText& OutReason) const
{
    if (Progress.bIsRunning)
    {
        OutReason = FText::FromString(TEXT("捕获正在进行中。"));
        return false;
    }

    return FPathTracerCaptureVersionCompat::IsPathTracingSupported(OutReason);
}

FString UPathTracerCaptureEditorSubsystem::GetStatusLogText() const
{
    return FString::Join(StatusLog, TEXT("\n"));
}

void UPathTracerCaptureEditorSubsystem::AppendStatusLog(const FString& Message)
{
    if (Message.IsEmpty())
    {
        return;
    }

    StatusLog.Add(FString::Printf(TEXT("[信息] %s"), *Message));
    UpdatedEvent.Broadcast();
}

bool UPathTracerCaptureEditorSubsystem::StartAlphaPreWarm(const FSoftObjectPath& InMaterialPath, FString& OutMessage)
{
    EndAlphaPreWarm();

    FSoftObjectPath MaterialPath = InMaterialPath;
    if (MaterialPath.IsNull()
        || MaterialPath.ToString().Equals(TEXT("/Game/Ref/MP_ALPHA.MP_ALPHA"), ESearchCase::IgnoreCase))
    {
        MaterialPath = UPathTracerCaptureSettings::GetDefaultAlphaPostProcessMaterialPath();
    }

    UObject* LoadedObject = MaterialPath.TryLoad();
    UMaterialInterface* Material = Cast<UMaterialInterface>(LoadedObject);
    if (!Material)
    {
        OutMessage = FString::Printf(TEXT("预热Alpha通道：无法加载材质 %s。"), *MaterialPath.ToString());
        return false;
    }

    UWorld* World = ResolvePreWarmWorld();
    if (!World)
    {
        OutMessage = TEXT("预热Alpha通道：未找到可用的世界或视口，无法在视口中加载材质。");
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.ObjectFlags = RF_Transient;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (!Volume)
    {
        OutMessage = TEXT("预热Alpha通道：创建临时后处理体积失败。");
        return false;
    }

    Volume->bUnbound = true;
    Volume->Priority = 100000.0f;
    Volume->BlendWeight = 1.0f;
    Volume->Settings = FPostProcessSettings();
    Volume->Settings.AddBlendable(Material, 1.0f);

    PreWarmAlphaPostProcessVolume = Volume;
    PreWarmAlphaWorld = World;
    PreWarmAlphaMaterial.Reset(Material);

    if (FEditorViewportClient* Client = GetActiveViewportClient())
    {
        Client->Invalidate();
    }

    OutMessage = FString::Printf(
        TEXT("预热Alpha通道：已在视口加载材质 %s，正在触发着色器编译，一秒后移除。"),
        *MaterialPath.ToString());
    return true;
}

void UPathTracerCaptureEditorSubsystem::EndAlphaPreWarm()
{
    if (PreWarmAlphaPostProcessVolume.IsValid())
    {
        PreWarmAlphaPostProcessVolume->Destroy();
    }
    PreWarmAlphaPostProcessVolume.Reset();
    PreWarmAlphaWorld.Reset();
    PreWarmAlphaMaterial.Reset();

    if (FEditorViewportClient* Client = GetActiveViewportClient())
    {
        Client->Invalidate();
    }
}

UWorld* UPathTracerCaptureEditorSubsystem::ResolvePreWarmWorld() const
{
    if (FEditorViewportClient* Client = GetActiveViewportClient())
    {
        if (UWorld* World = Client->GetWorld())
        {
            return World;
        }
    }
    return ResolveCaptureWorld();
}

FEditorViewportClient* UPathTracerCaptureEditorSubsystem::GetActiveViewportClient() const
{
    if (GEditor)
    {
        if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
        {
            return static_cast<FEditorViewportClient*>(ActiveViewport->GetClient());
        }
    }
    return nullptr;
}

bool UPathTracerCaptureEditorSubsystem::StartCapture(const FPathTracerCaptureRequest& Request)
{
    FText StartReason;
    if (!CanStartCapture(StartReason))
    {
        LastResult.bSuccess = false;
        LastResult.ErrorMessage = StartReason.ToString();
        SetStatus(EPathTracerCapturePhase::Failed, LastResult.ErrorMessage);
        return false;
    }

    FString ValidationError;
    if (!ValidateRequest(Request, ValidationError))
    {
        LastResult.bSuccess = false;
        LastResult.ErrorMessage = ValidationError;
        SetStatus(EPathTracerCapturePhase::Failed, ValidationError);
        return false;
    }

    ActiveRequest = Request;
    ActiveRequest.Backend = EPathTracerCaptureBackend::Viewport;
    PendingOutputPath = BuildOutputPath(ActiveRequest);
    PendingOutputPrefix = FPaths::GetBaseFilename(PendingOutputPath);
    CaptureStartSeconds = FPlatformTime::Seconds();
    LastProgressUpdateSeconds = CaptureStartSeconds;
    StatusLog.Reset();
    LastResult = FPathTracerCaptureResult();
    LastResult.Backend = EPathTracerCaptureBackend::Viewport;
    SetStatus(EPathTracerCapturePhase::Preparing, TEXT("正在准备视口路径追踪捕获..."), 0);

    if (!StartViewportCapture(ActiveRequest))
    {
        FinishCapture(false, LastResult.ErrorMessage.IsEmpty() ? TEXT("视口捕获启动失败。") : LastResult.ErrorMessage);
        return false;
    }

    Progress.bIsRunning = true;
    Progress.TargetSPP = Request.TargetSPP;
    return true;
}

void UPathTracerCaptureEditorSubsystem::CancelCapture()
{
    if (!Progress.bIsRunning)
    {
        return;
    }

    FinishCapture(false, TEXT("捕获已被用户取消。"));
    SetStatus(EPathTracerCapturePhase::Cancelled, TEXT("已取消。"));
}

bool UPathTracerCaptureEditorSubsystem::Tick(float DeltaTime)
{
    if (!Progress.bIsRunning)
    {
        return true;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    if (NowSeconds - CaptureStartSeconds > 600.0)
    {
        FinishCapture(false, TEXT("捕获超过10分钟，已超时。"));
        return true;
    }

    const double Elapsed = NowSeconds - CaptureStartSeconds;
    const double Normalized = FMath::Clamp(Elapsed / FMath::Max(1.0, ActiveRequest.TargetSPP / 12.0), 0.0, 1.0);
    const int32 EstimatedSPP = FMath::Clamp(FMath::RoundToInt(ActiveRequest.TargetSPP * Normalized), 0, ActiveRequest.TargetSPP);
    if (NowSeconds - LastProgressUpdateSeconds > 0.2)
    {
        SetStatus(EPathTracerCapturePhase::Accumulating, FString::Printf(TEXT("正在累积视口路径追踪采样... (%d/%d)"), EstimatedSPP, ActiveRequest.TargetSPP), EstimatedSPP);
        LastProgressUpdateSeconds = NowSeconds;
    }

    if (!bViewportMainCaptureCompleted && IFileManager::Get().FileExists(*PendingOutputPath))
    {
        bViewportMainCaptureCompleted = true;
    }

    if (!bViewportMainCaptureCompleted)
    {
        return true;
    }

    if (bViewportAuxiliaryAlphaCaptureRequired)
    {
        if (PendingAuxiliaryAlphaCapturePaths.Num() != ActiveRequest.AlphaStackSampleCount
            || !PendingAuxiliaryAlphaCapturePaths.IsValidIndex(ViewportAuxiliaryAlphaCaptureIndex))
        {
            FinishCapture(false, TEXT("辅助Alpha通道临时文件状态无效。"));
            return true;
        }

        PendingAuxiliaryAlphaCapturePath = PendingAuxiliaryAlphaCapturePaths[ViewportAuxiliaryAlphaCaptureIndex];
        if (!bViewportAuxiliaryAlphaCaptureInProgress)
        {
            if (!StartViewportAuxiliaryAlphaCapture())
            {
                FinishCapture(false, LastResult.ErrorMessage.IsEmpty() ? TEXT("辅助Alpha通道捕获失败。") : LastResult.ErrorMessage);
            }
            return true;
        }

        if (!IFileManager::Get().FileExists(*PendingAuxiliaryAlphaCapturePath)
            || IFileManager::Get().FileSize(*PendingAuxiliaryAlphaCapturePath) <= 0)
        {
            return true;
        }

        // FileExists can become true before the screenshot writer has flushed a valid PNG.
        int32 ReadyWidth = 0;
        int32 ReadyHeight = 0;
        TArray64<uint8> ReadyRawData;
        if (!PathTracerCapture::DecodePngToBgra8(PendingAuxiliaryAlphaCapturePath, ReadyWidth, ReadyHeight, ReadyRawData))
        {
            return true;
        }

        bViewportAuxiliaryAlphaCaptureInProgress = false;
        ++ViewportAuxiliaryAlphaCaptureIndex;
        if (ViewportAuxiliaryAlphaCaptureIndex < ActiveRequest.AlphaStackSampleCount)
        {
            PendingAuxiliaryAlphaCapturePath = PendingAuxiliaryAlphaCapturePaths[ViewportAuxiliaryAlphaCaptureIndex];
            SetStatus(
                EPathTracerCapturePhase::Encoding,
                FString::Printf(TEXT("Alpha通道采集中（%d/%d）..."), ViewportAuxiliaryAlphaCaptureIndex + 1, ActiveRequest.AlphaStackSampleCount),
                ActiveRequest.TargetSPP);
            return true;
        }
    }

    SetStatus(EPathTracerCapturePhase::Encoding, TEXT("正在编码PNG输出..."), ActiveRequest.TargetSPP);
    FString PrimaryOutputFile = PendingOutputPath;
    if (!FinalizeAlphaOutput(PendingOutputPath, PrimaryOutputFile))
    {
        FinishCapture(false, TEXT("Alpha输出后处理失败。"));
        return true;
    }

    LastResult.OutputFile = PrimaryOutputFile;
    FinishCapture(true, FString());
    return true;
}

bool UPathTracerCaptureEditorSubsystem::ValidateRequest(const FPathTracerCaptureRequest& Request, FString& OutError) const
{
    if (Request.ResolutionX < 16 || Request.ResolutionY < 16
        || Request.ResolutionX > 16384 || Request.ResolutionY > 16384)
    {
        OutError = TEXT("分辨率不能小于16x16。");
        return false;
    }

    if (Request.TargetSPP <= 0 || Request.TargetSPP > 8192)
    {
        OutError = TEXT("目标采样数（TargetSPP）必须大于0。");
        return false;
    }

    if (!FMath::IsFinite(Request.AlphaAuxiliaryScreenPercentage)
        || Request.AlphaAuxiliaryScreenPercentage < 25.0f
        || Request.AlphaAuxiliaryScreenPercentage > 400.0f)
    {
        OutError = TEXT("Alpha辅助屏幕采样百分比必须是25到400之间的有限数值。");
        return false;
    }

    if (Request.AlphaStackSampleCount < 1 || Request.AlphaStackSampleCount > 64)
    {
        OutError = TEXT("Alpha堆栈张数必须在1到64之间。");
        return false;
    }

    if (!FMath::IsFinite(Request.AlphaPower) || Request.AlphaPower <= 0.0f)
    {
        OutError = TEXT("Alpha Power必须是大于0的有限数值。");
        return false;
    }

    if (Request.OutputDirectory.IsEmpty())
    {
        OutError = TEXT("输出目录为空。");
        return false;
    }

    if (!IFileManager::Get().DirectoryExists(*Request.OutputDirectory))
    {
        if (!IFileManager::Get().MakeDirectory(*Request.OutputDirectory, true))
        {
            OutError = FString::Printf(TEXT("无法创建输出目录：%s"), *Request.OutputDirectory);
            return false;
        }
    }

    return true;
}

FString UPathTracerCaptureEditorSubsystem::BuildOutputPath(const FPathTracerCaptureRequest& Request) const
{
    const FString BaseFile = FPathTracerCaptureVersionCompat::ResolveFilename(Request.FilenamePattern, Request);
    return Request.OutputDirectory / (BaseFile + TEXT(".png"));
}

void UPathTracerCaptureEditorSubsystem::SetStatus(EPathTracerCapturePhase Phase, const FString& Message, int32 EstimatedSPP)
{
    Progress.Phase = Phase;
    Progress.StatusMessage = Message;
    Progress.EstimatedCurrentSPP = EstimatedSPP;

    const FString LogLine = FString::Printf(TEXT("[%s] %s"), *UEnum::GetDisplayValueAsText(Phase).ToString(), *Message);
    StatusLog.Add(LogLine);
    UpdatedEvent.Broadcast();
}

void UPathTracerCaptureEditorSubsystem::FinishCapture(bool bSuccess, const FString& ErrorMessage)
{
    RestoreViewportCaptureState();

    Progress.bIsRunning = false;
    if (bSuccess)
    {
        Progress.Phase = EPathTracerCapturePhase::Done;
        Progress.StatusMessage = TEXT("捕获完成。");
    }
    else
    {
        Progress.Phase = EPathTracerCapturePhase::Failed;
        Progress.StatusMessage = ErrorMessage;
    }

    LastResult.bSuccess = bSuccess;
    LastResult.ErrorMessage = ErrorMessage;
    LastResult.DurationSeconds = static_cast<float>(FPlatformTime::Seconds() - CaptureStartSeconds);
    LastResult.Backend = EPathTracerCaptureBackend::Viewport;
    UpdatedEvent.Broadcast();
}

void UPathTracerCaptureEditorSubsystem::RestoreViewportCaptureState()
{
    if (ViewportSnapshot.bValid && ViewportSnapshot.Client)
    {
        if (ViewportSnapshot.bRealtimeOverridden)
        {
            ViewportSnapshot.Client->RemoveRealtimeOverride(PathTracerCapture::GetRealtimeOverrideName(), false);
        }

        if (!ViewportSnapshot.BufferVisualizationMode.IsNone())
        {
            ViewportSnapshot.Client->ChangeBufferVisualizationMode(ViewportSnapshot.BufferVisualizationMode);
        }

        ViewportSnapshot.Client->SetViewMode(static_cast<EViewModeIndex>(ViewportSnapshot.ViewMode));
        ViewportSnapshot.Client->Invalidate();
    }

    GetHighResScreenshotConfig().SetMaskEnabled(false);
    const float* OriginalScreenPercentage = CVarSnapshot.FloatValues.Find(PathTracerCapture::CVarScreenPercentage);
    const bool bHadOriginalScreenPercentage = OriginalScreenPercentage != nullptr;
    const float SavedScreenPercentage = bHadOriginalScreenPercentage ? *OriginalScreenPercentage : 0.0f;
    RestoreCVars();
    if (bHadOriginalScreenPercentage)
    {
        StatusLog.Add(FString::Printf(
            TEXT("[信息] Alpha辅助采集结束，已恢复 r.ScreenPercentage 原值 %.3f（当前 %.3f）。"),
            static_cast<double>(SavedScreenPercentage),
            static_cast<double>(GetCVarFloat(PathTracerCapture::CVarScreenPercentage, SavedScreenPercentage))));
    }
    RestoreOtherPostProcessVolumes();
    for (const FString& AuxiliaryCapturePath : PendingAuxiliaryAlphaCapturePaths)
    {
        if (!AuxiliaryCapturePath.IsEmpty())
        {
            IFileManager::Get().Delete(*AuxiliaryCapturePath, false, true);
        }
    }
    if (TemporaryAlphaPostProcessVolume)
    {
        TemporaryAlphaPostProcessVolume->Destroy();
    }
    TemporaryAlphaPostProcessVolume = nullptr;
    TemporaryAlphaPostProcessMaterial = nullptr;

    ViewportSnapshot = FViewportSnapshot();
    bViewportMainCaptureCompleted = false;
    bViewportAuxiliaryAlphaCaptureRequired = false;
    bViewportAuxiliaryAlphaCapturePrepared = false;
    bViewportAuxiliaryAlphaCaptureInProgress = false;
    ViewportAuxiliaryAlphaWarmupFramesRemaining = 0;
    ViewportAuxiliaryAlphaCaptureIndex = 0;
    bScreenPercentageRaisedForAuxiliaryCapture = false;
    PendingAuxiliaryAlphaCapturePaths.Reset();
    PendingAuxiliaryAlphaCapturePath.Reset();
    PendingOutputPath.Reset();
    PendingOutputPrefix.Reset();
    CaptureWorld.Reset();
}

bool UPathTracerCaptureEditorSubsystem::StartViewportCapture(const FPathTracerCaptureRequest& Request)
{
    if (!GEditor)
    {
        LastResult.ErrorMessage = TEXT("编辑器不可用。");
        return false;
    }

    FViewport* ActiveViewport = GEditor->GetActiveViewport();
    if (!ActiveViewport)
    {
        LastResult.ErrorMessage = TEXT("未找到活动视口。");
        return false;
    }

    FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(ActiveViewport->GetClient());
    if (!ViewportClient)
    {
        LastResult.ErrorMessage = TEXT("活动视口不是编辑器视口。");
        return false;
    }

    ViewportSnapshot.Viewport = ActiveViewport;
    ViewportSnapshot.Client = ViewportClient;
    ViewportSnapshot.ViewMode = static_cast<int32>(ViewportClient->GetViewMode());
    ViewportSnapshot.BufferVisualizationMode = ViewportClient->CurrentBufferVisualizationMode;
    ViewportSnapshot.bRealtimeOverridden = false;
    ViewportSnapshot.bValid = true;
    CaptureWorld = ViewportClient->GetWorld();

    bViewportMainCaptureCompleted = false;
    bViewportAuxiliaryAlphaCaptureRequired = Request.AlphaMode != EPathTracerCaptureAlphaMode::None;
    bViewportAuxiliaryAlphaCapturePrepared = false;
    bViewportAuxiliaryAlphaCaptureInProgress = false;
    ViewportAuxiliaryAlphaWarmupFramesRemaining = 0;
    ViewportAuxiliaryAlphaCaptureIndex = 0;
    bScreenPercentageRaisedForAuxiliaryCapture = false;
    PendingAuxiliaryAlphaCapturePaths.Reset();
    PendingAuxiliaryAlphaCapturePath.Reset();

    if (bViewportAuxiliaryAlphaCaptureRequired)
    {
        const FString TempDirectory = FPaths::ProjectIntermediateDir() / TEXT("PathTracerCapture");
        IFileManager::Get().MakeDirectory(*TempDirectory, true);
        const FString Prefix = ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial ? TEXT("PostProcessAlpha") : TEXT("WorldNormal");
        PendingAuxiliaryAlphaCapturePaths.Reserve(ActiveRequest.AlphaStackSampleCount);
        for (int32 CaptureIndex = 0; CaptureIndex < ActiveRequest.AlphaStackSampleCount; ++CaptureIndex)
        {
            const FString CapturePath = TempDirectory / FString::Printf(
                TEXT("%s_%s_%d.png"),
                *Prefix,
                *FGuid::NewGuid().ToString(EGuidFormats::Digits),
                CaptureIndex + 1);
            PendingAuxiliaryAlphaCapturePaths.Add(CapturePath);
            IFileManager::Get().Delete(*CapturePath, false, true);
        }
        PendingAuxiliaryAlphaCapturePath = PendingAuxiliaryAlphaCapturePaths[0];

        ViewportClient->AddRealtimeOverride(false, PathTracerCapture::GetRealtimeOverrideName());
        ViewportSnapshot.bRealtimeOverridden = true;
    }
    IFileManager::Get().Delete(*PendingOutputPath, false, true);

    SaveCVar(PathTracerCapture::CVarPathTracing);
    SaveCVar(PathTracerCapture::CVarPathTracingSPP);
    SaveCVar(PathTracerCapture::CVarPathTracingBackgroundAlpha);
    SaveCVar(PathTracerCapture::CVarPathTracingDenoiser);
    SaveCVar(PathTracerCapture::CVarHighResDelay);
    if (Request.AlphaMode != EPathTracerCaptureAlphaMode::None)
    {
        SaveCVarFloat(PathTracerCapture::CVarScreenPercentage);
    }
    if (Request.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial)
    {
        SaveCVar(PathTracerCapture::CVarPostProcessAAQuality);
        SaveCVar(PathTracerCapture::CVarBloomQuality);
        SaveCVar(PathTracerCapture::CVarEyeAdaptationQuality);
        SaveCVar(PathTracerCapture::CVarLocalExposure);
        SaveCVar(PathTracerCapture::CVarMotionBlurQuality);
        SaveCVar(PathTracerCapture::CVarDepthOfFieldQuality);
        SaveCVar(PathTracerCapture::CVarLensFlareQuality);
        SaveCVar(PathTracerCapture::CVarSceneColorFringeQuality);
    }

    SetCVarInt(PathTracerCapture::CVarPathTracing, 1);
    SetCVarInt(PathTracerCapture::CVarPathTracingSPP, Request.TargetSPP);
    SetCVarInt(PathTracerCapture::CVarPathTracingBackgroundAlpha, 1);
    SetCVarInt(PathTracerCapture::CVarPathTracingDenoiser, Request.bEnableDenoiser ? 1 : 0);
    SetCVarInt(PathTracerCapture::CVarHighResDelay, Request.TargetSPP);

    ViewportClient->SetViewMode(VMI_PathTracing);
    ViewportClient->Invalidate();

    FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
    const FIntPoint Resolution = FPathTracerCaptureVersionCompat::ClampResolution(Request.ResolutionX, Request.ResolutionY);
    Config.SetResolution(Resolution.X, Resolution.Y, 1.0f);
    Config.SetFilename(PendingOutputPath);
    Config.SetMaskEnabled(false);
    Config.bCaptureHDR = false;

    SetStatus(
        EPathTracerCapturePhase::Accumulating,
        bViewportAuxiliaryAlphaCaptureRequired
            ? FString::Printf(TEXT("正在开始视口路径追踪捕获（%dx%d，%d spp）。接下来将运行%s Alpha通道采集..."), Resolution.X, Resolution.Y, Request.TargetSPP, ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial ? TEXT("场景Alpha通道") : TEXT("世界法线"))
            : FString::Printf(TEXT("正在开始视口路径追踪捕获（%dx%d，%d spp）..."), Resolution.X, Resolution.Y, Request.TargetSPP),
        0);

    if (!ActiveViewport->TakeHighResScreenShot())
    {
        LastResult.ErrorMessage = TEXT("触发视口路径追踪捕获失败。");
        return false;
    }

    return true;
}

bool UPathTracerCaptureEditorSubsystem::StartViewportAuxiliaryAlphaCapture()
{
    if (!ViewportSnapshot.bValid || !ViewportSnapshot.Viewport || !ViewportSnapshot.Client)
    {
        LastResult.ErrorMessage = TEXT("辅助Alpha捕获前视口状态无效。");
        return false;
    }

    if (PendingAuxiliaryAlphaCapturePath.IsEmpty())
    {
        LastResult.ErrorMessage = TEXT("辅助Alpha输出路径为空。");
        return false;
    }

    // Auxiliary captures render at the configured screen percentage while preserving the requested PNG size.
    if (!bScreenPercentageRaisedForAuxiliaryCapture)
    {
        const float OriginalScreenPercentage = GetCVarFloat(PathTracerCapture::CVarScreenPercentage, 100.0f);
        const float AuxiliaryScreenPercentage = ActiveRequest.AlphaAuxiliaryScreenPercentage;
        SetCVarFloat(PathTracerCapture::CVarScreenPercentage, AuxiliaryScreenPercentage);
        StatusLog.Add(FString::Printf(
            TEXT("[信息] Alpha辅助采集将 r.ScreenPercentage 从 %.3f 提高到 %.3f；结束时恢复原值。"),
            static_cast<double>(OriginalScreenPercentage),
            static_cast<double>(AuxiliaryScreenPercentage)));
        bScreenPercentageRaisedForAuxiliaryCapture = true;
    }

    if (ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial)
    {
        if (!bViewportAuxiliaryAlphaCapturePrepared)
        {
            const FSoftObjectPath MaterialPath =
                ActiveRequest.AlphaPostProcessMaterial.IsNull()
                    ? UPathTracerCaptureSettings::GetDefaultAlphaPostProcessMaterialPath()
                    : ActiveRequest.AlphaPostProcessMaterial;
            UObject* LoadedObject = MaterialPath.TryLoad();
            TemporaryAlphaPostProcessMaterial = Cast<UMaterialInterface>(LoadedObject);
            if (!TemporaryAlphaPostProcessMaterial)
            {
                LastResult.ErrorMessage = FString::Printf(
                    TEXT("无法加载场景Alpha通道材质：%s。请在项目设置中设置有效的材质资源路径。"),
                    *MaterialPath.ToString());
                return false;
            }

            UWorld* WorldForAlphaCapture = ResolveCaptureWorld();
            if (!WorldForAlphaCapture)
            {
                LastResult.ErrorMessage = TEXT("场景Alpha通道捕获所需的捕获世界不可用。");
                return false;
            }

            DisableOtherPostProcessVolumesForAlphaCapture(WorldForAlphaCapture);

            if (TemporaryAlphaPostProcessVolume)
            {
                TemporaryAlphaPostProcessVolume->Destroy();
                TemporaryAlphaPostProcessVolume = nullptr;
            }

            FActorSpawnParameters SpawnParams;
            SpawnParams.ObjectFlags = RF_Transient;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            TemporaryAlphaPostProcessVolume = WorldForAlphaCapture->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
            if (!TemporaryAlphaPostProcessVolume)
            {
                LastResult.ErrorMessage = TEXT("创建用于Alpha捕获的临时后处理体积失败。");
                return false;
            }

            TemporaryAlphaPostProcessVolume->bUnbound = true;
            TemporaryAlphaPostProcessVolume->Priority = 100000.0f;
            TemporaryAlphaPostProcessVolume->BlendWeight = 1.0f;
            TemporaryAlphaPostProcessVolume->Settings = FPostProcessSettings();
            TemporaryAlphaPostProcessVolume->Settings.AddBlendable(TemporaryAlphaPostProcessMaterial.Get(), 1.0f);

            SetCVarInt(PathTracerCapture::CVarPathTracing, 0);
            SetCVarInt(PathTracerCapture::CVarHighResDelay, 1);
            SetCVarInt(PathTracerCapture::CVarPostProcessAAQuality, 0);
            SetCVarInt(PathTracerCapture::CVarBloomQuality, 0);
            SetCVarInt(PathTracerCapture::CVarEyeAdaptationQuality, 0);
            SetCVarInt(PathTracerCapture::CVarLocalExposure, 0);
            SetCVarInt(PathTracerCapture::CVarMotionBlurQuality, 0);
            SetCVarInt(PathTracerCapture::CVarDepthOfFieldQuality, 0);
            SetCVarInt(PathTracerCapture::CVarLensFlareQuality, 0);
            SetCVarInt(PathTracerCapture::CVarSceneColorFringeQuality, 0);
            ViewportSnapshot.Client->ChangeBufferVisualizationMode(NAME_None);
            ViewportSnapshot.Client->SetViewMode(VMI_Lit);
            ViewportSnapshot.Client->Invalidate();
            StatusLog.Add(FString::Printf(TEXT("[信息] 场景Alpha通道采集世界：%s"), *WorldForAlphaCapture->GetName()));
            StatusLog.Add(TEXT("[信息] 已强制场景Alpha通道设置：EyeAdaptation=0、LocalExposure=0、AA/Bloom/DOF/MotionBlur/LensFlare/Fringe=0。"));
            UpdatedEvent.Broadcast();

            bViewportAuxiliaryAlphaCapturePrepared = true;
            ViewportAuxiliaryAlphaWarmupFramesRemaining = 1;
            SetStatus(EPathTracerCapturePhase::Encoding, TEXT("正在预热后处理Alpha通道（1帧）..."), ActiveRequest.TargetSPP);
            return true;
        }

        if (ViewportAuxiliaryAlphaWarmupFramesRemaining > 0)
        {
            --ViewportAuxiliaryAlphaWarmupFramesRemaining;
            SetStatus(EPathTracerCapturePhase::Encoding, TEXT("场景Alpha通道已预热，正在捕获下一帧..."), ActiveRequest.TargetSPP);
            return true;
        }
    }
    else
    {
        SetCVarInt(PathTracerCapture::CVarPathTracing, 0);
        SetCVarInt(PathTracerCapture::CVarHighResDelay, 1);
        ViewportSnapshot.Client->ChangeBufferVisualizationMode(FName(TEXT("WorldNormal")));
        ViewportSnapshot.Client->SetViewMode(VMI_VisualizeBuffer);
        ViewportSnapshot.Client->Invalidate();
    }

    FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
    const FIntPoint Resolution = FPathTracerCaptureVersionCompat::ClampResolution(ActiveRequest.ResolutionX, ActiveRequest.ResolutionY);
    Config.SetResolution(Resolution.X, Resolution.Y, 1.0f);
    Config.SetFilename(PendingAuxiliaryAlphaCapturePath);
    Config.SetMaskEnabled(false);
    Config.bCaptureHDR = false;

    SetStatus(
        EPathTracerCapturePhase::Encoding,
        ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial
            ? TEXT("正在捕获场景Alpha通道...")
            : TEXT("正在捕获世界法线通道以合成Alpha..."),
        ActiveRequest.TargetSPP);
    if (!ViewportSnapshot.Viewport->TakeHighResScreenShot())
    {
        LastResult.ErrorMessage = TEXT("触发辅助Alpha捕获失败。");
        return false;
    }

    bViewportAuxiliaryAlphaCaptureInProgress = true;
    return true;
}

bool UPathTracerCaptureEditorSubsystem::FinalizeAlphaOutput(const FString& SourcePngFile, FString& OutPrimaryOutputFile)
{
    OutPrimaryOutputFile = SourcePngFile;

    if (ActiveRequest.AlphaMode == EPathTracerCaptureAlphaMode::None)
    {
        return true;
    }

    int32 MainWidth = 0;
    int32 MainHeight = 0;
    TArray64<uint8> MainRawData;
    if (!PathTracerCapture::DecodePngToBgra8(SourcePngFile, MainWidth, MainHeight, MainRawData))
    {
        return false;
    }

    if (PendingAuxiliaryAlphaCapturePaths.Num() != ActiveRequest.AlphaStackSampleCount)
    {
        StatusLog.Add(TEXT("[错误] 未找到完整的辅助Alpha捕获文件堆栈。"));
        UpdatedEvent.Broadcast();
        return false;
    }

    int32 AuxWidth = 0;
    int32 AuxHeight = 0;
    TArray64<uint8> AuxRawData;
    if (!PathTracerCapture::BuildAveragedMaskFromPngFiles(
        PendingAuxiliaryAlphaCapturePaths,
        ActiveRequest.AlphaSource,
        AuxWidth,
        AuxHeight,
        AuxRawData))
    {
        StatusLog.Add(FString::Printf(TEXT("[错误] 辅助Alpha%d帧堆栈平均失败。"), ActiveRequest.AlphaStackSampleCount));
        UpdatedEvent.Broadcast();
        return false;
    }

    if (AuxWidth != MainWidth || AuxHeight != MainHeight)
    {
        StatusLog.Add(TEXT("[错误] 辅助Alpha捕获分辨率与颜色输出不匹配。"));
        UpdatedEvent.Broadcast();
        return false;
    }

    StatusLog.Add(FString::Printf(TEXT("[信息] 已完成辅助Alpha%d帧逐像素堆栈平均（%dx%d），即将进入掩码与映射。"), ActiveRequest.AlphaStackSampleCount, AuxWidth, AuxHeight));
    UpdatedEvent.Broadcast();

    // BuildAveragedMaskFromPngFiles already converts each frame into a scalar
    // mask before averaging, preserving partial coverage at WorldNormal edges.
    TArray64<uint8>& MaskRawData = AuxRawData;

    if (!ActiveRequest.bUseRawAlphaMask)
    {
        constexpr uint8 LevelsInputBlack = 0;
        constexpr uint8 LevelsInputWhite = 210;
        constexpr float LevelsGamma = 1.0f;
        const bool bLevelsChanged = PathTracerCapture::ApplyPhotoshopLikeLevels(
            MaskRawData,
            LevelsInputBlack,
            LevelsInputWhite,
            LevelsGamma);
        const bool bPowerChanged = PathTracerCapture::ApplyPowerToMask(MaskRawData, ActiveRequest.AlphaPower);
        StatusLog.Add(
            FString::Printf(
                TEXT("[信息] Alpha映射顺序：先 Levels（输入黑=%d、输入白=%d、伽马=%.2f，%s），再 Power=%.3f（%s）。"),
                static_cast<int32>(LevelsInputBlack),
                static_cast<int32>(LevelsInputWhite),
                static_cast<double>(LevelsGamma),
                bLevelsChanged ? TEXT("已变更") : TEXT("结果不变"),
                static_cast<double>(ActiveRequest.AlphaPower),
                bPowerChanged ? TEXT("已变更") : TEXT("结果不变")));
        UpdatedEvent.Broadcast();
    }
    else
    {
        StatusLog.Add(TEXT("[信息] 已启用原始Alpha：跳过 Levels 和 Power 重映射。"));
        UpdatedEvent.Broadcast();
    }

    if (ActiveRequest.AlphaMode == EPathTracerCaptureAlphaMode::MergeAlpha)
    {
        PathTracerCapture::ApplyMaskToMainAlpha(MainRawData, MaskRawData);
        if (!PathTracerCapture::EncodePngFromBgra8(SourcePngFile, MainWidth, MainHeight, MainRawData))
        {
            return false;
        }

        StatusLog.Add(
            ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial
                ? TEXT("[信息] 已将场景Alpha通道合并到RGBA输出。")
                : TEXT("[信息] 已将世界法线二值掩码合并到RGBA输出。"));
        UpdatedEvent.Broadcast();
        return true;
    }

    if (ActiveRequest.AlphaMode == EPathTracerCaptureAlphaMode::SeparateAlphaTexture)
    {
        const FString AlphaMaskPath = PathTracerCapture::BuildAlphaMaskFilePath(SourcePngFile, TEXT("_Alpha"));
        if (!PathTracerCapture::EncodePngFromBgra8(AlphaMaskPath, MainWidth, MainHeight, MaskRawData))
        {
            return false;
        }

        StatusLog.Add(FString::Printf(TEXT("[信息] 已写入分离的Alpha贴图：%s"), *AlphaMaskPath));
        UpdatedEvent.Broadcast();
        return true;
    }

    return true;
}

UWorld* UPathTracerCaptureEditorSubsystem::ResolveCaptureWorld() const
{
    if (CaptureWorld.IsValid())
    {
        return CaptureWorld.Get();
    }

    if (ViewportSnapshot.Client && ViewportSnapshot.Client->GetWorld())
    {
        return ViewportSnapshot.Client->GetWorld();
    }

    if (GEditor && GEditor->PlayWorld)
    {
        return GEditor->PlayWorld;
    }

    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void UPathTracerCaptureEditorSubsystem::DisableOtherPostProcessVolumesForAlphaCapture(UWorld* InCaptureWorld)
{
    DisabledPostProcessVolumes.Reset();

    if (!InCaptureWorld)
    {
        return;
    }

    for (TActorIterator<APostProcessVolume> It(InCaptureWorld); It; ++It)
    {
        APostProcessVolume* Volume = *It;
        if (!Volume || Volume == TemporaryAlphaPostProcessVolume.Get())
        {
            continue;
        }

        FPostProcessVolumeState State;
        State.Volume = Volume;
        State.bWasEnabled = Volume->bEnabled;
        DisabledPostProcessVolumes.Add(State);

        if (Volume->bEnabled)
        {
            Volume->bEnabled = false;
        }
    }

    if (DisabledPostProcessVolumes.Num() > 0)
    {
        StatusLog.Add(FString::Printf(TEXT("[信息] 已隔离后处理Alpha通道：禁用了场景中的%d个后处理体积。"), DisabledPostProcessVolumes.Num()));
        UpdatedEvent.Broadcast();
    }
}

void UPathTracerCaptureEditorSubsystem::RestoreOtherPostProcessVolumes()
{
    for (const FPostProcessVolumeState& State : DisabledPostProcessVolumes)
    {
        if (APostProcessVolume* Volume = State.Volume.Get())
        {
            Volume->bEnabled = State.bWasEnabled;
        }
    }
    DisabledPostProcessVolumes.Reset();
}

void UPathTracerCaptureEditorSubsystem::SaveCVar(const FString& Name)
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        CVarSnapshot.IntValues.FindOrAdd(Name) = Var->GetInt();
    }
}

void UPathTracerCaptureEditorSubsystem::SaveCVarFloat(const FString& Name)
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        if (!CVarSnapshot.FloatValues.Contains(Name))
        {
            CVarSnapshot.FloatValues.Add(Name, Var->GetFloat());
        }
    }
}

void UPathTracerCaptureEditorSubsystem::RestoreCVars()
{
    for (const TPair<FString, int32>& Pair : CVarSnapshot.IntValues)
    {
        SetCVarInt(Pair.Key, Pair.Value);
    }
    for (const TPair<FString, float>& Pair : CVarSnapshot.FloatValues)
    {
        SetCVarFloat(Pair.Key, Pair.Value);
    }
    CVarSnapshot.IntValues.Reset();
    CVarSnapshot.FloatValues.Reset();
}

int32 UPathTracerCaptureEditorSubsystem::GetCVarInt(const FString& Name, int32 DefaultValue) const
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        return Var->GetInt();
    }
    return DefaultValue;
}

void UPathTracerCaptureEditorSubsystem::SetCVarInt(const FString& Name, int32 Value) const
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        Var->Set(Value, ECVF_SetByCode);
    }
}

float UPathTracerCaptureEditorSubsystem::GetCVarFloat(const FString& Name, float DefaultValue) const
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        return Var->GetFloat();
    }
    return DefaultValue;
}

void UPathTracerCaptureEditorSubsystem::SetCVarFloat(const FString& Name, float Value) const
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
    {
        Var->Set(Value, ECVF_SetByCode);
    }
}
