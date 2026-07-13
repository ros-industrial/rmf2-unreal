// RMF2Exporter.cpp
#include "RMF2Exporter.h"
#include "GLBExporter.h"

FRMF2ExporterModule::~FRMF2ExporterModule() = default;

void FRMF2ExporterModule::StartupModule()
{
  GLBExporter = MakeUnique<FGLBExporter>();
  GLBExporter->RegisterMenus();
}

void FRMF2ExporterModule::ShutdownModule()
{
  if (GLBExporter)
  {
    GLBExporter->UnregisterMenus();
    GLBExporter.Reset();
  }
}

IMPLEMENT_MODULE(FRMF2ExporterModule, RMF2Exporter)