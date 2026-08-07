#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "EditorSubsystem.h"
#include "PathTracerCaptureTypes.h"
#include "PathTracerCaptureEditorSubsystem.generated.h"

class FEditorViewportClient;
class FViewport;
class APostProcessVolume;
class UMaterialInterface;
class UWorld;

DECLARE_MULTICAST_DELEGATE(FOnPathTracerCaptureUpdated);

UCLASS()
class PATHTRACERCAPTUREEDITOR_API UPathTracerCaptureEditorSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "PathTracerCapture")
    bool StartCapture(const FPathTracerCaptureRequest& Request);

    UFUNCTION(BlueprintCallable, Category = "PathTracerCapture")
    void CancelCapture();

    UFUNCTION(BlueprintPure, Category = "PathTracerCapture")
    FPathTracerCaptureResult GetLastResult() const { return LastResult; }

    UFUNCTION(BlueprintPure, Category = "PathTracerCapture")
    FPathTracerCaptureProgress GetProgress() const { return Progress; }

    UFUNCTION(BlueprintPure, Category = "PathTracerCapture")
    bool IsCaptureRunning() const { return Progress.bIsRunning; }

    bool CanStartCapture(FText& OutReason) const;
    FString GetStatusLogText() const;
    void AppendStatusLog(const FString& Message);
    const FOnPathTracerCaptureUpdated& OnCaptureUpdated() const { return UpdatedEvent; }

private:
    struct FPostProcessVolumeState
    {
        TWeakObjectPtr<APostProcessVolume> Volume;
        bool bWasEnabled = false;
    };

    struct FCVarSnapshot
    {
        TMap<FString, int32> IntValues;
    };

    struct FViewportSnapshot
    {
        FViewport* Viewport = nullptr;
        FEditorViewportClient* Client = nullptr;
        int32 ViewMode = 0;
        FName BufferVisualizationMode;
        bool bRealtimeOverridden = false;
        bool bValid = false;
    };

    bool Tick(float DeltaTime);
    bool ValidateRequest(const FPathTracerCaptureRequest& Request, FString& OutError) const;
    FString BuildOutputPath(const FPathTracerCaptureRequest& Request) const;
    void SetStatus(EPathTracerCapturePhase Phase, const FString& Message, int32 EstimatedSPP = 0);
    void FinishCapture(bool bSuccess, const FString& ErrorMessage);
    void RestoreViewportCaptureState();
    bool StartViewportCapture(const FPathTracerCaptureRequest& Request);
    bool StartViewportAuxiliaryAlphaCapture();
    bool FinalizeAlphaOutput(const FString& SourcePngFile, FString& OutPrimaryOutputFile);
    UWorld* ResolveCaptureWorld() const;
    void SaveCVar(const FString& Name);
    void RestoreCVars();
    int32 GetCVarInt(const FString& Name, int32 DefaultValue) const;
    void SetCVarInt(const FString& Name, int32 Value) const;
    void DisableOtherPostProcessVolumesForAlphaCapture(UWorld* InCaptureWorld);
    void RestoreOtherPostProcessVolumes();

private:
    FPathTracerCaptureResult LastResult;
    FPathTracerCaptureProgress Progress;
    FPathTracerCaptureRequest ActiveRequest;
    FCVarSnapshot CVarSnapshot;
    FViewportSnapshot ViewportSnapshot;
    TArray<FString> StatusLog;
    FString PendingOutputPath;
    FString PendingOutputPrefix;
    double CaptureStartSeconds = 0.0;
    double LastProgressUpdateSeconds = 0.0;
    FTSTicker::FDelegateHandle TickHandle;
    FOnPathTracerCaptureUpdated UpdatedEvent;

    bool bViewportMainCaptureCompleted = false;
    bool bViewportAuxiliaryAlphaCaptureRequired = false;
    bool bViewportAuxiliaryAlphaCapturePrepared = false;
    bool bViewportAuxiliaryAlphaCaptureInProgress = false;
    int32 ViewportAuxiliaryAlphaWarmupFramesRemaining = 0;
    FString PendingAuxiliaryAlphaCapturePath;
    TWeakObjectPtr<UWorld> CaptureWorld;

    UPROPERTY(Transient)
    TObjectPtr<APostProcessVolume> TemporaryAlphaPostProcessVolume;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> TemporaryAlphaPostProcessMaterial;

    TArray<FPostProcessVolumeState> DisabledPostProcessVolumes;
};
