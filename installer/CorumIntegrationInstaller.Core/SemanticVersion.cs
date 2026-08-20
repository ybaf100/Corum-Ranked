using System.Globalization;
using System.Text.RegularExpressions;

namespace CorumIntegrationInstaller.Core;

public sealed class SemanticVersion : IComparable<SemanticVersion>, IEquatable<SemanticVersion>
{
    private static readonly Regex VersionPattern = new(
        "^[vV]?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\\+([0-9A-Za-z.-]+))?$",
        RegexOptions.CultureInvariant | RegexOptions.Compiled);

    private SemanticVersion(
        int major,
        int minor,
        int patch,
        string? preRelease,
        string? buildMetadata)
    {
        Major = major;
        Minor = minor;
        Patch = patch;
        PreRelease = preRelease;
        BuildMetadata = buildMetadata;
    }

    public int Major { get; }

    public int Minor { get; }

    public int Patch { get; }

    public string? PreRelease { get; }

    public string? BuildMetadata { get; }

    public static SemanticVersion Parse(string value)
    {
        if (!TryParse(value, out var version))
        {
            throw new FormatException($"'{value}' is not a valid semantic version.");
        }

        return version;
    }

    public static bool TryParse(string? value, out SemanticVersion version)
    {
        version = null!;
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var match = VersionPattern.Match(value.Trim());
        if (!match.Success ||
            !int.TryParse(match.Groups[1].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var major) ||
            !int.TryParse(match.Groups[2].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var minor) ||
            !int.TryParse(match.Groups[3].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var patch))
        {
            return false;
        }

        var preRelease = match.Groups[4].Success ? match.Groups[4].Value : null;
        var buildMetadata = match.Groups[5].Success ? match.Groups[5].Value : null;
        if (!IsValidIdentifierList(preRelease, rejectLeadingZeroNumeric: true) ||
            !IsValidIdentifierList(buildMetadata, rejectLeadingZeroNumeric: false))
        {
            return false;
        }

        version = new SemanticVersion(
            major,
            minor,
            patch,
            preRelease,
            buildMetadata);
        return true;
    }

    public string ToNormalizedString(bool includeLeadingV = true)
    {
        var value = $"{Major}.{Minor}.{Patch}";
        if (!string.IsNullOrEmpty(PreRelease))
        {
            value += $"-{PreRelease}";
        }

        if (!string.IsNullOrEmpty(BuildMetadata))
        {
            value += $"+{BuildMetadata}";
        }

        return includeLeadingV ? $"v{value}" : value;
    }

    public int CompareTo(SemanticVersion? other)
    {
        if (other is null)
        {
            return 1;
        }

        var comparison = Major.CompareTo(other.Major);
        if (comparison != 0)
        {
            return comparison;
        }

        comparison = Minor.CompareTo(other.Minor);
        if (comparison != 0)
        {
            return comparison;
        }

        comparison = Patch.CompareTo(other.Patch);
        if (comparison != 0)
        {
            return comparison;
        }

        return ComparePreRelease(PreRelease, other.PreRelease);
    }

    public bool Equals(SemanticVersion? other) =>
        other is not null && CompareTo(other) == 0;

    public override bool Equals(object? obj) =>
        obj is SemanticVersion other && Equals(other);

    public override int GetHashCode() =>
        HashCode.Combine(Major, Minor, Patch, PreRelease);

    public override string ToString() => ToNormalizedString();

    public static bool operator <(SemanticVersion left, SemanticVersion right) => left.CompareTo(right) < 0;

    public static bool operator >(SemanticVersion left, SemanticVersion right) => left.CompareTo(right) > 0;

    public static bool operator <=(SemanticVersion left, SemanticVersion right) => left.CompareTo(right) <= 0;

    public static bool operator >=(SemanticVersion left, SemanticVersion right) => left.CompareTo(right) >= 0;

    private static bool IsValidIdentifierList(string? value, bool rejectLeadingZeroNumeric)
    {
        if (value is null)
        {
            return true;
        }

        foreach (var identifier in value.Split('.'))
        {
            if (identifier.Length == 0)
            {
                return false;
            }

            if (rejectLeadingZeroNumeric &&
                identifier.Length > 1 &&
                identifier[0] == '0' &&
                identifier.All(char.IsDigit))
            {
                return false;
            }
        }

        return true;
    }

    private static int ComparePreRelease(string? left, string? right)
    {
        if (left is null && right is null)
        {
            return 0;
        }

        if (left is null)
        {
            return 1;
        }

        if (right is null)
        {
            return -1;
        }

        var leftParts = left.Split('.');
        var rightParts = right.Split('.');
        for (var index = 0; index < Math.Min(leftParts.Length, rightParts.Length); index++)
        {
            var leftNumeric = int.TryParse(leftParts[index], NumberStyles.None, CultureInfo.InvariantCulture, out var leftNumber);
            var rightNumeric = int.TryParse(rightParts[index], NumberStyles.None, CultureInfo.InvariantCulture, out var rightNumber);

            int comparison;
            if (leftNumeric && rightNumeric)
            {
                comparison = leftNumber.CompareTo(rightNumber);
            }
            else if (leftNumeric)
            {
                comparison = -1;
            }
            else if (rightNumeric)
            {
                comparison = 1;
            }
            else
            {
                comparison = string.Compare(leftParts[index], rightParts[index], StringComparison.Ordinal);
            }

            if (comparison != 0)
            {
                return comparison;
            }
        }

        return leftParts.Length.CompareTo(rightParts.Length);
    }
}
