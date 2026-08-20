using System.Reflection;
using CorumIntegrationInstaller.Core;

namespace CorumIntegrationInstaller;

internal static class InstallerVersion
{
    public static string GetDisplayVersion()
    {
        var assembly = Assembly.GetEntryAssembly();
        var informational = assembly?
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?
            .InformationalVersion
            .Split('+', 2)[0];

        if (SemanticVersion.TryParse(informational, out var semanticVersion))
        {
            return semanticVersion.ToNormalizedString();
        }

        var version = assembly?.GetName().Version;
        return version is null
            ? "version unknown"
            : $"v{version.Major}.{version.Minor}.{Math.Max(version.Build, 0)}";
    }
}
