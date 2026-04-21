#include "SPathTracerCapturePanel.h"

#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDetailsView.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PathTracerCaptureEditorSubsystem.h"
#include "PathTracerCaptureSettings.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "UObject/Field.h"
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
                .Text(FText::FromString(TEXT("Render & Save")))
                .OnClicked(this, &SPathTracerCapturePanel::OnRenderClicked)
                .IsEnabled(this, &SPathTracerCapturePanel::IsRenderEnabled)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Open Output Folder")))
                .OnClicked(this, &SPathTracerCapturePanel::OnOpenOutputDirectoryClicked)
                .IsEnabled(this, &SPathTracerCapturePanel::IsOpenOutputDirectoryEnabled)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Reset")))
                .OnClicked(this, &SPathTracerCapturePanel::OnResetSettingsClicked)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Cancel")))
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
        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
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

bool SPathTracerCapturePanel::IsSettingsPropertyVisible(const FPropertyAndParent& PropertyAndParent) const
{
    return PropertyAndParent.Property.GetFName() != GET_MEMBER_NAME_CHECKED(UPathTracerCaptureSettings, Backend);
}

FText SPathTracerCapturePanel::GetPhaseText() const
{
    if (GEditor)
    {
        if (const UPathTracerCaptureEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UPathTracerCaptureEditorSubsystem>())
        {
            return FText::FromString(FString::Printf(TEXT("Phase: %s"), *UEnum::GetValueAsString(Subsystem->GetProgress().Phase)));
        }
    }
    return FText::FromString(TEXT("Phase: Unavailable"));
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
