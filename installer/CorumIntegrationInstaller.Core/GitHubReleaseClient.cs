using System.Buffers;
using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace CorumIntegrationInstaller.Core;

public sealed record ReleaseAsset(string Name, Uri DownloadUri, long Size);

public sealed record ReleaseInfo(string Version, string TagName, Uri HtmlUri, ReleaseAsset Asset);

public sealed record DownloadProgress(long BytesReceived, long? TotalBytes);

public sealed class TemporaryDownload : IDisposable
{
    public TemporaryDownload(string directoryPath, string filePath)
    {
        DirectoryPath = directoryPath;
        FilePath = filePath;
    }

    public string DirectoryPath { get; }

    public string FilePath { get; }

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(DirectoryPath))
            {
                Directory.Delete(DirectoryPath, true);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Windows may keep a recently scanned download locked briefly. Temp cleanup is best-effort.
        }
    }
}

public sealed class GitHubReleaseClient : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private readonly HttpClient _httpClient;
    private readonly bool _ownsClient;

    public GitHubReleaseClient(HttpClient? httpClient = null)
    {
        _ownsClient = httpClient is null;
        _httpClient = httpClient ?? new HttpClient(new HttpClientHandler
        {
            AutomaticDecompression = DecompressionMethods.All
        });
        _httpClient.Timeout = TimeSpan.FromMinutes(2);

        if (!_httpClient.DefaultRequestHeaders.UserAgent.Any())
        {
            _httpClient.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("CorumIntegrationInstaller", "1.0"));
        }

        _httpClient.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
        if (!_httpClient.DefaultRequestHeaders.Contains("X-GitHub-Api-Version"))
        {
            _httpClient.DefaultRequestHeaders.Add("X-GitHub-Api-Version", "2022-11-28");
        }
    }

    public async Task<ReleaseInfo> GetLatestReleaseAsync(CancellationToken cancellationToken = default)
    {
        HttpResponseMessage response;
        try
        {
            response = await _httpClient.GetAsync(AppConstants.LatestReleaseApiUri, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is HttpRequestException or TaskCanceledException)
        {
            throw new InstallerException(
                InstallerErrorCode.GitHubApiFailure,
                "GitHub API connection failed. Check your internet connection and try again.",
                exception);
        }

        using (response)
        {
            if (response.StatusCode == HttpStatusCode.NotFound)
            {
                throw new InstallerException(
                    InstallerErrorCode.ReleaseNotFound,
                    "No published Corum Integration GitHub Release was found.");
            }

            if (!response.IsSuccessStatusCode)
            {
                var rateLimited = response.StatusCode == HttpStatusCode.Forbidden &&
                                  response.Headers.TryGetValues("X-RateLimit-Remaining", out var values) &&
                                  values.Contains("0", StringComparer.Ordinal);
                throw new InstallerException(
                    InstallerErrorCode.GitHubApiFailure,
                    rateLimited
                        ? "The GitHub API rate limit was reached. Please try again later."
                        : $"GitHub API request failed ({(int)response.StatusCode} {response.ReasonPhrase}).");
            }

            GitHubReleaseDto release;
            try
            {
                await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
                release = await JsonSerializer.DeserializeAsync<GitHubReleaseDto>(stream, JsonOptions, cancellationToken).ConfigureAwait(false)
                          ?? throw new JsonException("The response was empty.");
            }
            catch (JsonException exception)
            {
                throw new InstallerException(
                    InstallerErrorCode.GitHubApiFailure,
                    "GitHub returned an unreadable Release response.",
                    exception);
            }

            if (!SemanticVersion.TryParse(release.TagName, out var releaseVersion))
            {
                throw new InstallerException(
                    InstallerErrorCode.ReleaseNotFound,
                    "The latest GitHub Release does not have a valid semantic version tag.");
            }

            var normalizedVersion = releaseVersion.ToNormalizedString();
            var expectedAssetName = AppConstants.ReleaseAssetName;
            var asset = release.Assets?.FirstOrDefault(candidate =>
                candidate.Name.Equals(expectedAssetName, StringComparison.OrdinalIgnoreCase));
            if (asset is null || !Uri.TryCreate(asset.BrowserDownloadUrl, UriKind.Absolute, out var downloadUri))
            {
                throw new InstallerException(
                    InstallerErrorCode.ReleaseAssetNotFound,
                    $"The latest Release does not contain {expectedAssetName}.");
            }

            var expectedPathPrefix = $"/{AppConstants.RepositoryOwner}/{AppConstants.RepositoryName}/releases/download/";
            if (downloadUri.Scheme != Uri.UriSchemeHttps ||
                !downloadUri.Host.Equals("github.com", StringComparison.OrdinalIgnoreCase) ||
                !downloadUri.AbsolutePath.StartsWith(expectedPathPrefix, StringComparison.OrdinalIgnoreCase))
            {
                throw new InstallerException(
                    InstallerErrorCode.ReleaseAssetNotFound,
                    "The Release package download URL is not an official HTTPS asset for this repository.");
            }

            if (!Uri.TryCreate(release.HtmlUrl, UriKind.Absolute, out var htmlUri))
            {
                htmlUri = new Uri($"https://github.com/{AppConstants.RepositoryOwner}/{AppConstants.RepositoryName}/releases/latest");
            }

            return new ReleaseInfo(
                normalizedVersion,
                release.TagName,
                htmlUri,
                new ReleaseAsset(asset.Name, downloadUri, asset.Size));
        }
    }

    public async Task<TemporaryDownload> DownloadPackageAsync(
        ReleaseInfo release,
        IProgress<DownloadProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        if (release.Asset.Size is <= 0 or > AppConstants.MaximumPackageBytes)
        {
            throw new InstallerException(
                InstallerErrorCode.DownloadFailed,
                "The Release package has an invalid download size.");
        }

        var temporaryDirectory = Path.Combine(Path.GetTempPath(), "CorumIntegrationInstaller", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporaryDirectory);
        var destinationPath = Path.Combine(temporaryDirectory, release.Asset.Name);

        try
        {
            using var response = await _httpClient.GetAsync(
                release.Asset.DownloadUri,
                HttpCompletionOption.ResponseHeadersRead,
                cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                throw new InstallerException(
                    InstallerErrorCode.DownloadFailed,
                    $"The .geode download failed ({(int)response.StatusCode} {response.ReasonPhrase}).");
            }

            var totalBytes = response.Content.Headers.ContentLength;
            if (totalBytes is > AppConstants.MaximumPackageBytes)
            {
                throw new InstallerException(
                    InstallerErrorCode.DownloadFailed,
                    "The downloaded package is larger than the allowed limit.");
            }

            await using var source = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
            await using var destination = new FileStream(
                destinationPath,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.None,
                81920,
                FileOptions.Asynchronous | FileOptions.SequentialScan);

            var buffer = ArrayPool<byte>.Shared.Rent(81920);
            try
            {
                long received = 0;
                while (true)
                {
                    var count = await source.ReadAsync(buffer.AsMemory(0, buffer.Length), cancellationToken).ConfigureAwait(false);
                    if (count == 0)
                    {
                        break;
                    }

                    received += count;
                    if (received > AppConstants.MaximumPackageBytes)
                    {
                        throw new InstallerException(
                            InstallerErrorCode.DownloadFailed,
                            "The downloaded package is larger than the allowed limit.");
                    }

                    await destination.WriteAsync(buffer.AsMemory(0, count), cancellationToken).ConfigureAwait(false);
                    progress?.Report(new DownloadProgress(received, totalBytes));
                }

                await destination.FlushAsync(cancellationToken).ConfigureAwait(false);
                if (received <= 0)
                {
                    throw new InstallerException(
                        InstallerErrorCode.DownloadFailed,
                        "The downloaded .geode file was empty.");
                }

                if (received != release.Asset.Size)
                {
                    throw new InstallerException(
                        InstallerErrorCode.DownloadFailed,
                        $"The downloaded package size ({received} bytes) does not match the GitHub Release asset ({release.Asset.Size} bytes).");
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }

            return new TemporaryDownload(temporaryDirectory, destinationPath);
        }
        catch (InstallerException)
        {
            TryDeleteDirectory(temporaryDirectory);
            throw;
        }
        catch (Exception exception) when (exception is HttpRequestException or IOException or UnauthorizedAccessException or TaskCanceledException)
        {
            TryDeleteDirectory(temporaryDirectory);
            throw new InstallerException(
                InstallerErrorCode.DownloadFailed,
                "The .geode package could not be downloaded.",
                exception);
        }
    }

    public void Dispose()
    {
        if (_ownsClient)
        {
            _httpClient.Dispose();
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
            // Best-effort cleanup after a failed download.
        }
    }

    private sealed class GitHubReleaseDto
    {
        [JsonPropertyName("tag_name")]
        public string TagName { get; init; } = string.Empty;

        [JsonPropertyName("html_url")]
        public string HtmlUrl { get; init; } = string.Empty;

        [JsonPropertyName("assets")]
        public List<GitHubAssetDto>? Assets { get; init; }
    }

    private sealed class GitHubAssetDto
    {
        [JsonPropertyName("name")]
        public string Name { get; init; } = string.Empty;

        [JsonPropertyName("browser_download_url")]
        public string BrowserDownloadUrl { get; init; } = string.Empty;

        [JsonPropertyName("size")]
        public long Size { get; init; }
    }
}
