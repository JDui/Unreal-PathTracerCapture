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

    static FString BuildAlphaMaskFilePath(const FString& SourcePngFile, const FString& Suffix)
    {
        const FString Directory = FPaths::GetPath(SourcePngFile);
        const FString BaseName = FPaths::GetBaseFilename(SourcePngFile);
        return Directory / (BaseName + Suffix + TEXT(".png"));
    }

    static void BuildBinaryMaskFromWorldNormal(const TArray64<uint8>& WorldNormalRawData, TArray64<uint8>& OutMaskRawData)
    {
        OutMaskRawData.SetNumUninitialized(WorldNormalRawData.Num());
        const int64 PixelCount = WorldNormalRawData.Num() / 4;
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const int64 Base = PixelIndex * 4;
            const bool bIsBlack = WorldNormalRawData[Base + 0] == 0 && WorldNormalRawData[Base + 1] == 0 && WorldNormalRawData[Base + 2] == 0;
            const uint8 MaskValue = bIsBlack ? 0 : 255;
            OutMaskRawData[Base + 0] = MaskValue;
            OutMaskRawData[Base + 1] = MaskValue;
            OutMaskRawData[Base + 2] = MaskValue;
            OutMaskRawData[Base + 3] = 255;
        }
    }

    static void BuildMaskFromPostProcessAlphaPass(const TArray64<uint8>& PostProcessRawData, TArray64<uint8>& OutMaskRawData)
    {
        OutMaskRawData.SetNumUninitialized(PostProcessRawData.Num());
        const int64 PixelCount = PostProcessRawData.Num() / 4;
        for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const int64 Base = PixelIndex * 4;
            const uint8 MaskValue = PostProcessRawData[Base + 0];
            OutMaskRawData[Base + 0] = MaskValue;
            OutMaskRawData[Base + 1] = MaskValue;
            OutMaskRawData[Base + 2] = MaskValue;
            OutMaskRawData[Base + 3] = 255;
        }
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

    static bool ApplyPhotoshopLikeLevels(TArray64<uint8>& InOutMaskRawData, const uint8 InputBlack, const uint8 InputWhite, const float Gamma)
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
        OutReason = FText::FromString(TEXT("A capture is already running."));
        return false;
    }

    return FPathTracerCaptureVersionCompat::IsPathTracingSupported(OutReason);
}

FString UPathTracerCaptureEditorSubsystem::GetStatusLogText() const
{
    return FString::Join(StatusLog, TEXT("\n"));
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
    SetStatus(EPathTracerCapturePhase::Preparing, TEXT("Preparing viewport path tracing capture..."), 0);

    if (!StartViewportCapture(ActiveRequest))
    {
        FinishCapture(false, LastResult.ErrorMessage.IsEmpty() ? TEXT("Viewport capture failed to start.") : LastResult.ErrorMessage);
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

    FinishCapture(false, TEXT("Capture cancelled by user."));
    SetStatus(EPathTracerCapturePhase::Cancelled, TEXT("Cancelled."));
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
        FinishCapture(false, TEXT("Capture timed out after 10 minutes."));
        return true;
    }

    const double Elapsed = NowSeconds - CaptureStartSeconds;
    const double Normalized = FMath::Clamp(Elapsed / FMath::Max(1.0, ActiveRequest.TargetSPP / 12.0), 0.0, 1.0);
    const int32 EstimatedSPP = FMath::Clamp(FMath::RoundToInt(ActiveRequest.TargetSPP * Normalized), 0, ActiveRequest.TargetSPP);
    if (NowSeconds - LastProgressUpdateSeconds > 0.2)
    {
        SetStatus(EPathTracerCapturePhase::Accumulating, FString::Printf(TEXT("Accumulating viewport path tracing samples... (%d/%d)"), EstimatedSPP, ActiveRequest.TargetSPP), EstimatedSPP);
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

    if (bViewportAuxiliaryAlphaCaptureRequired && !IFileManager::Get().FileExists(*PendingAuxiliaryAlphaCapturePath))
    {
        if (!bViewportAuxiliaryAlphaCaptureInProgress)
        {
            if (!StartViewportAuxiliaryAlphaCapture())
            {
                FinishCapture(false, LastResult.ErrorMessage.IsEmpty() ? TEXT("Failed to capture auxiliary alpha channel.") : LastResult.ErrorMessage);
            }
        }
        return true;
    }
    bViewportAuxiliaryAlphaCaptureInProgress = false;

    SetStatus(EPathTracerCapturePhase::Encoding, TEXT("Encoding PNG output..."), ActiveRequest.TargetSPP);
    FString PrimaryOutputFile = PendingOutputPath;
    if (!FinalizeAlphaOutput(PendingOutputPath, PrimaryOutputFile))
    {
        FinishCapture(false, TEXT("Failed to post-process alpha output."));
        return true;
    }

    LastResult.OutputFile = PrimaryOutputFile;
    FinishCapture(true, FString());
    return true;
}

bool UPathTracerCaptureEditorSubsystem::ValidateRequest(const FPathTracerCaptureRequest& Request, FString& OutError) const
{
    if (Request.ResolutionX < 16 || Request.ResolutionY < 16)
    {
        OutError = TEXT("Resolution must be at least 16x16.");
        return false;
    }

    if (Request.TargetSPP <= 0)
    {
        OutError = TEXT("TargetSPP must be greater than 0.");
        return false;
    }

    if (Request.OutputDirectory.IsEmpty())
    {
        OutError = TEXT("Output directory is empty.");
        return false;
    }

    if (!IFileManager::Get().DirectoryExists(*Request.OutputDirectory))
    {
        if (!IFileManager::Get().MakeDirectory(*Request.OutputDirectory, true))
        {
            OutError = FString::Printf(TEXT("Failed to create output directory: %s"), *Request.OutputDirectory);
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

    const FString LogLine = FString::Printf(TEXT("[%s] %s"), *UEnum::GetValueAsString(Phase), *Message);
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
        Progress.StatusMessage = TEXT("Capture completed.");
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
    RestoreCVars();
    RestoreOtherPostProcessVolumes();
    if (!PendingAuxiliaryAlphaCapturePath.IsEmpty())
    {
        IFileManager::Get().Delete(*PendingAuxiliaryAlphaCapturePath, false, true);
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
    PendingAuxiliaryAlphaCapturePath.Reset();
    PendingOutputPath.Reset();
    PendingOutputPrefix.Reset();
    CaptureWorld.Reset();
}

bool UPathTracerCaptureEditorSubsystem::StartViewportCapture(const FPathTracerCaptureRequest& Request)
{
    if (!GEditor)
    {
        LastResult.ErrorMessage = TEXT("Editor is not available.");
        return false;
    }

    FViewport* ActiveViewport = GEditor->GetActiveViewport();
    if (!ActiveViewport)
    {
        LastResult.ErrorMessage = TEXT("No active viewport found.");
        return false;
    }

    FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(ActiveViewport->GetClient());
    if (!ViewportClient)
    {
        LastResult.ErrorMessage = TEXT("Active viewport is not an editor viewport.");
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
    PendingAuxiliaryAlphaCapturePath.Reset();

    if (bViewportAuxiliaryAlphaCaptureRequired)
    {
        const FString TempDirectory = FPaths::ProjectIntermediateDir() / TEXT("PathTracerCapture");
        IFileManager::Get().MakeDirectory(*TempDirectory, true);
        const FString Prefix = ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial ? TEXT("PostProcessAlpha") : TEXT("WorldNormal");
        PendingAuxiliaryAlphaCapturePath =
            TempDirectory / FString::Printf(TEXT("%s_%s.png"), *Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        IFileManager::Get().Delete(*PendingAuxiliaryAlphaCapturePath, false, true);

        ViewportClient->AddRealtimeOverride(false, PathTracerCapture::GetRealtimeOverrideName());
        ViewportSnapshot.bRealtimeOverridden = true;
    }
    IFileManager::Get().Delete(*PendingOutputPath, false, true);

    SaveCVar(PathTracerCapture::CVarPathTracing);
    SaveCVar(PathTracerCapture::CVarPathTracingSPP);
    SaveCVar(PathTracerCapture::CVarPathTracingBackgroundAlpha);
    SaveCVar(PathTracerCapture::CVarPathTracingDenoiser);
    SaveCVar(PathTracerCapture::CVarHighResDelay);
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
            ? FString::Printf(TEXT("Starting viewport path tracing capture (%dx%d, %d spp). %s alpha pass will run next..."), Resolution.X, Resolution.Y, Request.TargetSPP, ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial ? TEXT("PostProcess") : TEXT("WorldNormal"))
            : FString::Printf(TEXT("Starting viewport path tracing capture (%dx%d, %d spp)..."), Resolution.X, Resolution.Y, Request.TargetSPP),
        0);

    if (!ActiveViewport->TakeHighResScreenShot())
    {
        LastResult.ErrorMessage = TEXT("Failed to trigger viewport path tracing capture.");
        return false;
    }

    return true;
}

bool UPathTracerCaptureEditorSubsystem::StartViewportAuxiliaryAlphaCapture()
{
    if (!ViewportSnapshot.bValid || !ViewportSnapshot.Viewport || !ViewportSnapshot.Client)
    {
        LastResult.ErrorMessage = TEXT("Viewport state was invalid before auxiliary alpha capture.");
        return false;
    }

    if (PendingAuxiliaryAlphaCapturePath.IsEmpty())
    {
        LastResult.ErrorMessage = TEXT("Auxiliary alpha output path is empty.");
        return false;
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
                    TEXT("Failed to load alpha post-process material: %s. Set a valid material asset path."),
                    *MaterialPath.ToString());
                return false;
            }

            UWorld* WorldForAlphaCapture = ResolveCaptureWorld();
            if (!WorldForAlphaCapture)
            {
                LastResult.ErrorMessage = TEXT("Capture world is unavailable for alpha post-process capture.");
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
                LastResult.ErrorMessage = TEXT("Failed to create temporary post-process volume for alpha capture.");
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
            StatusLog.Add(FString::Printf(TEXT("[Info] Post alpha capture world: %s"), *WorldForAlphaCapture->GetName()));
            StatusLog.Add(TEXT("[Info] Post alpha pass forced: EyeAdaptation=0, LocalExposure=0, AA/Bloom/DOF/MotionBlur/LensFlare/Fringe=0."));
            UpdatedEvent.Broadcast();

            bViewportAuxiliaryAlphaCapturePrepared = true;
            ViewportAuxiliaryAlphaWarmupFramesRemaining = 1;
            SetStatus(EPathTracerCapturePhase::Encoding, TEXT("Warming up post-process alpha pass (1 frame)..."), ActiveRequest.TargetSPP);
            return true;
        }

        if (ViewportAuxiliaryAlphaWarmupFramesRemaining > 0)
        {
            --ViewportAuxiliaryAlphaWarmupFramesRemaining;
            SetStatus(EPathTracerCapturePhase::Encoding, TEXT("Post-process alpha pass warmed up. Capturing next frame..."), ActiveRequest.TargetSPP);
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
            ? TEXT("Capturing post-process alpha channel...")
            : TEXT("Capturing WorldNormal channel for alpha synthesis..."),
        ActiveRequest.TargetSPP);
    if (!ViewportSnapshot.Viewport->TakeHighResScreenShot())
    {
        LastResult.ErrorMessage = TEXT("Failed to trigger auxiliary alpha capture.");
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

    if (PendingAuxiliaryAlphaCapturePath.IsEmpty() || !IFileManager::Get().FileExists(*PendingAuxiliaryAlphaCapturePath))
    {
        StatusLog.Add(TEXT("[Error] Auxiliary alpha capture file was not found."));
        UpdatedEvent.Broadcast();
        return false;
    }

    int32 AuxWidth = 0;
    int32 AuxHeight = 0;
    TArray64<uint8> AuxRawData;
    if (!PathTracerCapture::DecodePngToBgra8(PendingAuxiliaryAlphaCapturePath, AuxWidth, AuxHeight, AuxRawData))
    {
        StatusLog.Add(TEXT("[Error] Failed to decode auxiliary alpha capture."));
        UpdatedEvent.Broadcast();
        return false;
    }

    if (AuxWidth != MainWidth || AuxHeight != MainHeight)
    {
        StatusLog.Add(TEXT("[Error] Auxiliary alpha capture resolution does not match color output."));
        UpdatedEvent.Broadcast();
        return false;
    }

    TArray64<uint8> MaskRawData;
    if (ActiveRequest.AlphaSource == EPathTracerCaptureAlphaSource::PostProcessMaterial)
    {
        PathTracerCapture::BuildMaskFromPostProcessAlphaPass(AuxRawData, MaskRawData);
    }
    else
    {
        PathTracerCapture::BuildBinaryMaskFromWorldNormal(AuxRawData, MaskRawData);
    }

    if (!ActiveRequest.bUseRawAlphaMask)
    {
        constexpr uint8 LevelsInputBlack = 0;
        constexpr uint8 LevelsInputWhite = 210;
        constexpr float LevelsGamma = 1.0f;
        const bool bRemapped = PathTracerCapture::ApplyPhotoshopLikeLevels(MaskRawData, LevelsInputBlack, LevelsInputWhite, LevelsGamma);
        if (bRemapped)
        {
            StatusLog.Add(
                FString::Printf(
                    TEXT("[Info] Remapped alpha with Levels(InputBlack=%d, Gamma=%.2f, InputWhite=%d)."),
                    static_cast<int32>(LevelsInputBlack),
                    static_cast<double>(LevelsGamma),
                    static_cast<int32>(LevelsInputWhite)));
            UpdatedEvent.Broadcast();
        }
    }
    else
    {
        StatusLog.Add(TEXT("[Info] Raw alpha enabled: skipped Levels alpha remap."));
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
                ? TEXT("[Info] Merged alpha from post-process alpha pass into RGBA output.")
                : TEXT("[Info] Merged alpha from WorldNormal binary mask into RGBA output."));
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

        StatusLog.Add(FString::Printf(TEXT("[Info] Wrote separate alpha texture: %s"), *AlphaMaskPath));
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
        StatusLog.Add(FString::Printf(TEXT("[Info] Isolated post alpha pass: disabled %d scene post process volumes."), DisabledPostProcessVolumes.Num()));
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

void UPathTracerCaptureEditorSubsystem::RestoreCVars()
{
    for (const TPair<FString, int32>& Pair : CVarSnapshot.IntValues)
    {
        SetCVarInt(Pair.Key, Pair.Value);
    }
    CVarSnapshot.IntValues.Reset();
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
