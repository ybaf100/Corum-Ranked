namespace CorumIntegrationInstaller.Core;

public sealed record InstallationResult(string InstalledVersion, string InstalledPath, int ReplacedPackageCount);

public sealed class InstallationService
{
    private readonly GeodePackageInspector _packageInspector;
    private readonly IGeometryDashProcessService _processService;

    public InstallationService(
        GeodePackageInspector packageInspector,
        IGeometryDashProcessService processService)
    {
        _packageInspector = packageInspector;
        _processService = processService;
    }

    public InstallationResult InstallOrUpdate(
        string downloadedPackagePath,
        string geometryDashDirectory,
        string expectedVersion)
    {
        var downloadedMetadata = _packageInspector.ValidateCorumPackage(downloadedPackagePath, expectedVersion);

        if (!GeometryDashLocator.IsValidGeometryDashDirectory(geometryDashDirectory))
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidGeometryDashFolder,
                "The selected folder does not contain GeometryDash.exe.");
        }

        var modsDirectory = Path.Combine(geometryDashDirectory, "geode", "mods");
        if (!Directory.Exists(modsDirectory))
        {
            throw new InstallerException(
                InstallerErrorCode.GeodeNotInstalled,
                "Geode is not installed. Install Geode before installing Corum Integration.");
        }

        if (_processService.IsRunning())
        {
            throw new InstallerException(
                InstallerErrorCode.GeometryDashRunning,
                "Geometry Dash is currently running. Please close the game before installing or updating Corum Integration.");
        }

        var installedPackages = _packageInspector.FindInstalledCorumPackages(modsDirectory);
        var targetPath = Path.Combine(modsDirectory, AppConstants.ReleaseAssetName);
        EnsureTargetDoesNotBelongToAnotherMod(targetPath, installedPackages);

        var operationId = Guid.NewGuid().ToString("N");
        var stagingPath = Path.Combine(modsDirectory, $".corum-integration-{operationId}.installing");
        var backupDirectory = Path.Combine(
            Directory.GetParent(modsDirectory)!.FullName,
            $".corum-integration-backup-{operationId}");
        var movedPackages = new List<MovedPackage>();
        var newPackagePlaced = false;

        try
        {
            File.Copy(downloadedPackagePath, stagingPath, false);
            _packageInspector.ValidateCorumPackage(stagingPath, expectedVersion);
            if (_processService.IsRunning())
            {
                throw new InstallerException(
                    InstallerErrorCode.GeometryDashRunning,
                    "Geometry Dash started while the package was being prepared. Close the game and try again.");
            }

            Directory.CreateDirectory(backupDirectory);
            for (var index = 0; index < installedPackages.Count; index++)
            {
                var package = installedPackages[index];
                var backupPath = Path.Combine(backupDirectory, $"{index:D3}-{Path.GetFileName(package.FilePath)}");
                File.Move(package.FilePath, backupPath, false);
                movedPackages.Add(new MovedPackage(package.FilePath, backupPath));
            }

            File.Move(stagingPath, targetPath, false);
            newPackagePlaced = true;
            _packageInspector.ValidateCorumPackage(targetPath, expectedVersion);
        }
        catch (UnauthorizedAccessException exception)
        {
            var rollbackMessage = RollBack(targetPath, stagingPath, movedPackages, newPackagePlaced);
            if (string.IsNullOrEmpty(rollbackMessage))
            {
                TryDeleteDirectory(backupDirectory);
            }
            throw new InstallerException(
                InstallerErrorCode.AccessDenied,
                "Windows denied write access to the Geode mods folder. Close Geometry Dash and run the installer with permission to modify that folder." + rollbackMessage,
                exception);
        }
        catch (InstallerException exception)
        {
            var rollbackMessage = RollBack(targetPath, stagingPath, movedPackages, newPackagePlaced);
            if (string.IsNullOrEmpty(rollbackMessage))
            {
                TryDeleteDirectory(backupDirectory);
                throw;
            }

            throw new InstallerException(exception.Code, exception.Message + rollbackMessage, exception);
        }
        catch (IOException exception)
        {
            var rollbackMessage = RollBack(targetPath, stagingPath, movedPackages, newPackagePlaced);
            if (string.IsNullOrEmpty(rollbackMessage))
            {
                TryDeleteDirectory(backupDirectory);
            }
            throw new InstallerException(
                InstallerErrorCode.InstallFailed,
                "Corum Integration could not be installed or replaced. The previous installation was preserved whenever possible." + rollbackMessage,
                exception);
        }
        finally
        {
            TryDeleteFile(stagingPath);
        }

        TryDeleteDirectory(backupDirectory);
        return new InstallationResult(downloadedMetadata.Version, targetPath, installedPackages.Count);
    }

    private void EnsureTargetDoesNotBelongToAnotherMod(
        string targetPath,
        IReadOnlyList<GeodePackageMetadata> installedPackages)
    {
        if (!File.Exists(targetPath) ||
            installedPackages.Any(package => package.FilePath.Equals(targetPath, StringComparison.OrdinalIgnoreCase)))
        {
            return;
        }

        string description;
        try
        {
            var targetMetadata = _packageInspector.Inspect(targetPath);
            description = $"Mod ID '{targetMetadata.Id}'";
        }
        catch (InstallerException)
        {
            description = "an unreadable or invalid package";
        }

        throw new InstallerException(
            InstallerErrorCode.FileCollision,
            $"The destination file already belongs to {description}. It was not overwritten.");
    }

    private static string RollBack(
        string targetPath,
        string stagingPath,
        IReadOnlyList<MovedPackage> movedPackages,
        bool newPackagePlaced)
    {
        var failures = new List<string>();

        if (newPackagePlaced)
        {
            TryAction(
                () => File.Delete(targetPath),
                $"remove the incomplete new package at {targetPath}",
                failures);
        }

        for (var index = movedPackages.Count - 1; index >= 0; index--)
        {
            var movedPackage = movedPackages[index];
            TryAction(
                () => File.Move(movedPackage.BackupPath, movedPackage.OriginalPath, false),
                $"restore {movedPackage.OriginalPath}",
                failures);
        }

        TryDeleteFile(stagingPath);
        return failures.Count == 0
            ? string.Empty
            : $" Recovery requires attention: {string.Join("; ", failures)}.";
    }

    private static void TryAction(Action action, string description, ICollection<string> failures)
    {
        try
        {
            action();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            failures.Add(description);
        }
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Installation has already succeeded or rollback has already reported a more useful error.
        }
    }

    private static void TryDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
            {
                Directory.Delete(path, true);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Backups are stored in a hidden non-mod directory and can be cleaned up on a later run.
        }
    }

    private sealed record MovedPackage(string OriginalPath, string BackupPath);
}
