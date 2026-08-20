using System.IO.Compression;
using System.Text.Json;

namespace CorumIntegrationInstaller.Core;

public sealed record GeodePackageMetadata(string FilePath, string Id, string Version);

public sealed class GeodePackageInspector
{
    private const long MaximumModJsonBytes = 1024L * 1024L;

    public GeodePackageMetadata Inspect(string packagePath)
    {
        if (!File.Exists(packagePath))
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidGeodeArchive,
                "The downloaded .geode file does not exist.");
        }

        try
        {
            using var archive = ZipFile.OpenRead(packagePath);
            var modJsonEntry = archive.Entries.FirstOrDefault(entry =>
                entry.FullName.Replace('\\', '/').Equals("mod.json", StringComparison.OrdinalIgnoreCase));
            if (modJsonEntry is null)
            {
                throw new InstallerException(
                    InstallerErrorCode.ModJsonMissing,
                    "The .geode package does not contain mod.json.");
            }

            if (modJsonEntry.Length <= 0 || modJsonEntry.Length > MaximumModJsonBytes)
            {
                throw new InstallerException(
                    InstallerErrorCode.InvalidModJson,
                    "The package contains an invalid mod.json file.");
            }

            using var stream = modJsonEntry.Open();
            using var document = JsonDocument.Parse(stream, new JsonDocumentOptions
            {
                AllowTrailingCommas = true,
                CommentHandling = JsonCommentHandling.Skip
            });

            var root = document.RootElement;
            var id = ReadRequiredString(root, "id");
            var version = ReadRequiredString(root, "version");
            return new GeodePackageMetadata(Path.GetFullPath(packagePath), id, version);
        }
        catch (InstallerException)
        {
            throw;
        }
        catch (Exception exception) when (exception is InvalidDataException or NotSupportedException)
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidGeodeArchive,
                "The downloaded file is not a valid ZIP/Geode package.",
                exception);
        }
        catch (JsonException exception)
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidModJson,
                "The package contains malformed mod.json data.",
                exception);
        }
        catch (IOException exception)
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidGeodeArchive,
                "The .geode package could not be read.",
                exception);
        }
        catch (UnauthorizedAccessException exception)
        {
            throw new InstallerException(
                InstallerErrorCode.AccessDenied,
                "The .geode package could not be read because access was denied.",
                exception);
        }
    }

    public GeodePackageMetadata ValidateCorumPackage(string packagePath, string? expectedVersion = null)
    {
        var metadata = Inspect(packagePath);
        if (!metadata.Id.Equals(AppConstants.ModId, StringComparison.Ordinal))
        {
            throw new InstallerException(
                InstallerErrorCode.ModIdMismatch,
                $"The downloaded package has Mod ID '{metadata.Id}', not '{AppConstants.ModId}'. Installation was stopped.");
        }

        if (!SemanticVersion.TryParse(metadata.Version, out var packageVersion))
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidModJson,
                "The downloaded package contains an invalid version in mod.json.");
        }

        if (expectedVersion is not null)
        {
            if (!SemanticVersion.TryParse(expectedVersion, out var releaseVersion) || !packageVersion.Equals(releaseVersion))
            {
                throw new InstallerException(
                    InstallerErrorCode.ModVersionMismatch,
                    $"The package version ({metadata.Version}) does not match the GitHub Release version ({expectedVersion}).");
            }
        }

        return metadata with { Version = packageVersion.ToNormalizedString() };
    }

    public IReadOnlyList<GeodePackageMetadata> FindInstalledCorumPackages(string modsDirectory)
    {
        if (!Directory.Exists(modsDirectory))
        {
            return Array.Empty<GeodePackageMetadata>();
        }

        var matches = new List<GeodePackageMetadata>();
        IEnumerable<string> files;
        try
        {
            files = Directory.EnumerateFiles(modsDirectory, "*.geode", SearchOption.TopDirectoryOnly).ToArray();
        }
        catch (UnauthorizedAccessException exception)
        {
            throw new InstallerException(
                InstallerErrorCode.AccessDenied,
                "The Geode mods folder could not be read because access was denied.",
                exception);
        }
        catch (IOException exception)
        {
            throw new InstallerException(
                InstallerErrorCode.InstallFailed,
                "The Geode mods folder could not be read.",
                exception);
        }

        foreach (var file in files)
        {
            try
            {
                var metadata = Inspect(file);
                if (metadata.Id.Equals(AppConstants.ModId, StringComparison.Ordinal))
                {
                    matches.Add(metadata);
                }
            }
            catch (InstallerException exception) when (
                exception.Code is InstallerErrorCode.InvalidGeodeArchive or
                    InstallerErrorCode.ModJsonMissing or
                    InstallerErrorCode.InvalidModJson or
                    InstallerErrorCode.AccessDenied)
            {
                // An unrelated broken package must not prevent Corum Integration detection.
            }
        }

        return matches
            .OrderBy(package => package.FilePath, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static string ReadRequiredString(JsonElement root, string propertyName)
    {
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty(propertyName, out var property) ||
            property.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(property.GetString()))
        {
            throw new InstallerException(
                InstallerErrorCode.InvalidModJson,
                $"The package mod.json is missing a valid '{propertyName}' value.");
        }

        return property.GetString()!;
    }
}
