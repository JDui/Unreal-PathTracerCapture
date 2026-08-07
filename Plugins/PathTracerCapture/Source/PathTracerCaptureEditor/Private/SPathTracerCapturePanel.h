#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class UMaterialInterface;
struct FPropertyAndParent;
struct FPropertyChangedEvent;

class SPathTracerCapturePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPathTracerCapturePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SPathTracerCapturePanel() override;

private:
    FReply OnRenderClicked();
    FReply OnCancelClicked();
    FReply OnResetSettingsClicked();
    FReply OnOpenOutputDirectoryClicked();
    FReply OnPreWarmAlphaClicked();
    bool OnPreWarmAlphaTick(float DeltaTime);
    bool IsRenderEnabled() const;
    bool IsCancelEnabled() const;
    bool IsOpenOutputDirectoryEnabled() const;
    bool IsPreWarmAlphaEnabled() const;
    EVisibility GetPreWarmAlphaVisibility() const;
    bool IsSettingsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const;
    void OnSettingsPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);
    void UpdateAlphaModeState();
    FText GetPreWarmAlphaText() const;
    FText GetPhaseText() const;
    FText GetStatusText() const;
    FText GetLogText() const;

private:
    TSharedPtr<IDetailsView> DetailsView;
    TStrongObjectPtr<UMaterialInterface> PreWarmMaterial;
    FTSTicker::FDelegateHandle PreWarmTickHandle;
    bool bIsAlphaModeSelected = false;
    bool bIsPreWarmingAlpha = false;
};
