using System.Text;

namespace CorumIntegrationInstaller.Core;

public static class SteamLibraryParser
{
    public static IReadOnlyList<string> ParseLibraryPaths(string content)
    {
        var tokens = Tokenize(content);
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        for (var index = 0; index + 1 < tokens.Count; index++)
        {
            if (tokens[index].Kind != VdfTokenKind.Value || tokens[index + 1].Kind != VdfTokenKind.Value)
            {
                continue;
            }

            var key = tokens[index].Value;
            var value = tokens[index + 1].Value;
            if (key.Equals("path", StringComparison.OrdinalIgnoreCase) ||
                (key.All(char.IsDigit) && LooksLikeAbsolutePath(value)))
            {
                AddPath(paths, value);
            }
        }

        return paths.ToArray();
    }

    public static string? FindValue(string content, string key)
    {
        var tokens = Tokenize(content);
        for (var index = 0; index + 1 < tokens.Count; index++)
        {
            if (tokens[index].Kind == VdfTokenKind.Value &&
                tokens[index + 1].Kind == VdfTokenKind.Value &&
                tokens[index].Value.Equals(key, StringComparison.OrdinalIgnoreCase))
            {
                return tokens[index + 1].Value;
            }
        }

        return null;
    }

    private static IReadOnlyList<VdfToken> Tokenize(string content)
    {
        var tokens = new List<VdfToken>();
        var index = 0;

        while (index < content.Length)
        {
            if (char.IsWhiteSpace(content[index]))
            {
                index++;
                continue;
            }

            if (content[index] == '/' && index + 1 < content.Length && content[index + 1] == '/')
            {
                index += 2;
                while (index < content.Length && content[index] is not '\r' and not '\n')
                {
                    index++;
                }

                continue;
            }

            if (content[index] == '{')
            {
                tokens.Add(new VdfToken(VdfTokenKind.OpenBrace, "{"));
                index++;
                continue;
            }

            if (content[index] == '}')
            {
                tokens.Add(new VdfToken(VdfTokenKind.CloseBrace, "}"));
                index++;
                continue;
            }

            if (content[index] != '"')
            {
                while (index < content.Length && !char.IsWhiteSpace(content[index]) && content[index] is not '{' and not '}')
                {
                    index++;
                }

                continue;
            }

            index++;
            var value = new StringBuilder();
            while (index < content.Length)
            {
                var current = content[index++];
                if (current == '"')
                {
                    break;
                }

                if (current == '\\' && index < content.Length)
                {
                    var escaped = content[index];
                    if (escaped is '\\' or '"')
                    {
                        value.Append(escaped);
                        index++;
                        continue;
                    }
                }

                value.Append(current);
            }

            tokens.Add(new VdfToken(VdfTokenKind.Value, value.ToString()));
        }

        return tokens;
    }

    private static bool LooksLikeAbsolutePath(string value) =>
        Path.IsPathRooted(value) ||
        (value.Length >= 3 && char.IsLetter(value[0]) && value[1] == ':' && value[2] is '\\' or '/');

    private static void AddPath(ISet<string> paths, string value)
    {
        var trimmed = Path.TrimEndingDirectorySeparator(value.Trim());
        if (!string.IsNullOrEmpty(trimmed) && LooksLikeAbsolutePath(trimmed))
        {
            paths.Add(trimmed);
        }
    }

    private enum VdfTokenKind
    {
        Value,
        OpenBrace,
        CloseBrace
    }

    private sealed record VdfToken(VdfTokenKind Kind, string Value);
}
