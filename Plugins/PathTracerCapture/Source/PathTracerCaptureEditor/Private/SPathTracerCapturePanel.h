#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
struct FPropertyAndParent;

class SPathTracerCapturePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPathTracerCapturePanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FReply OnRenderClicked();
    FReply OnCancelClicked();
    FReply OnResetSettingsClicked();
    FReply OnOpenOutputDirectoryClicked();
    bool IsRenderEnabled() const;
    bool IsCancelEnabled() const;
    bool IsOpenOutputDirectoryEnabled() const;
    bool IsSettingsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const;
    FText GetPhaseText() const;
    FText GetStatusText() const;
    FText GetLogText() const;

private:
    TSharedPtr<IDetailsView> DetailsView;
};
