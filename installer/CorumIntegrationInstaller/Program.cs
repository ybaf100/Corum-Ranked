using CorumIntegrationInstaller.Core;

namespace CorumIntegrationInstaller;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();

        var packageInspector = new GeodePackageInspector();
        var processService = new GeometryDashProcessService();
        using var releaseClient = new GitHubReleaseClient();
        Application.Run(new MainForm(
            new GeometryDashLocator(),
            new SettingsStore(),
            packageInspector,
            releaseClient,
            new InstallationService(packageInspector, processService)));
    }
}
