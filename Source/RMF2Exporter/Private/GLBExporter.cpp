#include "GLBExporter.h"
#include "HAL/PlatformProcess.h"
#include "IPythonScriptPlugin.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FGLBExporter"

void FGLBExporter::RegisterMenus()
{
  UToolMenus::RegisterStartupCallback(
      FSimpleMulticastDelegate::FDelegate::CreateRaw(
          this,
          &FGLBExporter::RegisterMenuEntries
      )
  );
}

void FGLBExporter::UnregisterMenus()
{
  UToolMenus::UnRegisterStartupCallback(this);
  UToolMenus::UnregisterOwner(this);
}

void FGLBExporter::RegisterMenuEntries()
{
  FToolMenuOwnerScoped OwnerScoped(this);

  UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
  FToolMenuSection& Section = Menu->FindOrAddSection("GLBSceneExporter");

  Section.AddMenuEntry(
      "ExportSceneToGLB",
      LOCTEXT("ExportSceneToGLB", "Export Scene to GLB"),
      LOCTEXT(
          "ExportSceneToGLBTooltip",
          "Exports the current level to GLB using generated GLTF-safe materials."
      ),
      FSlateIcon(),
      FUIAction(FExecuteAction::CreateRaw(this, &FGLBExporter::RunExportScript))
  );

  Section.AddMenuEntry(
      "OpenGLBExportFolder",
      LOCTEXT("OpenGLBExportFolder", "Open GLB Export Folder"),
      LOCTEXT(
          "OpenGLBExportFolderTooltip",
          "Opens the GLB export output folder."
      ),
      FSlateIcon(),
      FUIAction(FExecuteAction::CreateRaw(this, &FGLBExporter::OpenExportFolder)
      )
  );
}

void FGLBExporter::RunExportScript()
{
  TSharedPtr<IPlugin> Plugin =
      IPluginManager::Get().FindPlugin(TEXT("RMF2ForUnreal"));
  if (!Plugin.IsValid())
  {
    UE_LOG(LogTemp, Error, TEXT("RMF2ForUnreal plugin not found."));
    return;
  }

  FString ScriptPath = FPaths::Combine(
      Plugin->GetBaseDir(),
      TEXT("Content/Python/RMF2Exporter/GLBExporter/GLBExporter.py")
  );

  if (!FPaths::FileExists(ScriptPath))
  {
    UE_LOG(LogTemp, Error, TEXT("Export script not found: %s"), *ScriptPath);
    return;
  }

  UE_LOG(
      LogTemp,
      Display,
      TEXT("Running RMF GLB export script: %s"),
      *ScriptPath
  );

  FString PythonCommand = FString::Printf(
      TEXT("exec(open(r'%s', encoding='utf-8').read())"),
      *ScriptPath
  );

  IPythonScriptPlugin::Get()->ExecPythonCommand(*PythonCommand);
}

void FGLBExporter::OpenExportFolder()
{
  FString ExportFolder =
      FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GLTFExports"));

  FPlatformProcess::ExploreFolder(*ExportFolder);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRMF2ExporterModule, RMF2Exporter)