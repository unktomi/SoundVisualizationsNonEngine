// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SoundVisualizationsNonEnginePrivatePCH.h"




class FSoundVisualizationsNonEnginePlugin : public ISoundVisualizationsNonEnginePlugin
{
public:
	// IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End of IModuleInterface implementation
};

IMPLEMENT_MODULE( FSoundVisualizationsNonEnginePlugin, SoundVisualizationsNonEngine )



void FSoundVisualizationsNonEnginePlugin::StartupModule()
{
	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)
}


void FSoundVisualizationsNonEnginePlugin::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}



