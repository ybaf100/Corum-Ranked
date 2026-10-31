#!/usr/bin/env python3
"""Merge exactly one channel's five Native artifacts with the pinned Geode CLI.

The upstream combine action downloads all artifacts with fixed names. A shared
release run must instead isolate Release/Debug inputs before invoking the same
`geode package merge --optimize-pngs` operation.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

MOD = 'hwanhee1.corum_ranked'
TARGETS = {'Win64': '.dll', 'MacOS': '.dylib', 'iOS': '.ios.dylib',
           'Android32': '.android32.so', 'Android64': '.android64.so'}
BINARIES = {MOD + suffix for suffix in TARGETS.values()}


def combine(source, channel, version, output, run=subprocess.run):
    if channel not in ('release', 'debug'):
        raise ValueError('Unsupported channel')
    source, output = Path(source), Path(output)
    expected_dirs = {'corum-platform-' + channel + '-' + target for target in TARGETS}
    if {p.name for p in source.iterdir()} != expected_dirs:
        raise ValueError('Mixed channels, missing target or unexpected build artifact')
    inputs = []
    for target, suffix in TARGETS.items():
        packages = list((source / f'corum-platform-{channel}-{target}').glob('*.geode'))
        if len(packages) != 1:
            raise ValueError('Expected exactly one package per platform')
        with zipfile.ZipFile(packages[0]) as archive:
            names = archive.namelist()
            manifest = json.loads(archive.read('mod.json'))
            if len(names) != len(set(names)) or set(names) & BINARIES != {MOD + suffix}:
                raise ValueError('Wrong Native platform or duplicate archive entries')
            if manifest.get('id') != MOD or manifest.get('version') != version:
                raise ValueError('Wrong package identity/version')
        inputs.append(packages[0])
    output.mkdir(parents=True, exist_ok=False)
    # CLI mutates its first input. Preserve the downloaded original artifacts.
    copies = []
    for target, path in zip(TARGETS, inputs):
        copy = output / (target + '.geode')
        shutil.copyfile(path, copy)
        copies.append(copy)
    run(['geode', 'package', 'merge', '--optimize-pngs', *map(str, copies)], check=True)
    with zipfile.ZipFile(copies[0]) as archive:
        if not BINARIES <= set(archive.namelist()):
            raise ValueError('Combined package is missing Native platforms')
    final = output / (MOD + '.geode')
    copies[0].rename(final)
    for copy in copies[1:]:
        copy.unlink()
    return final


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('channel', choices=('release', 'debug'))
    parser.add_argument('version')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(combine(args.source, args.channel, args.version, args.output))
