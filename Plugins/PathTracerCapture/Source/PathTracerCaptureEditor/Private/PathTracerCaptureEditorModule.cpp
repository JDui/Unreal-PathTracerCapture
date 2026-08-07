#include "PathTracerCaptureEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "SPathTracerCapturePanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

const FName FPathTracerCaptureEditorModule::TabName(TEXT("PathTracerCapture"));

void FPathTracerCaptureEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabName,
        FOnSpawnTab::CreateRaw(this, &FPathTracerCaptureEditorModule::SpawnPluginTab))
        .SetDisplayName(FText::FromString(TEXT("AXi_PathTracerCapture")))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPathTracerCaptureEditorModule::RegisterMenus));
}

void FPathTracerCaptureEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

void FPathTracerCaptureEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
    Section.AddMenuEntry(
        "PathTracerCapture_OpenTab",
        FText::FromString(TEXT("AXi_PathTracerCapture")),
        FText::FromString(TEXT("打开 AXi_PathTracerCapture 面板。")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FPathTracerCaptureEditorModule::OpenPluginWindow)));
}

void FPathTracerCaptureEditorModule::OpenPluginWindow()
{
    FGlobalTabmanager::Get()->TryInvokeTab(TabName);
}

TSharedRef<SDockTab> FPathTracerCaptureEditorModule::SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SPathTracerCapturePanel)
        ];
}

IMPLEMENT_MODULE(FPathTracerCaptureEditorModule, PathTracerCaptureEditor)
