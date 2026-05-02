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

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);

			// APL for Java integration
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", System.IO.Path.Combine(PluginPath, "AndroidCamera2Plugin_APL.xml"));

			// ZXing jar (expected at Source/ThirdParty/zxing-core-3.5.2.jar)
			// Jar is pulled in via APL <addJars>; no need to add as native lib here.
		}
	}
}