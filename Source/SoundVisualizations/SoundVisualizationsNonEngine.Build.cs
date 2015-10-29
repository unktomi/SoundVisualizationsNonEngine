// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    using System.IO;
    public class SoundVisualizationsNonEngine : ModuleRules
    {
        private string ModulePath
        {
            get { return Path.GetDirectoryName(RulesCompiler.GetModuleFilename(this.GetType().Name)); }
        }
        private string ThirdPartyPath
        {
            get { return Path.GetFullPath(Path.Combine(ModulePath, "../../Thirdparty/")); }
        }
        public SoundVisualizationsNonEngine(TargetInfo Target)
        {
            PrivateIncludePaths.Add("SoundVisualizations/Private");

            PublicDependencyModuleNames.AddRange(
                new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "Media",
                    "MediaAssets",
				}
                );

		string Kiss_FFTPath = ThirdPartyPath;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug && BuildConfiguration.bDebugBuildsActuallyUseDebugCRT)
			{
				PublicLibraryPaths.Add(Kiss_FFTPath + "/Windows/x64/Debug");
			}
			else
			{
				PublicLibraryPaths.Add(Kiss_FFTPath + "/Windows/x64/Release");
			}

			PublicAdditionalLibraries.Add("KissFFT.lib");
                        PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "Include"));

		}
        }
    }
}
