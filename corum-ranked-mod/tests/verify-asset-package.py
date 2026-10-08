#!/usr/bin/env python3
"""Check the external ZIP and that every visual PNG is excluded from the .geode."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def verify_pack(pack, metadata):
    meta = json.loads(Path(metadata).read_text())
    config = meta['client']['assets']
    data = Path(pack).read_bytes()
    assert len(data) == config['sizeBytes'], 'Pack size mismatch'
    assert hashlib.sha256(data).hexdigest() == config['sha256'], 'Pack digest mismatch'
    assert meta['releaseTag'] == config['assetPackVersion']
    assert meta['repository'] == 'ybaf100/corum-ranked-assets'
    assert meta['downloadUrl'] == f"https://github.com/ybaf100/corum-ranked-assets/releases/download/{meta['releaseTag']}/Corum-Ranked-Assets.zip"
    expected = set(json.loads((ROOT / 'asset-pack.json').read_text())['files'])
    with zipfile.ZipFile(pack) as archive:
        assert archive.testzip() is None, 'Bad ZIP CRC'
        assert len(archive.namelist()) == len(set(archive.namelist())) == 63, 'Unexpected/duplicate entries'
        assert set(archive.namelist()) == expected | {'asset-manifest.json', 'GLOBED-LICENSE.txt'}
        manifest = json.loads(archive.read('asset-manifest.json'))
        assert manifest['schema'] == 1 and manifest['assetPackVersion'] == config['assetPackVersion']
        assert len(manifest['files']) == 62
        assert {f['name'] for f in manifest['files']} == expected | {'GLOBED-LICENSE.txt'}
        for file in manifest['files']:
            assert Path(file['name']).name == file['name'], 'Nested archive path'
            payload = archive.read(file['name'])
            assert len(payload) == file['sizeBytes'] and hashlib.sha256(payload).hexdigest() == file['sha256']
    print(f"Verified {len(expected)} images in {Path(pack).name}, {len(data)} bytes; tag {meta['releaseTag']}")


def verify_mod(package):
    expected = set(json.loads((ROOT / 'asset-pack.json').read_text())['files'])
    with zipfile.ZipFile(package) as archive:
        manifest = json.loads(archive.read('mod.json'))
        assert not manifest['resources'].get('sprites'), 'Visuals still bundled in sprite sheet'
        for name in archive.namelist():
            base = Path(name).name
            assert base not in expected, f'Visual still bundled: {name}'
            assert not base.startswith(('rank-', 'fx-', 'placement-grade-', 'corum-trophy-', 'corum-fire-')) or not base.endswith(('.png', '.plist'))
        assert any(Path(n).name.startswith('CorumSans') and n.endswith('.fnt') for n in archive.namelist()), 'Fonts must remain bundled'
    print(f'Verified external visual separation in {Path(package).name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path)
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--mod', type=Path)
    args = parser.parse_args()
    if args.pack:
        if not args.metadata: parser.error('--pack requires --metadata')
        verify_pack(args.pack, args.metadata)
    if args.mod: verify_mod(args.mod)
    if not args.pack and not args.mod: parser.error('Supply --pack or --mod')
