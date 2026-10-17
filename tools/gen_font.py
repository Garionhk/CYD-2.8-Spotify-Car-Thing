#!/usr/bin/env python3
"""Build the title fonts that live in LittleFS (fs/fonts/*.cydf).

Same idea as the MP3 player's tools/gen_cjk_font.py (STHeiti; Big5 level 1 + GB2312 level 1 +
kana + Latin), plus Japanese kanji (JIS X 0208 level 1) and Korean (KS X 1001), in a different
container. The MP3 player loads its .vlw
once at boot, before Bluetooth, which costs ~12 bytes of RAM per glyph (~100 KB). Here
titles arrive at runtime with Wi-Fi and TLS up, and there is no 100 KB to spare. So:

  - a sorted index the device binary-searches on flash: no per-glyph RAM at all
  - 2-bit alpha (4 levels, as the MP3 player's title strips): half the size of 4-bit

Format (little-endian):
    header 16 B:  "CYDF", u8 version=1, u8 bpp=2, u8 pixelSize, u8 lineHeight,
                  u8 ascent, 3 B pad, u32 glyphCount
    index:        glyphCount x 12 B, sorted by codepoint:
                  u24 codepoint, u24 bitmap offset, u8 w, u8 h, i8 xOff, i8 yOff (from the
                  top of the line), u8 xAdvance, u8 pad
    bitmaps:      per glyph ceil(w*h*2/8) bytes, row-major, 2 bits per pixel, MSB first

Usage:
    python3 tools/gen_font.py                    # writes fs/fonts/b16.cydf and fs/fonts/s12.cydf
    python3 tools/gen_font.py --no-hangul        # without Korean (~280 KB less)
    python3 tools/gen_font.py --no-japanese      # without the Japanese-only kanji (~95 KB less)
Then: pio run -e cyd -t uploadfs

Faces (--faces): "noto" by default -- Noto Sans CJK TC Medium for Chinese, Japanese and Korean,
Noto Sans Medium for Latin, Greek, Cyrillic and punctuation (CJK faces draw ’ “ ” full-width);
both SIL Open Font License 1.1, so the release can ship them. "apple" uses STHeiti / Helvetica /
Apple SD Gothic Neo: fine on your own device, not redistributable. Half-width katakana are left
out (they show as '?').
"""
import argparse
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required:  python3 -m pip install Pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTO_DIR = os.path.join(ROOT, "tools", "fonts")
NOTO_CJK = os.path.join(NOTO_DIR, "NotoSansCJKtc-Medium.otf")
NOTO_LATIN = os.path.join(NOTO_DIR, "NotoSans-Medium.ttf")

# Face sets: the main (CJK) face, and scripts drawn from another face: (ranges, path, ttc index).
# "noto" is the default and the only one fit for a public release (SIL Open Font License 1.1).
# Get the two files once:
#   curl -L -o tools/fonts/NotoSansCJKtc-Medium.otf \
#     https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/TraditionalChinese/NotoSansCJKtc-Medium.otf
#   curl -L -o tools/fonts/NotoSans-Medium.ttf \
#     https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf/NotoSans-Medium.ttf
LATIN_RANGES = [range(0x20, 0x250), range(0x370, 0x500),            # Latin, Greek, Cyrillic
                range(0x2010, 0x2028), range(0x2030, 0x205F)]        # punctuation: CJK faces draw ' " full-width
FACE_SETS = {
    "noto": {
        "main": (NOTO_CJK, 0),
        "alt": [(LATIN_RANGES, NOTO_LATIN, 0)],
    },
    # Apple's fonts: fine on your own device, NOT redistributable.
    "apple": {
        "main": ("/System/Library/Fonts/STHeiti Medium.ttc", 0),
        "alt": [
            ([range(0x370, 0x500), range(0x2010, 0x2028), range(0x2030, 0x205F)],
             "/System/Library/Fonts/Helvetica.ttc", 0),
            ([range(0x1100, 0x1200), range(0x3130, 0x3190), range(0xAC00, 0xD7A4)],  # STHeiti has no Hangul
             "/System/Library/Fonts/AppleSDGothicNeo.ttc", 2),
        ],
    },
}
LINE_SAMPLE = "國傳説気한ABCÉÅÖgjpqy()[]Ω"  # tall, accented and descending: what a line must hold
SIZES = [("b16", 16), ("s12", 12)]  # title / body, and the small text over the cover
TOFU_PROBE = "\ue123"  # a private-use codepoint no face assigns


def decode_range(codec, lead_lo, lead_hi, trail_ranges):
    out = set()
    for lead in range(lead_lo, lead_hi + 1):
        for lo, hi in trail_ranges:
            for trail in range(lo, hi + 1):
                try:
                    out.add(bytes([lead, trail]).decode(codec))
                except UnicodeDecodeError:
                    pass
    return out


def charset(hangul, japanese):
    chars = set()
    for lo, hi in [(0x20, 0x7E), (0xA0, 0x17F), (0x391, 0x3C9), (0x401, 0x45F), (0x2010, 0x2027),
                   (0x2030, 0x205E), (0x3000, 0x303F), (0x3041, 0x30FF), (0xFF01, 0xFF5E)]:
        chars.update(chr(c) for c in range(lo, hi + 1))
    chars |= decode_range("big5", 0xA4, 0xC6, [(0x40, 0x7E), (0xA1, 0xFE)])  # Big5 level 1
    chars |= decode_range("gb2312", 0xB0, 0xD7, [(0xA1, 0xFE)])               # GB2312 level 1
    if japanese:
        chars |= decode_range("euc_jp", 0xB0, 0xCF, [(0xA1, 0xFE)])           # JIS X 0208 level 1
    if hangul:
        chars |= decode_range("euc-kr", 0xB0, 0xC8, [(0xA1, 0xFE)])           # KS X 1001 syllables
        chars.update(chr(c) for c in range(0x3131, 0x318F))                    # Hangul letters
    return sorted(c for c in chars if len(c) == 1)


def load_face(path_override, size, face_set):
    path, index = (path_override, 0) if path_override else FACE_SETS[face_set]["main"]
    if not os.path.exists(path):
        sys.exit(f"{path} not found. See the curl lines at the top of this script, or pass --face PATH.")
    return ImageFont.truetype(path, size, index=index), path


def tofu_sig(font):
    m = font.getmask(TOFU_PROBE, mode="L")
    return (m.size, bytes(m))


def build(name, size, chars, face_path, out_dir, face_set):
    main_font, path = load_face(face_path, size, face_set)
    alts = []  # (ranges, font, tofu signature)
    for ranges, alt_path, alt_index in FACE_SETS[face_set]["alt"]:
        if os.path.exists(alt_path):
            f = ImageFont.truetype(alt_path, size, index=alt_index)
            alts.append((ranges, f, tofu_sig(f)))
    sigs = {id(main_font): tofu_sig(main_font)}
    for _, f, sig in alts:
        sigs[id(f)] = sig
    def face_for(cp):
        for ranges, f, _ in alts:
            if any(cp in r for r in ranges):
                return f
        return main_font

    # Line height and baseline from the glyphs' real ink, not the font's declared metrics: Noto
    # declares 24 px lines for a 16 px font (the screen lays text out in tight rows).
    ascent = descent = 0
    for ch in LINE_SAMPLE:
        x0, y0, x1, y1 = face_for(ord(ch)).getbbox(ch, anchor="ls")
        ascent, descent = max(ascent, -y0), max(descent, y1)
    line_h = ascent + descent

    index, blobs, skipped, offset = [], [], 0, 0
    for ch in chars:
        cp = ord(ch)
        font = face_for(cp)
        adv = int(round(font.getlength(ch)))
        if ch != " ":
            # Characters the face lacks are left out rather than shipped as blank boxes.
            mask = font.getmask(ch, mode="L")
            if (mask.size, bytes(mask)) == sigs[id(font)]:
                skipped += 1
                continue
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")  # relative to the baseline
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        if w > 255 or h > 255 or not -128 <= x0 <= 127 or adv > 255:
            skipped += 1
            continue
        y_off = ascent + y0  # top of the ink, measured from the top of the line
        if not -128 <= y_off <= 127:
            skipped += 1
            continue
        data = b""
        if w and h:
            pad = max(8, h)
            img = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
            ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
            alpha = img.crop((pad, pad, pad + w, pad + h)).tobytes()
            bits = bytearray((w * h * 2 + 7) // 8)
            for i, a in enumerate(alpha):
                level = (a * 3 + 127) // 255  # 0..3
                bits[i // 4] |= level << (6 - 2 * (i % 4))
            data = bytes(bits)
        index.append(struct.pack("<I", cp)[:3] + struct.pack("<I", offset)[:3] +
                     struct.pack("<BBbbBB", w, h, x0, y_off, adv, 0))
        blobs.append(data)
        offset += len(data)
        if offset >= 1 << 24:
            sys.exit("font too large for 24-bit offsets")

    out = os.path.join(out_dir, name + ".cydf")
    with open(out, "wb") as f:
        f.write(b"CYDF" + struct.pack("<BBBBB3xI", 1, 2, size, line_h, ascent, len(index)))
        f.writelines(index)
        f.writelines(blobs)
    kb = os.path.getsize(out) / 1024
    print(f"{name}: {os.path.basename(path)} {size} px, line {line_h} px (ascent {ascent}), "
          f"{len(index)} glyphs ({skipped} skipped), {kb:.0f} KB -> {out}")
    return kb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-hangul", action="store_true", help="leave out Korean")
    ap.add_argument("--no-japanese", action="store_true", help="leave out the Japanese-only kanji")
    ap.add_argument("--faces", choices=sorted(FACE_SETS), default="noto",
                    help="noto (default, redistributable) or apple (own device only)")
    ap.add_argument("--face", help="main font file to use instead of the set's CJK face")
    ap.add_argument("--out", default=os.path.join(ROOT, "fs", "fonts"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    chars = charset(not args.no_hangul, not args.no_japanese)
    total = sum(build(name, size, chars, args.face, args.out, args.faces) for name, size in SIZES)
    print(f"total {total:.0f} KB of the 2432 KB LittleFS partition")


if __name__ == "__main__":
    main()
