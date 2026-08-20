#!/usr/bin/env python3

import argparse
import re
from pathlib import Path


VERSION_PATTERN = re.compile(
    r"^v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
    r"(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the GitHub Release body from the matching changelog section."
    )
    parser.add_argument("--version", required=True)
    parser.add_argument("--changelog", type=Path, required=True)
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def normalize_heading(heading: str) -> str:
    return heading.replace(r"\.", ".").strip()


def extract_version_notes(changelog: str, version: str) -> str:
    lines = changelog.splitlines()
    section_start = None

    for index, line in enumerate(lines):
        match = re.fullmatch(r"#\s+(.+?)\s*", line)
        if match and normalize_heading(match.group(1)) == version:
            section_start = index + 1
            break

    if section_start is None:
        raise ValueError(f"Changelog section not found for {version}")

    section_end = len(lines)
    for index in range(section_start, len(lines)):
        if re.fullmatch(r"#\s+.+?\s*", lines[index]):
            section_end = index
            break

    notes = "\n".join(lines[section_start:section_end]).strip()
    if not notes:
        raise ValueError(f"Changelog section for {version} is empty")
    if not any(line.startswith("- ") for line in notes.splitlines()):
        raise ValueError(f"Changelog section for {version} has no bullet points")
    return notes


def main() -> None:
    args = parse_args()
    if not VERSION_PATTERN.fullmatch(args.version):
        raise ValueError(f"Invalid release version: {args.version}")

    template = args.template.read_text(encoding="utf-8")
    required_placeholders = ("{{VERSION}}", "{{CHANGELOG}}")
    missing_placeholders = [
        placeholder for placeholder in required_placeholders if placeholder not in template
    ]
    if missing_placeholders:
        raise ValueError(
            "Release notes template is missing placeholders: "
            + ", ".join(missing_placeholders)
        )

    notes = extract_version_notes(
        args.changelog.read_text(encoding="utf-8"), args.version
    )
    release_body = template.replace("{{VERSION}}", args.version).replace(
        "{{CHANGELOG}}", notes
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(release_body.rstrip() + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
