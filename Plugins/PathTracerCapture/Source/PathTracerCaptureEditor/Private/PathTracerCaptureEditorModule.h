#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FPathTracerCaptureEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void OpenPluginWindow();
    TSharedRef<class SDockTab> SpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

private:
    static const FName TabName;
};
