#!/usr/bin/env python3
"""Build port/assets/fonts/ChronicleTwoMessage-Regular.ttf from Kosugi and scripts/font/message_font.json.

Each Kosugi component goes through the FreeType steps the fitter rendered (the affine, then the stem
offset), is read back point for point, clipped where its recipe says, and a glyph's parts are merged into
one outline. Needs freetype-py, fonttools and skia-pathops (scripts/font/requirements.txt), and fetches
Kosugi once into ~/.cache/chronicletwo. No game data is read.
"""
import argparse
import sys
from pathlib import Path

import pathops
from fontTools import agl
from fontTools.fontBuilder import FontBuilder
from fontTools.misc.timeTools import timestampFromString
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.pointPen import PointToSegmentPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.reverseContourPen import ReverseContourPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

sys.path.insert(0, str(Path(__file__).resolve().parent))
import message_font as mf  # noqa: E402

FAMILY = "ChronicleTwoMessage"
VERSION = "1.000"
VERSION_DATE = "Sat Oct 24 00:00:00 2026"


def kosugi_part(source, ch, affine, strength, clip):
    """A Kosugi component as the fitter rendered it, as a pathops path in master units."""
    raw = source.outline(ch, affine, strength)
    path = pathops.Path()
    pen = PointToSegmentPen(path.getPen())
    start = 0
    for c in range(raw.n_contours):
        end = raw.contours[c]
        pen.beginPath()
        previous_on = bool(raw.tags[end] & 1)
        for i in range(start, end + 1):
            on = bool(raw.tags[i] & 1)
            pen.addPoint((raw.points[i].x, raw.points[i].y), ("line" if previous_on else "qcurve") if on else None)
            previous_on = on
        pen.endPath()
        start = end + 1
    if clip:
        a, _, _, d, e, f = affine
        x0, y0, x1, y1 = clip
        xs = sorted((a * x0 + e, a * x1 + e))
        ys = sorted((d * y0 + f, d * y1 + f))
        box = pathops.Path()
        box.moveTo(xs[0], ys[0])
        box.lineTo(xs[1], ys[0])
        box.lineTo(xs[1], ys[1])
        box.lineTo(xs[0], ys[1])
        box.close()
        path = pathops.op(path, box, pathops.PathOp.INTERSECTION)
    return path


def outline(source, components):
    merged = pathops.Path()
    for component in components:
        part = component[1] if component[0] == "path" else kosugi_part(source, *component[1:])
        merged = pathops.op(merged, part, pathops.PathOp.UNION)
    return merged


def signed_area(recording):
    """Twice the signed area of a recorded outline's on-curve polygon; positive runs counter-clockwise."""
    total, first, last = 0.0, None, None
    for op, args in recording.value:
        if op == "moveTo":
            first = last = args[0]
        elif op in ("lineTo", "qCurveTo", "curveTo"):
            total += last[0] * args[-1][1] - args[-1][0] * last[1]
            last = args[-1]
        elif op in ("closePath", "endPath") and first is not None:
            total += last[0] * first[1] - first[0] * last[1]
    return total


def tt_glyph(path):
    recording = RecordingPen()
    path.draw(Cu2QuPen(recording, 0.5))
    pen = TTGlyphPen(None)
    # TrueType outlines run clockwise around the ink; pathops leaves them the other way.
    recording.replay(ReverseContourPen(pen) if signed_area(recording) > 0 else pen)
    glyph = pen.glyph()
    if glyph.numberOfContours:
        glyph.recalcBounds(None)
    return glyph


def glyph_name(ch):
    return agl.UV2AGL.get(ord(ch)) or f"uni{ord(ch):04X}"


def build(kosugi, parameters, out):
    source = mf.Source(kosugi)
    table = mf.Recipes(source, parameters).glyphs()
    order = [".notdef"] + [glyph_name(ch) for ch in sorted(table)]
    glyf = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (mf.HALF, 0)}
    cmap = {}
    for ch in sorted(table):
        advance, components = table[ch]
        name = glyph_name(ch)
        glyph = tt_glyph(outline(source, components))
        glyf[name] = glyph
        # FreeType and stb_truetype place an outline by its side bearing, so it is the outline's left edge.
        metrics[name] = (advance, glyph.xMin if glyph.numberOfContours else 0)
        cmap[ord(ch)] = name
    builder = FontBuilder(mf.UNITS_PER_EM, isTTF=True)
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyf)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=mf.ASCENT, descent=-mf.DESCENT, lineGap=0)
    builder.setupNameTable({
        "copyright": "Copyright 2010 The Kosugi Project Authors (https://github.com/googlefonts/kosugi). "
                     "Modified for ChronicleTwo; see NOTICE.",
        "familyName": FAMILY,
        "styleName": "Regular",
        "uniqueFontIdentifier": f"{FAMILY}-Regular {VERSION}",
        "fullName": f"{FAMILY} Regular",
        "version": f"Version {VERSION}",
        "psName": f"{FAMILY}-Regular",
        "description": "Dark Chronicle's message font as outlines, made from Kosugi for the ChronicleTwo port.",
        "licenseDescription": "Licensed under the Apache License, Version 2.0",
        "licenseInfoURL": "https://www.apache.org/licenses/LICENSE-2.0",
    })
    builder.setupOS2(sTypoAscender=mf.ASCENT, sTypoDescender=-mf.DESCENT, sTypoLineGap=0, usWinAscent=mf.ASCENT,
                     usWinDescent=mf.DESCENT, sxHeight=glyf[glyph_name("x")].yMax,
                     sCapHeight=glyf[glyph_name("H")].yMax, usWeightClass=400, fsType=0, achVendID="CHR2",
                     fsSelection=0x40)
    builder.setupPost()
    builder.setupHead(unitsPerEm=mf.UNITS_PER_EM, fontRevision=float(VERSION))
    font = builder.font
    font["OS/2"].recalcUnicodeRanges(font)
    font["OS/2"].recalcCodePageRanges(font)
    # A fixed date, the version's, keeps the build reproducible: the same inputs give the same bytes.
    font["head"].created = font["head"].modified = timestampFromString(VERSION_DATE)
    font.recalcTimestamp = False
    out.parent.mkdir(parents=True, exist_ok=True)
    builder.save(str(out))
    return len(table)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kosugi", type=Path, help="Kosugi-Regular.ttf to use instead of fetching it")
    parser.add_argument("--parameters", type=Path, default=mf.PARAMETERS)
    parser.add_argument("--out", type=Path, default=mf.FONT)
    args = parser.parse_args()
    count = build(args.kosugi or mf.kosugi_path(), mf.load_parameters(args.parameters), args.out)
    print(f"{count} glyphs -> {args.out}")


if __name__ == "__main__":
    main()
