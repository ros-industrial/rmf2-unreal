#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FRMF2ExporterModule : public IModuleInterface
{
public:
  virtual ~FRMF2ExporterModule() override;
  virtual void StartupModule() override;
  virtual void ShutdownModule() override;

private:
  TUniquePtr<class FGLBExporter> GLBExporter;
};
