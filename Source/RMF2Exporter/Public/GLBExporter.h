#pragma once

class FGLBExporter
{
public:
  void RegisterMenus();
  void UnregisterMenus();

private:
  void RegisterMenuEntries();
  void RunExportScript();
  void OpenExportFolder();
};
