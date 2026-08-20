namespace CorumIntegrationInstaller.Core;

public enum InstallerErrorCode
{
    GeometryDashNotFound,
    InvalidGeometryDashFolder,
    GeodeNotInstalled,
    GitHubApiFailure,
    ReleaseNotFound,
    ReleaseAssetNotFound,
    DownloadFailed,
    InvalidGeodeArchive,
    ModJsonMissing,
    InvalidModJson,
    ModIdMismatch,
    ModVersionMismatch,
    GeometryDashRunning,
    FileCollision,
    AccessDenied,
    InstallFailed
}

public sealed class InstallerException : Exception
{
    public InstallerException(InstallerErrorCode code, string message)
        : base(message)
    {
        Code = code;
    }

    public InstallerException(InstallerErrorCode code, string message, Exception innerException)
        : base(message, innerException)
    {
        Code = code;
    }

    public InstallerErrorCode Code { get; }
}
