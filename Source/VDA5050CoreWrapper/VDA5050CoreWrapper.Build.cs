using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class VDA5050CoreWrapper : ModuleRules
{
    public VDA5050CoreWrapper(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseRTTI = true;
        bEnableExceptions = true;

        // The vendored vda5050_core json serialization (serialization.hpp) uses
        // `vec = j.at("...")` assignments that are ambiguous under C++20's
        // overload resolution (nlohmann's implicit json->value conversion
        // collides with std::vector's initializer_list operator=). It compiles
        // cleanly under C++17, so pin this module to C++17.
        CppStandard = CppStandardVersion.Cpp17;

        PrivateIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "../../ThirdParty/include")
            }
        );

        // Libraries to search for (platform-agnostic base names)
        string[] libNames = new string[]
        {
            "vda5050_execution",
            "vda5050_transport",
            "vda5050_logger",
            "vda5050_client",
            "paho-mqttpp3",
            "paho-mqtt3as",
            "fmt",
        };

        // Create platform specific search pattern
        string platform;
        string libSearchPattern;
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            platform = "Windows-AMD64-";
            libSearchPattern = "*.dll";
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            platform = "Darwin-universal-";
            libSearchPattern = "lib*.so";

            PublicFrameworks.Add("SystemConfiguration");
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            platform = "Android-aarch64-";
            libSearchPattern = "lib*.so";
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            platform = "Linux-x86_64-";
            libSearchPattern = "lib*.so";
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            platform = "iOS-ARM64-";
            libSearchPattern = "lib*.so";
        }
        else
        {
            throw new InvalidOperationException("VDA5050CoreWrapper does not support this platform.");
        }

        string libPathBase = Path.Combine(ModuleDirectory, "../../ThirdParty/lib/" + platform);

        // Add Debug / Release to the search pattern
        string libPathDebug = libPathBase + "Debug";
        string libPathRelease = libPathBase + "Release";

        bool useDebug = false;
        if (Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            if (Directory.Exists(libPathDebug))
            {
                useDebug = true;
            }
        }

        string libPath = useDebug ? libPathDebug : libPathRelease;

        if (!Directory.Exists(libPath))
        {
            throw new InvalidOperationException(
                string.Format("VDA5050CoreWrapper: library directory not found: {0}", libPath));
        }

        // add public additional libraries
        List<string> allLibs = new List<string>();
        List<string> missingLibs = new List<string>();
        foreach (string libName in libNames)
        {
            // Resolve the platform-specific filename, e.g. "lib*.so" -> "libfmt.so", "*.dll" -> "fmt.dll"
            string pattern = libSearchPattern.Replace("*", libName);
            string[] matches = Directory.GetFiles(libPath, pattern);
            if (matches.Length == 0)
            {
                missingLibs.Add(pattern);
                continue;
            }
            allLibs.AddRange(matches);
        }
        if (missingLibs.Count > 0)
        {
            throw new InvalidOperationException(
                string.Format("VDA5050CoreWrapper: required libraries not found in {0}: {1}",
                    libPath, string.Join(", ", missingLibs)));
        }
        PublicAdditionalLibraries.AddRange(allLibs);

        // Copy to output dir at runtime
        foreach (string libFullPath in allLibs)
        {
            RuntimeDependencies.Add(
                Path.Combine("$(TargetOutputDir)", Path.GetFileName(libFullPath)),
                libFullPath,
                StagedFileType.NonUFS);
        }

        PublicRuntimeLibraryPaths.Add(libPath);

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
