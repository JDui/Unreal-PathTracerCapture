#include "PathTracerCaptureSettings.h"

#include "Misc/Paths.h"

namespace
{
    static const TCHAR* DefaultAlphaPostProcessMaterialPath = TEXT("/PathTracerCapture/Ref/MP_ALPHA.MP_ALPHA");
    static const TCHAR* LegacyProjectAlphaPostProcessMaterialPath = TEXT("/Game/Ref/MP_ALPHA.MP_ALPHA");
}

UPathTracerCaptureSettings::UPathTracerCaptureSettings()
{
    ResetToDefaults();
}

FSoftObjectPath UPathTracerCaptureSettings::GetDefaultAlphaPostProcessMaterialPath()
{
    return FSoftObjectPath(DefaultAlphaPostProcessMaterialPath);
}

void UPathTracerCaptureSettings::ResetToDefaults()
{
    Backend = EPathTracerCaptureBackend::Viewport;
    ResolutionX = 1920;
    ResolutionY = 1080;
    TargetSPP = 128;
    OutputDirectory.Path = FPaths::ProjectSavedDir() / TEXT("PathTracerCapture");

    FilenamePattern = TEXT("AXi_{date}{seq}");
    bEnableDenoiser = false;
    AlphaMode = EPathTracerCaptureAlphaMode::None;
    AlphaSource = EPathTracerCaptureAlphaSource::PostProcessMaterial;
    AlphaPostProcessMaterial = GetDefaultAlphaPostProcessMaterialPath();
    bUseRawAlphaMask = false;
    AlphaPower = 1.5f;
}

FPathTracerCaptureRequest UPathTracerCaptureSettings::MakeRequest() const
{
    FPathTracerCaptureRequest Request;
    Request.Backend = EPathTracerCaptureBackend::Viewport;
    Request.ResolutionX = ResolutionX;
    Request.ResolutionY = ResolutionY;
    Request.TargetSPP = TargetSPP;
    Request.OutputDirectory = OutputDirectory.Path;
    Request.FilenamePattern = FilenamePattern;
    Request.bEnableDenoiser = bEnableDenoiser;
    Request.AlphaMode = AlphaMode;
    Request.AlphaSource = AlphaSource;
    if (AlphaPostProcessMaterial.IsNull() || AlphaPostProcessMaterial.ToString().Equals(LegacyProjectAlphaPostProcessMaterialPath, ESearchCase::IgnoreCase))
    {
        Request.AlphaPostProcessMaterial = GetDefaultAlphaPostProcessMaterialPath();
    }
    else
    {
        Request.AlphaPostProcessMaterial = AlphaPostProcessMaterial;
    }
    Request.bUseRawAlphaMask = bUseRawAlphaMask;
    Request.AlphaPower = AlphaPower;
    return Request;
}
