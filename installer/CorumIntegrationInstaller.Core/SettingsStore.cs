using System.Text.Json;

namespace CorumIntegrationInstaller.Core;

public sealed record InstallerSettings(string? GeometryDashDirectory);

public sealed class SettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    private readonly string _settingsPath;

    public SettingsStore(string? settingsPath = null)
    {
        _settingsPath = settingsPath ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "CorumIntegrationInstaller",
            "settings.json");
    }

    public InstallerSettings Load()
    {
        try
        {
            if (!File.Exists(_settingsPath))
            {
                return new InstallerSettings(null);
            }

            return JsonSerializer.Deserialize<InstallerSettings>(File.ReadAllText(_settingsPath), JsonOptions)
                   ?? new InstallerSettings(null);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
            return new InstallerSettings(null);
        }
    }

    public void Save(InstallerSettings settings)
    {
        var parent = Path.GetDirectoryName(_settingsPath);
        if (!string.IsNullOrEmpty(parent))
        {
            Directory.CreateDirectory(parent);
        }

        var temporaryPath = $"{_settingsPath}.{Guid.NewGuid():N}.tmp";
        try
        {
            File.WriteAllText(temporaryPath, JsonSerializer.Serialize(settings, JsonOptions));
            File.Move(temporaryPath, _settingsPath, true);
        }
        finally
        {
            TryDelete(temporaryPath);
        }
    }

    private static void TryDelete(string path)
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
            // A stale temporary settings file does not affect installer correctness.
        }
    }
}
