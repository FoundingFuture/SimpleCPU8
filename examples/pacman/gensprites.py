#!/usr/bin/env python3
"""Draws Pac-Man's sprite strips from shapes, anti-aliased, as 14 x 14 frames.

    python3 gensprites.py          writes the seven PNGs beside this file

Each pixel is sampled 16 x 16 times. A sample is inside a shape or not, and
the pixel is the average of what its samples hit. An edge against the maze
takes the colour darkened toward black by how much of the pixel the shape
covers. An edge inside the sprite, such as an eye on the body, mixes the two
colours. The result is matched to the nearest 3-3-2 palette entry outside 0
to 7. 0 is transparent and the maze fades rewrite 1 to 3 and 5 to 7. 4 is
the pupils, which the program sets to near black. A pixel all pupil is 4.

The PNGs are the source the ROM is built from, and the sprite editor opens
them. Running this again replaces any hand edits to them.

Needs Pillow.
"""

import math
import os

from PIL import Image
from PIL.PngImagePlugin import PngInfo

SIZE = 14
SAMPLES = 16
LOW, HIGH = 0.18, 0.82  # coverage below LOW is transparent, above HIGH full colour
HERE = os.path.dirname(os.path.abspath(__file__))

PALETTE = []
for i in range(256):
    PALETTE.append((round(((i >> 5) & 7) * 255 / 7), round(((i >> 2) & 7) * 255 / 7), round((i & 3) * 255 / 3)))
PUPIL = 4
PUPIL_RGB = (24, 24, 24)  # what the program loads into index 4
WALLDIM, DOTDIM, PILLDIM = 5, 6, 7  # the maze's anti-aliasing shades, which the fades write
RESERVED = {0, 1, 2, 3, PUPIL, WALLDIM, DOTDIM, PILLDIM}


def rgb(index):
    return PUPIL_RGB if index == PUPIL else PALETTE[index]


def nearest(colour):
    best, best_d = 5, 1e18
    for i, c in enumerate(PALETTE):
        if i in RESERVED:
            continue
        d = sum((a - b) ** 2 for a, b in zip(c, colour))
        if d < best_d:
            best, best_d = i, d
    return best


def render(scene):
    """scene(x, y) gives a palette index at a point, or None for nothing."""
    frame = []
    for py in range(SIZE):
        row = []
        for px in range(SIZE):
            hits = []
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    c = scene(px + (sx + 0.5) / SAMPLES, py + (sy + 0.5) / SAMPLES)
                    if c is not None:
                        hits.append(c)
            cover = len(hits) / (SAMPLES * SAMPLES)
            if cover < LOW:
                row.append(0)
                continue
            if cover > HIGH and len(set(hits)) == 1:
                row.append(hits[0])
                continue
            mix = [sum(rgb(c)[k] for c in hits) / len(hits) for k in range(3)]
            if cover <= HIGH:
                mix = [v * cover for v in mix]
            row.append(nearest(mix))
        frame.append(row)
    return frame


def save(name, frames, fps):
    image = Image.new("P", (SIZE * len(frames), SIZE))
    image.putpalette([v for c in PALETTE for v in c])
    for k, frame in enumerate(frames):
        for y in range(SIZE):
            for x in range(SIZE):
                image.putpixel((k * SIZE + x, y), frame[y][x])
    info = PngInfo()
    info.add_text("SimpleCPU-8 frames", str(len(frames)))
    info.add_text("SimpleCPU-8 fps", str(fps))
    image.save(os.path.join(HERE, name + ".png"), transparency=0, pnginfo=info)


C = SIZE / 2  # the centre
R = C - 0.1   # a disc that just fits the frame
FACINGS = [(1, 0), (0, 1), (-1, 0), (0, -1)]  # right, down, left, up


def pacman(facing, half_angle):
    dx, dy = FACINGS[facing]
    heading = math.atan2(dy, dx)
    # The wedge's apex sits a little behind the centre, so the lips meet.
    ax, ay = C - 0.7 * dx, C - 0.7 * dy

    def scene(x, y):
        if (x - C) ** 2 + (y - C) ** 2 > R * R:
            return None
        if half_angle:
            a = math.degrees(math.atan2(y - ay, x - ax) - heading)
            if abs((a + 180) % 360 - 180) <= half_angle:
                return None
        return 0xFC

    return render(scene)


def skirt_bottom(x, phase):
    """The skirt's lower edge at x: points hang to the frame's bottom, the
    notches between them rise two pixels. Phase 0 puts points at both sides,
    phase 1 moves them half a step, so the skirt ripples."""
    period = SIZE / 3
    u = (x / period + phase * 0.5) % 1.0
    return SIZE - 2.2 * (1 - abs(2 * u - 1))


def body(x, y, phase):
    if y < C:
        return (x - C) ** 2 + (y - C) ** 2 <= R * R
    return 0.1 <= x <= SIZE - 0.1 and y <= skirt_bottom(x, phase)


def eyes(x, y, facing):
    """White, pupil or None. The whites lean the way the ghost looks, and the
    pupils sit at that side of them."""
    dx, dy = FACINGS[facing]
    for ex in (4.3, 9.7):
        cx, cy = ex + 0.6 * dx, 5.8 + 0.6 * dy
        if ((x - cx) / 2.2) ** 2 + ((y - cy) / 2.7) ** 2 > 1:
            continue
        if (x - cx - 1.0 * dx) ** 2 + (y - cy - 1.3 * dy) ** 2 <= 1.05 ** 2:
            return PUPIL
        return 0xFF
    return None


def ghost(colour, facing, phase):
    def scene(x, y):
        e = eyes(x, y, facing)
        if e is not None:
            return e
        return colour if body(x, y, phase) else None

    return render(scene)


def eaten(facing):
    return render(lambda x, y: eyes(x, y, facing))


def mouth_distance(x, y):
    """How far a point is from the frightened mouth, a zigzag across the
    lower face."""
    points = [(2.2 + i * 1.6, 9.9 if i % 2 == 0 else 8.5) for i in range(7)]
    best = 1e9
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        vx, vy = x1 - x0, y1 - y0
        t = max(0.0, min(1.0, ((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy)))
        best = min(best, math.hypot(x - x0 - t * vx, y - y0 - t * vy))
    return best


def frightened(colour, face, phase):
    def scene(x, y):
        if not body(x, y, phase):
            return None
        for ex in (4.8, 9.2):
            if (x - ex) ** 2 + (y - 5.4) ** 2 <= 1.25 ** 2:
                return face
        if mouth_distance(x, y) <= 0.6:
            return face
        return colour

    return render(scene)


def main():
    # Pac-Man: four facings, each closed, half open and open.
    save("pacman", [pacman(f, half) for f in range(4) for half in (0, 24, 44)], 12)
    # A ghost: two skirts for each facing.
    for name, colour in (("blinky", 0xE0), ("pinky", 0xF3), ("inky", 0x1F), ("clyde", 0xF0)):
        save(name, [ghost(colour, f, p) for f in range(4) for p in (0, 1)], 8)
    # Frightened: blue with both skirts, then the white warning with both.
    save("fright", [frightened(0x27, 0xFF, 0), frightened(0x27, 0xFF, 1),
                    frightened(0xFF, 0xE0, 0), frightened(0xFF, 0xE0, 1)], 8)
    # The eyes of an eaten ghost, each facing twice to match the ghosts.
    save("eyes", [eaten(f) for f in range(4) for _ in (0, 1)], 8)


if __name__ == "__main__":
    main()
