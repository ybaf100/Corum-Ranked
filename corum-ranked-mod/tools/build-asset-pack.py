#!/usr/bin/env python3
"""Build the immutable, single-download Ranked image pack (no mod binaries/fonts)."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

MOD_ROOT = Path(__file__).resolve().parents[1]
ZIP_NAME = 'Corum-Ranked-Assets.zip'


def build(resources: Path, specification: Path, output: Path, version=None):
    spec = json.loads(specification.read_text(encoding='utf-8'))
    version = version or spec['assetPackVersion']
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}', version) or version in ('.', '..'):
        raise ValueError('Version must be a safe release tag, 1..64 ASCII characters')
    names = spec['files']
    if len(names) != 61 or len(set(names)) != 61 or any(Path(n).name != n or not n.endswith('.png') for n in names):
        raise ValueError('Expected the complete inventory of 61 flat PNG names')
    discovered = {p.name for p in resources.glob('*.png')}
    if discovered != set(names):
        raise ValueError(f'Image inventory differs: missing={set(names)-discovered}, extra={discovered-set(names)}')
    payload = {}
    for name in sorted(names):
        path = resources / name
        data = path.read_bytes()
        if path.is_symlink() or not data.startswith(b'\x89PNG\r\n\x1a\n') or len(data) > 8 * 1024 * 1024:
            raise ValueError(f'Invalid PNG: {name}')
        payload[name] = data
    payload['GLOBED-LICENSE.txt'] = (resources / 'GLOBED-LICENSE.txt').read_bytes()
    manifest = {
        'schema': 1, 'assetPackVersion': version,
        'files': [{'name': name, 'sizeBytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                  for name, data in sorted(payload.items())],
    }
    payload['asset-manifest.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ZIP_NAME
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as packed:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            packed.writestr(info, data, compresslevel=9)
    data = archive.read_bytes()
    if len(data) > 64 * 1024 * 1024:
        raise ValueError('Pack exceeds the client download limit')
    config = {
        'enabled': True, 'assetPackVersion': version,
        'sha256': hashlib.sha256(data).hexdigest(), 'sizeBytes': len(data),
    }
    metadata = {
        'repository': 'ybaf100/corum-ranked-assets', 'releaseTag': version,
        'filename': ZIP_NAME, 'imageCount': len(names),
        'downloadUrl': f'https://github.com/ybaf100/corum-ranked-assets/releases/download/{version}/{ZIP_NAME}',
        'client': {'assets': config},
        'sheetSettings': {
            'rankedAssetPackEnabled': True, 'rankedAssetPackVersion': version,
            'rankedAssetPackSha256': config['sha256'], 'rankedAssetPackSizeBytes': config['sizeBytes'],
        },
    }
    (output / 'asset-pack-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(metadata, indent=2))
    return archive, metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, default=MOD_ROOT / 'resources')
    parser.add_argument('--specification', type=Path, default=MOD_ROOT / 'asset-pack.json')
    parser.add_argument('--output', type=Path, default=MOD_ROOT / 'asset-pack-dist')
    parser.add_argument('--version', help='Exact server-selected GitHub release tag')
    args = parser.parse_args()
    build(args.resources, args.specification, args.output, args.version)
