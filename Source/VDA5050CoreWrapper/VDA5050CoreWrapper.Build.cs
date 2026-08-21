using System.IO;
using UnrealBuildTool;

public class VDA5050CoreWrapper : ModuleRules
{
    public VDA5050CoreWrapper(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseRTTI = true;
        bEnableExceptions = true;

        string ThirdPartyPath = Path.GetFullPath(
            Path.Combine(ModuleDirectory, "../../ThirdParty/"));
        string IncludePath = Path.Combine(ThirdPartyPath, "include");
        string LibPath = Path.Combine(ThirdPartyPath, "lib");

        PrivateIncludePaths.Add(IncludePath);

        // vda5050_core libraries (built from ~/vda5050_core with the UE toolchain)
        // plus the external MQTT/formatting deps they link against.
        string[] SharedLibs =
        {
            "libvda5050_execution.so",
            "libvda5050_transport.so",
            "libvda5050_logger.so",
            "libvda5050_client.so",
            "libvda5050_validation.so",
            "libvda5050_layout.so",
            "libvda5050_master.so",
            "libpaho-mqttpp3.so",
            "libpaho-mqtt3a.so",
            "libfmt.so",
        };

        foreach (string Lib in SharedLibs)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, Lib));
            // Copy to output dir at runtime
            RuntimeDependencies.Add(
                Path.Combine("$(TargetOutputDir)", Lib),
                Path.Combine(LibPath, Lib),
                StagedFileType.NonUFS);
        }

        PublicRuntimeLibraryPaths.Add(LibPath);

        PublicSystemLibraries.Add("pthread");
        PublicSystemLibraries.Add("dl");

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );
    }
}
