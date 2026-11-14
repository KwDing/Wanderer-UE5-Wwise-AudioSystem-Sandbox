using UnrealBuildTool;

public class WandererEditor : ModuleRules
{
	public WandererEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Editor-only asset synchronization, JSON metadata, and Wwise asset bindings.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "Wanderer", "InteriorFitEditor",
			"InteriorFitRuntime", "UnrealEd", "Slate", "SlateCore", "UMG", "ToolMenus",
			"PythonScriptPlugin"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"PhysicsCore", "PCG", "Landscape", "Niagara", "BSPUtils", "BlueprintGraph", "AnimGraph", "AnimGraphRuntime", "Foliage", "MeshDescription", "StaticMeshDescription", "RenderCore", "RHI", "AkAudio", "WwiseSoundEngine", "WwiseUtils", "WwiseResourceLoader", "WwiseProjectDatabase", "WwiseReconcile", "AudiokineticTools", "AssetTools", "AssetRegistry", "ContentBrowser", "PropertyEditor", "Json", "XmlParser"
		});
	}
}
