using System.IO.Compression;
using System.Net;
using System.Text;
using CorumIntegrationInstaller.Core;

namespace CorumIntegrationInstaller.Tests;

internal static class Program
{
    private static async Task<int> Main()
    {
        var tests = new (string Name, Func<Task> Run)[]
        {
            ("semantic versions compare numerically", TestSemanticVersionsAsync),
            ("Steam VDF parser reads current and legacy libraries", TestSteamVdfParserAsync),
            ("Geometry Dash locator searches additional Steam libraries", TestAdditionalSteamLibraryAsync),
            ("package detection trusts mod.json ID instead of filename", TestPackageDetectionAsync),
            ("package validation distinguishes archive and manifest failures", TestPackageValidationErrorsAsync),
            ("GitHub Release client selects the canonical Mod ID asset", TestReleaseClientAsync),
            ("safe update replaces only matching Mod IDs", TestSafeUpdateAsync),
            ("invalid downloads preserve the installed package", TestInvalidDownloadPreservesInstalledAsync),
            ("running Geometry Dash blocks replacement", TestRunningGamePreservesInstalledAsync),
            ("destination collisions never overwrite another mod", TestTargetCollisionPreservesOtherModAsync)
        };

        var failures = 0;
        foreach (var test in tests)
        {
            try
            {
                await test.Run();
                Console.WriteLine($"PASS: {test.Name}");
            }
            catch (Exception exception)
            {
                failures++;
                Console.Error.WriteLine($"FAIL: {test.Name}");
                Console.Error.WriteLine(exception);
            }
        }

        Console.WriteLine($"{tests.Length - failures}/{tests.Length} installer tests passed.");
        return failures == 0 ? 0 : 1;
    }

    private static Task TestSemanticVersionsAsync()
    {
        Assert(SemanticVersion.Parse("v0.2.40") > SemanticVersion.Parse("v0.2.9"), "0.2.40 must be newer than 0.2.9.");
        Assert(SemanticVersion.Parse("v1.0.0") > SemanticVersion.Parse("1.0.0-rc.1"), "A release must be newer than its prerelease.");
        Assert(SemanticVersion.Parse("v1.0.0-rc.10") > SemanticVersion.Parse("v1.0.0-rc.2"), "Numeric prerelease parts must compare numerically.");
        Assert(SemanticVersion.Parse("v1.0.0+one").Equals(SemanticVersion.Parse("1.0.0+two")), "Build metadata must not affect precedence.");
        Assert(!SemanticVersion.TryParse("v1.0.0-alpha..1", out _), "Empty prerelease identifiers must be rejected.");
        return Task.CompletedTask;
    }

    private static Task TestSteamVdfParserAsync()
    {
        const string current = """
            "libraryfolders"
            {
                "0"
                {
                    "path" "C:\\Program Files (x86)\\Steam"
                }
                "1"
                {
                    "path" "D:\\SteamLibrary"
                }
            }
            """;
        var currentPaths = SteamLibraryParser.ParseLibraryPaths(current);
        Assert(currentPaths.Contains(@"C:\Program Files (x86)\Steam", StringComparer.OrdinalIgnoreCase), "The default current-format library was not parsed.");
        Assert(currentPaths.Contains(@"D:\SteamLibrary", StringComparer.OrdinalIgnoreCase), "The additional current-format library was not parsed.");

        const string legacy = """
            "LibraryFolders"
            {
                "1" "E:\\Games\\Steam"
            }
            """;
        var legacyPaths = SteamLibraryParser.ParseLibraryPaths(legacy);
        Assert(legacyPaths.Contains(@"E:\Games\Steam", StringComparer.OrdinalIgnoreCase), "The legacy library path was not parsed.");
        return Task.CompletedTask;
    }

    private static Task TestAdditionalSteamLibraryAsync()
    {
        using var fixture = new TemporaryFixture();
        var steamRoot = fixture.CreateDirectory("Steam");
        var additionalLibrary = fixture.CreateDirectory("AdditionalLibrary");
        var steamApps = Directory.CreateDirectory(Path.Combine(steamRoot, "steamapps")).FullName;
        File.WriteAllText(
            Path.Combine(steamApps, "libraryfolders.vdf"),
            $"\"libraryfolders\" {{ \"1\" {{ \"path\" \"{additionalLibrary.Replace("\\", "\\\\", StringComparison.Ordinal)}\" }} }}");

        var additionalSteamApps = Directory.CreateDirectory(Path.Combine(additionalLibrary, "steamapps")).FullName;
        File.WriteAllText(
            Path.Combine(additionalSteamApps, $"appmanifest_{AppConstants.SteamAppId}.acf"),
            "\"AppState\" { \"installdir\" \"Custom Geometry Dash\" }");
        var geometryDashDirectory = Directory.CreateDirectory(
            Path.Combine(additionalSteamApps, "common", "Custom Geometry Dash")).FullName;
        File.WriteAllBytes(Path.Combine(geometryDashDirectory, AppConstants.GeometryDashExecutableName), [0x4d, 0x5a]);

        var located = GeometryDashLocator.FindInSteamRoot(steamRoot);
        Assert(
            string.Equals(Path.GetFullPath(geometryDashDirectory), located, StringComparison.OrdinalIgnoreCase),
            "Geometry Dash was not found in the additional Steam library.");
        return Task.CompletedTask;
    }

    private static Task TestPackageDetectionAsync()
    {
        using var fixture = new TemporaryFixture();
        var modsDirectory = fixture.CreateDirectory("mods");
        CreateGeodePackage(Path.Combine(modsDirectory, "Corum-Integration-v0.2.40.geode"), AppConstants.ModId, "v0.2.40");
        CreateGeodePackage(Path.Combine(modsDirectory, "CorumIntegration.geode"), AppConstants.ModId, "v1.0.0");
        CreateGeodePackage(Path.Combine(modsDirectory, "Corum-Integration-v1.0.0.geode"), "someone.else", "v9.9.9");

        var packages = new GeodePackageInspector().FindInstalledCorumPackages(modsDirectory);
        Assert(packages.Count == 2, "Exactly the two packages with the Corum Mod ID must be detected.");
        Assert(packages.All(package => package.Id == AppConstants.ModId), "A filename match was incorrectly treated as a Mod ID match.");
        return Task.CompletedTask;
    }

    private static Task TestPackageValidationErrorsAsync()
    {
        using var fixture = new TemporaryFixture();
        var inspector = new GeodePackageInspector();

        var invalidArchive = Path.Combine(fixture.RootDirectory, "invalid.geode");
        File.WriteAllText(invalidArchive, "not a zip archive");
        ExpectInstallerError(InstallerErrorCode.InvalidGeodeArchive, () => inspector.Inspect(invalidArchive));

        var missingManifest = Path.Combine(fixture.RootDirectory, "missing-mod-json.geode");
        using (var archive = ZipFile.Open(missingManifest, ZipArchiveMode.Create))
        {
            archive.CreateEntry("readme.txt");
        }

        ExpectInstallerError(InstallerErrorCode.ModJsonMissing, () => inspector.Inspect(missingManifest));

        var wrongVersion = Path.Combine(fixture.RootDirectory, "wrong-version.geode");
        CreateGeodePackage(wrongVersion, AppConstants.ModId, "v1.0.0");
        ExpectInstallerError(
            InstallerErrorCode.ModVersionMismatch,
            () => inspector.ValidateCorumPackage(wrongVersion, "v1.0.2"));
        return Task.CompletedTask;
    }

    private static async Task TestReleaseClientAsync()
    {
        using var fixture = new TemporaryFixture();
        var packagePath = Path.Combine(fixture.RootDirectory, "release.geode");
        CreateGeodePackage(packagePath, AppConstants.ModId, "v1.0.2");
        var packageBytes = File.ReadAllBytes(packagePath);

        var json = """
            {
              "tag_name": "v1.0.2",
              "html_url": "https://github.com/ybaf100/Corum-integration/releases/tag/v1.0.2",
              "assets": [
                {
                  "name": "Corum-Integration-Installer-v1.0.2.exe",
                  "browser_download_url": "https://github.com/ybaf100/Corum-integration/releases/download/v1.0.2/installer.exe",
                  "size": 100
                },
                {
                  "name": "hwanhee1.corum_integration.geode",
                  "browser_download_url": "https://github.com/ybaf100/Corum-integration/releases/download/v1.0.2/hwanhee1.corum_integration.geode",
                  "size": 200
                }
              ]
            }
            """.Replace("\"size\": 200", $"\"size\": {packageBytes.Length}", StringComparison.Ordinal);
        using var httpClient = new HttpClient(new StubHttpMessageHandler(json, packageBytes));
        using var releaseClient = new GitHubReleaseClient(httpClient);

        var release = await releaseClient.GetLatestReleaseAsync();
        Assert(release.Version == "v1.0.2", "The Release version was not normalized.");
        Assert(release.Asset.Name == AppConstants.ReleaseAssetName, "The canonical Mod ID .geode asset was not selected.");
        using var download = await releaseClient.DownloadPackageAsync(release);
        var downloadedMetadata = new GeodePackageInspector().ValidateCorumPackage(download.FilePath, release.Version);
        Assert(downloadedMetadata.Id == AppConstants.ModId, "The Release package download was not preserved as a valid Geode archive.");
    }

    private static Task TestSafeUpdateAsync()
    {
        using var fixture = new InstallerFixture();
        var oldPath = Path.Combine(fixture.ModsDirectory, "old-custom-name.geode");
        var otherPath = Path.Combine(fixture.ModsDirectory, "another-mod.geode");
        CreateGeodePackage(oldPath, AppConstants.ModId, "v0.2.40");
        CreateGeodePackage(otherPath, "example.other_mod", "v3.0.0");
        var otherBytes = File.ReadAllBytes(otherPath);
        var downloadPath = fixture.CreateDownload(AppConstants.ModId, "v1.0.2");

        var result = fixture.CreateInstallationService(gameRunning: false)
            .InstallOrUpdate(downloadPath, fixture.GeometryDashDirectory, "v1.0.2");

        Assert(result.InstalledVersion == "v1.0.2", "The new version was not installed.");
        Assert(!File.Exists(oldPath), "The old same-ID package was not removed.");
        Assert(File.Exists(Path.Combine(fixture.ModsDirectory, AppConstants.ReleaseAssetName)), "The canonical package was not created.");
        Assert(File.ReadAllBytes(otherPath).SequenceEqual(otherBytes), "Another Geode mod was changed.");
        Assert(
            !Directory.EnumerateDirectories(Path.GetDirectoryName(fixture.ModsDirectory)!, ".corum-integration-backup-*").Any(),
            "A successful update left a backup directory behind.");
        return Task.CompletedTask;
    }

    private static Task TestInvalidDownloadPreservesInstalledAsync()
    {
        using var fixture = new InstallerFixture();
        var oldPath = Path.Combine(fixture.ModsDirectory, "CorumIntegration.geode");
        CreateGeodePackage(oldPath, AppConstants.ModId, "v1.0.0");
        var oldBytes = File.ReadAllBytes(oldPath);
        var downloadPath = fixture.CreateDownload("wrong.mod_id", "v1.0.2");

        ExpectInstallerError(
            InstallerErrorCode.ModIdMismatch,
            () => fixture.CreateInstallationService(gameRunning: false)
                .InstallOrUpdate(downloadPath, fixture.GeometryDashDirectory, "v1.0.2"));
        Assert(File.ReadAllBytes(oldPath).SequenceEqual(oldBytes), "The existing package changed before download validation completed.");
        return Task.CompletedTask;
    }

    private static Task TestRunningGamePreservesInstalledAsync()
    {
        using var fixture = new InstallerFixture();
        var oldPath = Path.Combine(fixture.ModsDirectory, "CorumIntegration.geode");
        CreateGeodePackage(oldPath, AppConstants.ModId, "v1.0.0");
        var oldBytes = File.ReadAllBytes(oldPath);
        var downloadPath = fixture.CreateDownload(AppConstants.ModId, "v1.0.2");

        ExpectInstallerError(
            InstallerErrorCode.GeometryDashRunning,
            () => fixture.CreateInstallationService(gameRunning: true)
                .InstallOrUpdate(downloadPath, fixture.GeometryDashDirectory, "v1.0.2"));
        Assert(File.ReadAllBytes(oldPath).SequenceEqual(oldBytes), "The installed package changed while Geometry Dash was running.");
        return Task.CompletedTask;
    }

    private static Task TestTargetCollisionPreservesOtherModAsync()
    {
        using var fixture = new InstallerFixture();
        var collisionPath = Path.Combine(fixture.ModsDirectory, AppConstants.ReleaseAssetName);
        CreateGeodePackage(collisionPath, "example.unrelated", "v4.0.0");
        var collisionBytes = File.ReadAllBytes(collisionPath);
        var downloadPath = fixture.CreateDownload(AppConstants.ModId, "v1.0.2");

        ExpectInstallerError(
            InstallerErrorCode.FileCollision,
            () => fixture.CreateInstallationService(gameRunning: false)
                .InstallOrUpdate(downloadPath, fixture.GeometryDashDirectory, "v1.0.2"));
        Assert(File.ReadAllBytes(collisionPath).SequenceEqual(collisionBytes), "A different mod at the destination path was overwritten.");
        return Task.CompletedTask;
    }

    private static void CreateGeodePackage(string path, string id, string version)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
        var entry = archive.CreateEntry("mod.json", CompressionLevel.NoCompression);
        using var writer = new StreamWriter(entry.Open(), new UTF8Encoding(false));
        writer.Write($"{{\"id\":\"{id}\",\"name\":\"Test\",\"version\":\"{version}\"}}");
    }

    private static void ExpectInstallerError(InstallerErrorCode expectedCode, Action action)
    {
        try
        {
            action();
        }
        catch (InstallerException exception) when (exception.Code == expectedCode)
        {
            return;
        }

        throw new InvalidOperationException($"Expected installer error {expectedCode}.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private sealed class StubHttpMessageHandler : HttpMessageHandler
    {
        private readonly string _json;
        private readonly byte[]? _assetBytes;

        public StubHttpMessageHandler(string json, byte[]? assetBytes = null)
        {
            _json = json;
            _assetBytes = assetBytes;
        }

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            HttpContent content = request.RequestUri == AppConstants.LatestReleaseApiUri
                ? new StringContent(_json, Encoding.UTF8, "application/json")
                : new ByteArrayContent(_assetBytes ?? Array.Empty<byte>());
            var response = new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = content,
                RequestMessage = request
            };
            return Task.FromResult(response);
        }
    }

    private sealed class FakeProcessService : IGeometryDashProcessService
    {
        private readonly bool _running;

        public FakeProcessService(bool running)
        {
            _running = running;
        }

        public bool IsRunning() => _running;
    }

    private class TemporaryFixture : IDisposable
    {
        public TemporaryFixture()
        {
            RootDirectory = Path.Combine(Path.GetTempPath(), "CorumInstallerTests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(RootDirectory);
        }

        public string RootDirectory { get; }

        public string CreateDirectory(string relativePath) =>
            Directory.CreateDirectory(Path.Combine(RootDirectory, relativePath)).FullName;

        public void Dispose()
        {
            try
            {
                if (Directory.Exists(RootDirectory))
                {
                    Directory.Delete(RootDirectory, true);
                }
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                // Test temp cleanup does not change the assertion result.
            }
        }
    }

    private sealed class InstallerFixture : TemporaryFixture
    {
        public InstallerFixture()
        {
            GeometryDashDirectory = CreateDirectory("Geometry Dash");
            File.WriteAllBytes(Path.Combine(GeometryDashDirectory, AppConstants.GeometryDashExecutableName), [0x4d, 0x5a]);
            ModsDirectory = CreateDirectory(Path.Combine("Geometry Dash", "geode", "mods"));
        }

        public string GeometryDashDirectory { get; }

        public string ModsDirectory { get; }

        public string CreateDownload(string id, string version)
        {
            var path = Path.Combine(RootDirectory, $"download-{Guid.NewGuid():N}.geode");
            CreateGeodePackage(path, id, version);
            return path;
        }

        public InstallationService CreateInstallationService(bool gameRunning)
        {
            var inspector = new GeodePackageInspector();
            return new InstallationService(inspector, new FakeProcessService(gameRunning));
        }
    }
}
