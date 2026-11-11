#!/usr/bin/env python3
"""Cut the lower-tier rank badges and rebuild their finished trophies.

UNRANKED keeps only its core, drawn twice as large so it still reads at
list size. RED keeps only its four spike arrows and its core, enlarged 1.8
times so the arrows sit against it (its body layer is cleared).
AQUA and BRONZE lose the middle of each
side of their body layer (a horizontal and a vertical band through the
centre); the narrower the band, the higher the tier. Ornament layers and the
core stay whole, every layer keeps its 1024 px canvas, file name and role,
so the placement / promotion / demotion assemblies animate exactly as
before. Each trophy is the composite of its layers in z order at 1254 px,
which is how the original trophies relate to their layers, so the finished
badge matches the end of a transition.

The cut is idempotent: running it again leaves the images unchanged.
Requires Pillow and NumPy. SILVER and GOLD are never touched.
"""
from pathlib import Path

import numpy as np
from PIL import Image

RESOURCES = Path(__file__).resolve().parents[1] / 'resources'
CANVAS, TROPHY = 1024, 1254

# Layers in z order (RankBadgeAssets.cpp partsFor).
LAYERS = {
    'unranked': ['body', 'core', 'fragment-tl', 'fragment-tr', 'fragment-bl', 'fragment-br'],
    'red': ['body', 'spikes', 'core'],
    'aqua': ['orbit-back', 'body', 'core', 'orbit-front'],
    'bronze': ['body', 'frame-top', 'frame-bottom', 'frame-left', 'frame-right', 'core'],
}
# Half width in canvas pixels of the band removed through the middle of each
# side of the body layer.
BODY_CUT = {'bronze': 45, 'aqua': 80}
# Tiers whose body layer is cleared entirely.
BODY_CLEARED = ('red',)
# Cores enlarged around the canvas centre while they are still at their
# original size (idempotent: an enlarged core is left alone):
# tier -> (scale, original maximum width in canvas pixels).
CORE_ENLARGE = {'unranked': (2.0, 300), 'red': (1.8, 400)}


def layer_path(tier, role):
    return RESOURCES / f'rank-{tier}-{role}.png'


def load(path):
    image = Image.open(path).convert('RGBA')
    if image.size != (CANVAS, CANVAS):
        raise ValueError(f'{path.name}: expected a {CANVAS} px canvas, got {image.size}')
    return np.asarray(image).copy()


def save(path, pixels):
    Image.fromarray(pixels, 'RGBA').save(path, optimize=True)


def middle_bands(half_width):
    yy, xx = np.mgrid[0:CANVAS, 0:CANVAS]
    centre = (CANVAS - 1) / 2.0
    return (np.abs(xx - centre) < half_width) | (np.abs(yy - centre) < half_width)


def enlarge_core(tier, scale, original_max):
    path = layer_path(tier, 'core')
    pixels = load(path)
    ys, xs = np.nonzero(pixels[..., 3] > 16)
    if xs.max() - xs.min() > original_max:
        return
    size = round(CANVAS * scale)
    offset = (size - CANVAS) // 2
    big = Image.fromarray(pixels, 'RGBA').resize((size, size), Image.LANCZOS)
    save(path, np.asarray(big.crop((offset, offset, offset + CANVAS, offset + CANVAS))).copy())


def keep_red_arrows():
    # The four spike arrows only: the scattered pixels and hairlines of the
    # spikes layer would float around the enlarged core.
    path = layer_path('red', 'spikes')
    pixels = load(path)
    yy, xx = np.mgrid[0:CANVAS, 0:CANVAS]
    centre = (CANVAS - 1) / 2.0
    vertical = (np.abs(xx - centre) < 90) & ((yy < 200) | (yy > 820))
    horizontal = (np.abs(yy - centre) < 120) & ((xx < 200) | (xx > 820))
    pixels[~(vertical | horizontal)] = 0
    save(path, pixels)


def cut():
    for tier, (scale, original_max) in CORE_ENLARGE.items():
        enlarge_core(tier, scale, original_max)
    keep_red_arrows()
    for role in LAYERS['unranked']:
        if role == 'core':
            continue
        pixels = load(layer_path('unranked', role))
        pixels[...] = 0
        save(layer_path('unranked', role), pixels)
    for tier in BODY_CLEARED:
        pixels = load(layer_path(tier, 'body'))
        pixels[...] = 0
        save(layer_path(tier, 'body'), pixels)
    for tier, half_width in BODY_CUT.items():
        pixels = load(layer_path(tier, 'body'))
        pixels[middle_bands(half_width)] = 0
        save(layer_path(tier, 'body'), pixels)


def rebuild_trophies():
    for tier, roles in LAYERS.items():
        composite = Image.new('RGBA', (CANVAS, CANVAS), (0, 0, 0, 0))
        for role in roles:
            composite.alpha_composite(Image.fromarray(load(layer_path(tier, role)), 'RGBA'))
        composite.resize((TROPHY, TROPHY), Image.LANCZOS).save(RESOURCES / f'corum-trophy-{tier}.png', optimize=True)


if __name__ == '__main__':
    cut()
    rebuild_trophies()
    print('Cut lower-tier badge layers and rebuilt their trophies.')
