// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    using System.IO;
    public class SoundVisualizationsNonEngine : ModuleRules
    {
        private string EnginePath
        {
            get { return Path.GetFullPath(BuildConfiguration.RelativeEnginePath); }
        }
        private string ModulePath
        {
            get { return Path.GetDirectoryName(RulesCompiler.GetModuleFilename(this.GetType().Name)); }
        }
        private string ModuleThirdPartyPath
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

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                if (Target.Configuration == UnrealTargetConfiguration.Debug && BuildConfiguration.bDebugBuildsActuallyUseDebugCRT)
                {
                    PublicLibraryPaths.Add(Path.Combine(ModuleThirdPartyPath, "./Windows/x64/Debug"));
                }
                else
                {
                    // Use the engine library
                    PublicLibraryPaths.Add(Path.Combine(EnginePath, "./Source/ThirdParty/Kiss_FFT/kiss_fft129/Lib/x64/VS2015/Release/"));
                }

                PublicAdditionalLibraries.Add("KissFFT.lib");
                PrivateIncludePaths.Add(Path.Combine(ModuleThirdPartyPath, "Include"));
            }
        }
    }
}
