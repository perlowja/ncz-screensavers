"""Pure image helpers for the host-test harness.

These functions are intentionally framework-free (no PIL, no numpy) so the
agent can run on the build hosts with only the Python 3.13 standard library.
The agent module imports from here; the unit tests import from here too.

Performance contract: every helper below is engineered to finish in well under
one second on a 480x270 RGB frame (the default ``grim -t ppm -s 0.25`` capture
of a 1920x1080 panel). The trick is to operate on the flat RGB byte buffer
directly with ``bytes`` slicing and ``zip`` over disjoint channel groups,
which avoids constructing per-pixel tuples.
"""

from __future__ import annotations

from collections.abc import Iterable

# Number of channels in a P6 (binary) PPM frame.
_PPM_CHANNELS = 3

# Pixel difference threshold used by ``frame_diff_fraction``. Matches the
# "differs by more than N in any channel" definition in the design brief.
_DEFAULT_DIFF_THRESHOLD = 12

# Brightness threshold used by ``coverage_fraction``. "Pixel is non-black" ==
# ``max(r,g,b) > N`` (in 0..255 range).
_DEFAULT_BRIGHT_THRESHOLD = 16


class PPMError(ValueError):
    """Raised for malformed PPM frames (bad header, short payload, etc)."""


def parse_ppm(data: bytes) -> tuple[int, int, bytes]:
    """Parse a binary P6 PPM frame.

    Returns ``(width, height, rgb_bytes)`` where ``rgb_bytes`` is a flat
    ``width*height*3`` byte string (row-major, top-to-bottom). Header parsing
    tolerates the single comment line that most PPM emitters insert between
    the magic and the dimensions.

    Raises :class:`PPMError` on any malformed input.
    """
    if len(data) < 3 or data[:2] != b"P6":
        raise PPMError("not a P6 PPM frame")
    pos = 2
    # Consume any ASCII whitespace and full-line comments between header
    # fields. PPM emitters (including ``convert`` and most hand-rolled tools)
    # are allowed to insert "# ..." lines anywhere before the payload.
    pos = _skip_ws_and_comments(data, pos)
    width, pos = _read_int(data, pos)
    pos = _skip_ws_and_comments(data, pos)
    height, pos = _read_int(data, pos)
    pos = _skip_ws_and_comments(data, pos)
    maxval, pos = _read_int(data, pos)
    # Exactly ONE whitespace byte separates the header from the pixels; any
    # further whitespace-valued bytes (0x09-0x0d, 0x20) are pixel data.
    pos += 1
    if pos >= len(data):
        raise PPMError("PPM header not terminated before payload")
    if maxval != 255:
        raise PPMError(f"unsupported PPM maxval {maxval}; only 8-bit frames accepted")
    expected = width * height * _PPM_CHANNELS
    if len(data) - pos < expected:
        raise PPMError(
            f"PPM payload too short: got {len(data) - pos} bytes, expected {expected}"
        )
    return width, height, bytes(data[pos : pos + expected])


def _read_int(data: bytes, pos: int) -> tuple[int, int]:
    """Read a base-10 integer at ``pos``, returning ``(value, new_pos)``.

    Skips leading ASCII whitespace (space, tab, CR, LF). Stops at the first
    non-digit. Raises :class:`PPMError` if no digit is present.
    """
    while pos < len(data) and data[pos : pos + 1] in (b" ", b"\t", b"\n", b"\r"):
        pos += 1
    start = pos
    while pos < len(data) and 0x30 <= data[pos] <= 0x39:
        pos += 1
    if pos == start:
        raise PPMError("PPM header ended before an integer was found")
    return int(data[start:pos]), pos


def _skip_ws_and_comments(data: bytes, pos: int) -> int:
    """Skip ASCII whitespace and ``# ... \\n`` comment lines, return new pos."""
    while pos < len(data):
        b = data[pos : pos + 1]
        if b in (b" ", b"\t", b"\n", b"\r"):
            pos += 1
        elif b == b"#":
            nl = data.find(b"\n", pos)
            if nl < 0:
                # Comment runs off the end of the header; treat as terminator.
                return len(data)
            pos = nl + 1
        else:
            return pos
    return pos


def _iter_channel_triples(
    buf: bytes, step: int = _PPM_CHANNELS
) -> Iterable[tuple[int, int, int]]:
    """Yield ``(r, g, b)`` triples from a flat RGB byte buffer.

    Operates on a buffer view (``memoryview``) so we don't allocate a list of
    tuples; the ``zip`` of three disjoint slices runs in C. The caller is
    responsible for ``buf`` length being a multiple of ``step``.
    """
    view = memoryview(buf)
    # Using slicing produces ``memoryview`` views, which zip iterates lazily.
    return zip(view[0::step], view[1::step], view[2::step])


def coverage_fraction(buf: bytes, threshold: int = _DEFAULT_BRIGHT_THRESHOLD) -> float:
    """Return the fraction of pixels where ``max(r,g,b) > threshold``.

    For an entirely black frame the result is 0.0; for a fully white frame
    the result is 1.0. Pixels are counted in row-major order; the function
    does not care about layout, only about ``len(buf) % 3 == 0``.
    """
    if not buf:
        return 0.0
    if len(buf) % _PPM_CHANNELS != 0:
        raise PPMError("RGB buffer length is not a multiple of 3")
    total = len(buf) // _PPM_CHANNELS
    bright = 0
    for r, g, b in _iter_channel_triples(buf):
        if r > threshold or g > threshold or b > threshold:
            bright += 1
    return bright / total


def tile_coverage(
    width: int,
    height: int,
    buf: bytes,
    tile: int = 8,
    threshold: int = _DEFAULT_BRIGHT_THRESHOLD,
) -> float:
    """Fraction of tile x tile blocks containing at least one lit pixel.

    Tells a sparse scene (points or lines on black) from an empty frame.
    """
    if width <= 0 or height <= 0 or len(buf) < width * height * _PPM_CHANNELS:
        return 0.0
    tx = (width + tile - 1) // tile
    ty = (height + tile - 1) // tile
    lit = bytearray(tx * ty)
    for y in range(height):
        row = buf[y * width * 3 : (y + 1) * width * 3]
        base = (y // tile) * tx
        for x, (r, g, b) in enumerate(
            zip(row[0::3], row[1::3], row[2::3], strict=False)
        ):
            if r > threshold or g > threshold or b > threshold:
                lit[base + x // tile] = 1
    return sum(lit) / len(lit)


def frame_diff_fraction(
    a: bytes,
    b: bytes,
    threshold: int = _DEFAULT_DIFF_THRESHOLD,
) -> float:
    """Fraction of pixels where any channel differs by more than ``threshold``.

    Both frames must be the same length. The check is ``abs(a_i - b_i) > t``
    for any of the three channels; pixels are counted once if *any* channel
    triggers. Returns 0.0 if the frames are identical.
    """
    if len(a) != len(b):
        raise PPMError("frame_diff_fraction: buffers must be the same length")
    if not a:
        return 0.0
    n = len(a)
    pixels = n // _PPM_CHANNELS
    if pixels == 0:
        return 0.0
    diff = 0
    # Walk the three channel planes separately so each step is a pure
    # subtraction over ``memoryview``. Using a manual index loop is faster
    # than zipping three slices on CPython because of the per-iteration
    # Python overhead.
    av = memoryview(a)
    bv = memoryview(b)
    for i in range(0, n, _PPM_CHANNELS):
        dr = av[i] - bv[i]
        if dr < 0:
            dr = -dr
        if dr > threshold:
            diff += 1
            continue
        dg = av[i + 1] - bv[i + 1]
        if dg < 0:
            dg = -dg
        if dg > threshold:
            diff += 1
            continue
        db = av[i + 2] - bv[i + 2]
        if db < 0:
            db = -db
        if db > threshold:
            diff += 1
    return diff / pixels


def mean_brightness_of_bright_pixels(
    buf: bytes, threshold: int = _DEFAULT_BRIGHT_THRESHOLD
) -> tuple[float, float, float, int]:
    """Mean per-channel RGB across pixels whose ``max(r,g,b) > threshold``.

    Returns ``(mean_r, mean_g, mean_b, count)``. If no pixel passes the
    threshold the means are 0.0 and the count is 0. Used by the color
    phase to discriminate color modes that have the same brightness but
    different hues (the design brief says "different chromaticity OR
    different brightness").
    """
    if len(buf) % _PPM_CHANNELS != 0:
        raise PPMError("RGB buffer length is not a multiple of 3")
    if not buf:
        return 0.0, 0.0, 0.0, 0
    av = memoryview(buf)
    n = len(buf)
    sr = sg = sb = 0
    count = 0
    for i in range(0, n, _PPM_CHANNELS):
        r = av[i]
        g = av[i + 1]
        b = av[i + 2]
        if r > threshold or g > threshold or b > threshold:
            sr += r
            sg += g
            sb += b
            count += 1
    if count == 0:
        return 0.0, 0.0, 0.0, 0
    return sr / count, sg / count, sb / count, count


def chromaticity_distance(
    rgb_a: tuple[float, float, float],
    rgb_b: tuple[float, float, float],
) -> float:
    """Euclidean distance between two normalized chromaticity vectors.

    Chromaticity is ``(r / (r+g+b), g / (r+g+b))``; the blue component is
    implicit and dropped (it sums to 1 minus the other two). A pair of
    black frames (denominator == 0 for both) returns 0.0; the caller is
    responsible for comparing like with like via ``coverage_fraction``.
    """
    ra, ga, ba = rgb_a
    rb, gb, bb = rgb_b
    sa = ra + ga + ba
    sb = rb + gb + bb
    if sa <= 0 or sb <= 0:
        return 0.0
    ax, ay = ra / sa, ga / sa
    bx, by = rb / sb, gb / sb
    dx = ax - bx
    dy = ay - by
    # sqrt is overkill for the 0..sqrt(2) range we care about, but readability
    # beats micro-optimisation here.
    return (dx * dx + dy * dy) ** 0.5


def brightness_ratio(a: float, b: float) -> float:
    """Return ``max(a, b) / min(a, b)`` for the two mean brightnesses.

    Returns 1.0 if either operand is zero (the caller should already have
    excluded the all-black case via ``coverage_fraction``).
    """
    if a <= 0 or b <= 0:
        return 1.0
    if a > b:
        return a / b
    return b / a
