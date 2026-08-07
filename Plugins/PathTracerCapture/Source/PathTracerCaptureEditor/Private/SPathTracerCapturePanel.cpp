#include "SPathTracerCapturePanel.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDetailsView.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PathTracerCaptureEditorSubsystem.h"
#include "PathTracerCaptureSettings.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

void SPathTracerCapturePanel::Construct(const FArguments& InArgs)
{
    FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

    FDetailsViewArgs Args;
    Args.bHideSelectionTip = true;
    Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    Args.bAllowSearch = true;
    Args.bShowScrollBar = true;

    DetailsView = PropertyModule.CreateDetailView(Args);
    UPathTracerCaptureSettings* Settings = GetMutableDefault<UPathTracerCaptureSettings>();
    if (Settings && (Settings->AlphaPostProcessMaterial.IsNull()
        || Settings->AlphaPostProcessMaterial.ToString().Equals(TEXT("/Game/Ref/MP_ALPHA.MP_ALPHA"), ESearchCase::IgnoreCase)))
    {
        Settings->AlphaPostProcessMaterial = UPathTracerCaptureSettings::GetDefaultAlphaPostProcessMaterialPath();
        Settings->SaveConfig();
    }
    DetailsView->SetObject(Settings);
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SPathTracerCapturePanel::IsSettingsPropertyVisible));
    DetailsView->OnFinishedChangingProperties().AddSP(this, &SPathTracerCapturePanel::OnSettingsPropertyChanged);
    UpdateAlphaModeState();

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f)
        [
            DetailsView.ToSharedRef()
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 2.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("渲染并保存")))
                .OnClicked(this, &SPathTracerCapturePanel::OnRenderClicked)
                .IsEnabled(this, &SPathTracerCapturePanel::IsRenderEnabled)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SButton)
                .Text(this, &SPathTracerCapturePanel::GetPreWarmAlphaText)
                .ToolTipText(FText::FromString(TEXT("提前加载透明通道后处理材质以触发着色器编译，一秒后自动释放引用。可在正式渲染前避免首次加载卡顿。")))
                .OnClicked(this, &SPathTracerCapturePanel::OnPreWarmAlphaClicked)
                .IsEnabled(this, &SPathTracerCapturePanel::IsPreWarmAlphaEnabled)
                .Visibility(this, &SPathTracerCapturePanel::GetPreWarmAlphaVisibility)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("打开输出文件夹")))
                .OnClicked(this, &SPathTracerCapturePanel::OnOpenOutputDirectoryClicked)
                .IsEnabled(this, &SPathTracerCapturePanel::IsOpenOutputDirectoryEnabled)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("重置")))
                .OnClicked(this, &SPathTracerCapturePanel::OnResetSettingsClicked)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("取消")))
                .OnClicked(this, &SPathTracerCapturePanel::OnCancelClicked)
                .IsEnabled(this, &SPathTracerCapturePanel::IsCancelEnabled)
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 4.0f)
        [
            SNew(SSeparator)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(this, &SPathTracerCapturePanel::GetPhaseText)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(this, &SPathTracerCapturePanel::GetStatusText)
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(8.0f, 6.0f)
        [
            SNew(SMultiLineEditableTextBox)
            .IsReadOnly(true)
            .Text(this, &SPathTracerCapturePanel::GetLogText)
        ]
    ];
}

SPathTracerCapturePanel::~SPathTracerCapturePanel()
{
    if (PreWarmTickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PreWarmTickHandle);
        PreWarmTickHandle.Reset();
    }
}

FReply SPathTracerCapturePanel::OnRenderClicked()
{
    UPathTracerCaptureSettings* Settings = GetMutableDefault<UPathTracerCaptureSettings>();
    if (Settings && (Settings->AlphaPostProcessMaterial.IsNull()
        || Settings->AlphaPostProcessMaterial.ToString().Equals(TEXT("/Game/Ref/MP_ALPHA.MP_ALPHA"), ESearchCase::IgnoreCase)))
    {
        Settings->AlphaPostProcessMaterial = UPathTracerCaptureSettings::GetDefaultAlphaPostProcessMaterialPath();
    }
    Settings->SaveConfig();

    if (GEditor)
    {
        if (UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            Subsystem->StartCapture(Settings->MakeRequest());
        }
    }
    return FReply::Handled();
}

FReply SPathTracerCapturePanel::OnCancelClicked()
{
    if (GEditor)
    {
        if (UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            Subsystem->CancelCapture();
        }
    }
    return FReply::Handled();
}

FReply SPathTracerCapturePanel::OnResetSettingsClicked()
{
    UPathTracerCaptureSettings* Settings = GetMutableDefault<UPathTracerCaptureSettings>();
    if (Settings)
    {
        Settings->ResetToDefaults();
        Settings->SaveConfig();
        UpdateAlphaModeState();
        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
        Invalidate(EInvalidateWidgetReason::LayoutAndVolatility | EInvalidateWidgetReason::Visibility);
    }
    return FReply::Handled();
}

FReply SPathTracerCapturePanel::OnOpenOutputDirectoryClicked()
{
    const UPathTracerCaptureSettings* Settings = GetDefault<UPathTracerCaptureSettings>();
    if (!Settings)
    {
        return FReply::Handled();
    }

    FString OutputDirectory = Settings->OutputDirectory.Path;
    if (OutputDirectory.IsEmpty())
    {
        OutputDirectory = FPaths::ProjectSavedDir() / TEXT("PathTracerCapture");
    }

    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    FPlatformProcess::ExploreFolder(*OutputDirectory);
    return FReply::Handled();
}

FReply SPathTracerCapturePanel::OnPreWarmAlphaClicked()
{
    UPathTracerCaptureSettings* Settings = GetMutableDefault<UPathTracerCaptureSettings>();
    if (!Settings || Settings->AlphaMode == EPathTracerCaptureAlphaMode::None)
    {
        return FReply::Handled();
    }

    FSoftObjectPath MaterialPath = Settings->AlphaPostProcessMaterial;
    if (MaterialPath.IsNull() || MaterialPath.ToString().Equals(TEXT("/Game/Ref/MP_ALPHA.MP_ALPHA"), ESearchCase::IgnoreCase))
    {
        MaterialPath = UPathTracerCaptureSettings::GetDefaultAlphaPostProcessMaterialPath();
    }

    FString Message;
    if (UObject* LoadedObject = MaterialPath.TryLoad())
    {
        PreWarmMaterial.Reset(Cast<UMaterialInterface>(LoadedObject));
        if (PreWarmMaterial)
        {
            Message = FString::Printf(TEXT("预热Alpha通道：已加载材质 %s，一秒后释放引用。"), *MaterialPath.ToString());
        }
        else
        {
            Message = FString::Printf(TEXT("预热Alpha通道：资源 %s 不是有效的材质接口。"), *MaterialPath.ToString());
        }
    }
    else
    {
        Message = FString::Printf(TEXT("预热Alpha通道：无法加载材质 %s。"), *MaterialPath.ToString());
    }

    bIsPreWarmingAlpha = true;
    Invalidate(EInvalidateWidgetReason::LayoutAndVolatility | EInvalidateWidgetReason::Visibility);

    if (PreWarmTickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(PreWarmTickHandle);
    }
    PreWarmTickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(this, &SPathTracerCapturePanel::OnPreWarmAlphaTick),
        1.0f);

    if (GEditor)
    {
        if (UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            Subsystem->AppendStatusLog(Message);
        }
    }

    return FReply::Handled();
}

bool SPathTracerCapturePanel::OnPreWarmAlphaTick(float DeltaTime)
{
    PreWarmMaterial.Reset();
    bIsPreWarmingAlpha = false;
    PreWarmTickHandle.Reset();
    Invalidate(EInvalidateWidgetReason::LayoutAndVolatility | EInvalidateWidgetReason::Visibility);

    if (GEditor)
    {
        if (UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            Subsystem->AppendStatusLog(TEXT("预热Alpha通道：已完成，已释放材质引用。"));
        }
    }

    return false;
}

bool SPathTracerCapturePanel::IsRenderEnabled() const
{
    if (!GEditor)
    {
        return false;
    }

    if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
    {
        FText Reason;
        return Subsystem->CanStartCapture(Reason);
    }
    return false;
}

bool SPathTracerCapturePanel::IsCancelEnabled() const
{
    if (!GEditor)
    {
        return false;
    }

    if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
    {
        return Subsystem->IsCaptureRunning();
    }
    return false;
}

bool SPathTracerCapturePanel::IsOpenOutputDirectoryEnabled() const
{
    const UPathTracerCaptureSettings* Settings = GetDefault<UPathTracerCaptureSettings>();
    return Settings != nullptr && !Settings->OutputDirectory.Path.IsEmpty();
}

bool SPathTracerCapturePanel::IsPreWarmAlphaEnabled() const
{
    if (!bIsAlphaModeSelected || bIsPreWarmingAlpha)
    {
        return false;
    }

    if (GEditor)
    {
        if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            return !Subsystem->IsCaptureRunning();
        }
    }
    return false;
}

EVisibility SPathTracerCapturePanel::GetPreWarmAlphaVisibility() const
{
    return bIsAlphaModeSelected ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SPathTracerCapturePanel::GetPreWarmAlphaText() const
{
    return FText::FromString(bIsPreWarmingAlpha ? TEXT("预热中...") : TEXT("预热Alpha通道"));
}

bool SPathTracerCapturePanel::IsSettingsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const
{
    if (PropertyAndParent.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UPathTracerCaptureSettings, Backend))
    {
        return false;
    }

    if (PropertyAndParent.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UPathTracerCaptureSettings, AlphaPostProcessMaterial))
    {
        const UPathTracerCaptureSettings* Settings = GetDefault<UPathTracerCaptureSettings>();
        return Settings != nullptr && Settings->AlphaMode != EPathTracerCaptureAlphaMode::None;
    }

    return true;
}

void SPathTracerCapturePanel::OnSettingsPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
    if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UPathTracerCaptureSettings, AlphaMode))
    {
        UpdateAlphaModeState();
        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
        Invalidate(EInvalidateWidgetReason::LayoutAndVolatility | EInvalidateWidgetReason::Visibility);
    }
}

void SPathTracerCapturePanel::UpdateAlphaModeState()
{
    const UPathTracerCaptureSettings* Settings = GetDefault<UPathTracerCaptureSettings>();
    bIsAlphaModeSelected = Settings != nullptr && Settings->AlphaMode != EPathTracerCaptureAlphaMode::None;
}

FText SPathTracerCapturePanel::GetPhaseText() const
{
    if (GEditor)
    {
        if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            return FText::Format(
                FText::FromString(TEXT("阶段：{0}")),
                UEnum::GetDisplayValueAsText(Subsystem->GetProgress().Phase));
        }
    }
    return FText::FromString(TEXT("阶段：不可用"));
}

FText SPathTracerCapturePanel::GetStatusText() const
{
    if (GEditor)
    {
        if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            return FText::FromString(Subsystem->GetProgress().StatusMessage);
        }
    }
    return FText::GetEmpty();
}

FText SPathTracerCapturePanel::GetLogText() const
{
    if (GEditor)
    {
        if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            return FText::FromString(Subsystem->GetStatusLogText());
        }
    }
    return FText::GetEmpty();
}
