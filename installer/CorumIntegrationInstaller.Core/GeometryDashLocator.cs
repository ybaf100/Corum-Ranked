using Microsoft.Win32;
using System.Runtime.Versioning;

namespace CorumIntegrationInstaller.Core;

public sealed class GeometryDashLocator
{
    public string? FindInstalledDirectory(string? preferredDirectory = null)
    {
        if (IsValidGeometryDashDirectory(preferredDirectory))
        {
            return Path.GetFullPath(preferredDirectory!);
        }

        foreach (var steamRoot in EnumerateSteamRoots())
        {
            var result = FindInSteamRoot(steamRoot);
            if (result is not null)
            {
                return result;
            }
        }

        return null;
    }

    public static bool IsValidGeometryDashDirectory(string? directory)
    {
        if (string.IsNullOrWhiteSpace(directory))
        {
            return false;
        }

        try
        {
            return File.Exists(Path.Combine(directory, AppConstants.GeometryDashExecutableName));
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return false;
        }
    }

    public static string? FindInSteamRoot(string steamRoot)
    {
        if (string.IsNullOrWhiteSpace(steamRoot))
        {
            return null;
        }

        var libraryRoots = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            steamRoot
        };

        var libraryFile = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
        try
        {
            if (File.Exists(libraryFile))
            {
                foreach (var libraryPath in SteamLibraryParser.ParseLibraryPaths(File.ReadAllText(libraryFile)))
                {
                    libraryRoots.Add(libraryPath);
                }
            }
        }
        catch (IOException)
        {
            // A locked or partially written Steam metadata file should not block other candidates.
        }
        catch (UnauthorizedAccessException)
        {
            // Continue with the default Steam library and any remaining roots.
        }

        foreach (var libraryRoot in libraryRoots)
        {
            var steamApps = Path.Combine(libraryRoot, "steamapps");
            var installDirectoryName = ReadInstallDirectoryName(steamApps) ?? "Geometry Dash";
            var candidate = Path.Combine(steamApps, "common", installDirectoryName);
            if (IsValidGeometryDashDirectory(candidate))
            {
                return Path.GetFullPath(candidate);
            }
        }

        return null;
    }

    private static IEnumerable<string> EnumerateSteamRoots()
    {
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        if (OperatingSystem.IsWindows())
        {
            AddRegistrySteamPath(roots, RegistryHive.CurrentUser, RegistryView.Default, @"Software\Valve\Steam", "SteamPath");
            AddRegistrySteamPath(roots, RegistryHive.LocalMachine, RegistryView.Registry64, @"SOFTWARE\Valve\Steam", "InstallPath");
            AddRegistrySteamPath(roots, RegistryHive.LocalMachine, RegistryView.Registry32, @"SOFTWARE\Valve\Steam", "InstallPath");
        }

        AddEnvironmentSteamPath(roots, Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86));
        AddEnvironmentSteamPath(roots, Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles));

        return roots;
    }

    [SupportedOSPlatform("windows")]
    private static void AddRegistrySteamPath(
        ISet<string> roots,
        RegistryHive hive,
        RegistryView view,
        string subKey,
        string valueName)
    {
        try
        {
            using var baseKey = RegistryKey.OpenBaseKey(hive, view);
            using var key = baseKey.OpenSubKey(subKey);
            if (key?.GetValue(valueName) is string value && !string.IsNullOrWhiteSpace(value))
            {
                roots.Add(Path.TrimEndingDirectorySeparator(value.Replace('/', Path.DirectorySeparatorChar)));
            }
        }
        catch (Exception exception) when (exception is UnauthorizedAccessException or IOException)
        {
            // Registry discovery is best-effort; standard install paths are checked afterward.
        }
    }

    private static void AddEnvironmentSteamPath(ISet<string> roots, string programFiles)
    {
        if (!string.IsNullOrWhiteSpace(programFiles))
        {
            roots.Add(Path.Combine(programFiles, "Steam"));
        }
    }

    private static string? ReadInstallDirectoryName(string steamAppsDirectory)
    {
        var manifestPath = Path.Combine(steamAppsDirectory, $"appmanifest_{AppConstants.SteamAppId}.acf");
        try
        {
            if (!File.Exists(manifestPath))
            {
                return null;
            }

            var value = SteamLibraryParser.FindValue(File.ReadAllText(manifestPath), "installdir");
            return string.IsNullOrWhiteSpace(value) ||
                   value.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                   value.Contains('/') ||
                   value.Contains('\\') ||
                   value is "." or ".." ||
                   Path.IsPathRooted(value)
                ? null
                : value;
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }
    }
}
