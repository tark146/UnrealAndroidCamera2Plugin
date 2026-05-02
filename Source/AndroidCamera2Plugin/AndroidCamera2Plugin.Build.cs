using UnrealBuildTool;

public class AndroidCamera2Plugin : ModuleRules
{
	public AndroidCamera2Plugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",    // UObject、UTexture2D など
				"Engine",         // UE 基本機能
				"RenderCore",
				"InputCore",      // 入力機能
				"ApplicationCore" // FAndroidApplication::GetJavaEnv() を含む
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Keep it minimal for now
			}
		);

		// Android platform settings
		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// Basic Android support
			PublicDependencyModuleNames.Add("Launch");
			
			// Enable APL for Java integration
			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(PluginPath, "AndroidCamera2Plugin_APL.xml"));
		}
	}
}