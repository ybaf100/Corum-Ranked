namespace CorumIntegrationInstaller.Core;

public static class AppConstants
{
    public const string ModId = "hwanhee1.corum_integration";
    public const string ModName = "Corum Integration";
    public const string RepositoryOwner = "ybaf100";
    public const string RepositoryName = "Corum-integration";
    public const string GeometryDashExecutableName = "GeometryDash.exe";
    public const string GeometryDashProcessName = "GeometryDash";
    public const string SteamAppId = "322170";
    public const string GeodeInstallUrl = "https://geode-sdk.org/install";
    public const string ReleaseAssetName = ModId + ".geode";
    public const long MaximumPackageBytes = 256L * 1024L * 1024L;

    public static Uri LatestReleaseApiUri { get; } =
        new($"https://api.github.com/repos/{RepositoryOwner}/{RepositoryName}/releases/latest");
}
