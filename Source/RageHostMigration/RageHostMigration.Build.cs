// Copyright (c) 2026 Abdallah Boutrif
// SPDX-License-Identifier: MIT

using System.IO;
using UnrealBuildTool;

public class RageHostMigration : ModuleRules
{
	private bool IsPluginPresent(string PluginName)
	{
		string FileName = PluginName + ".uplugin";

		foreach (string Root in new string[] { Path.GetDirectoryName(PluginDirectory), Path.Combine(EngineDirectory, "Plugins") })
		{
			if (!string.IsNullOrEmpty(Root) && 
			    Directory.Exists(Root) && 
			    Directory.GetFiles(Root, FileName, SearchOption.AllDirectories).Length > 0)
			{
				return true;
			}
		}

		return false;
	}

	public RageHostMigration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CoreOnline",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EngineSettings",
			"NetCore"
		});
		
		bool bHasCommonLoadingScreen = IsPluginPresent("CommonLoadingScreen");
		if (bHasCommonLoadingScreen)
		{
			PublicDependencyModuleNames.Add("CommonLoadingScreen");
		}
		
		/* Rage (Game) uses CommonLoadingScreen but since it's a plugin now we have to make it optional. */
		PublicDefinitions.Add("WITH_COMMON_LOADING_SCREEN=" + (bHasCommonLoadingScreen ? "1" : "0"));
	}
}
