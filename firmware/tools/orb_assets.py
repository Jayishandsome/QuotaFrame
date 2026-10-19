"""Render the flying-orb character into LVGL RGB565A8 images.

The orb is split into layers so the board can animate it cheaply:
body (sphere, visor, jets), left/right hand with light trail, sparkles.
Eyes, ripples and dots are drawn by LVGL itself. Coordinates follow the
200x200 design viewBox; every layer is rendered on a padded canvas
(-20,-30)-(220,210) and cropped, keeping its offset.

Usage: python3 tools/orb_assets.py   (needs playwright + pillow)
Writes main/assets/orb_assets.c/.h and previews under tools/preview/.
"""
from pathlib import Path
from PIL import Image
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "main" / "assets"
PREVIEW = ROOT / "tools" / "preview"
VB_X, VB_Y, VB = -20, -30, 240  # padded canvas in design units

ACCENTS = {
    # name: (accent, trail opacity, jet opacity, dim body)
    "cyan": ("#5CE8F0", 1.0, 0.8, False),
    "amber": ("#FFC069", 0.6, 0.8, False),
    "coral": ("#FF7A6B", 0.45, 0.12, False),
    "grey": ("#3E484C", 0.2, 0.12, True),
}
SIZES = {"L": 240, "M": 180}  # on-screen orb size in px (design box 200)


def defs(acc, dim):
    mid, edge = ("#8E979A", "#596265") if dim else ("#EEF3F5", "#AFC0C7")
    return f"""<defs>
<radialGradient id="body" cx="0.36" cy="0.3" r="0.78">
<stop offset="0" stop-color="#FFFFFF"/><stop offset="0.55" stop-color="{mid}"/><stop offset="1" stop-color="{edge}"/></radialGradient>
<radialGradient id="visor" cx="0.5" cy="0.3" r="0.85">
<stop offset="0" stop-color="#262C31"/><stop offset="0.6" stop-color="#0A0C0E"/><stop offset="1" stop-color="#000000"/></radialGradient>
<linearGradient id="trail" x1="0" y1="1" x2="0" y2="0">
<stop offset="0" stop-color="{acc}" stop-opacity="0.95"/><stop offset="0.45" stop-color="{acc}" stop-opacity="0.55"/><stop offset="1" stop-color="{acc}" stop-opacity="0"/></linearGradient>
<filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
<feGaussianBlur stdDeviation="2.6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
</defs>"""


def body(acc, jet):
    return f"""
<g opacity="{jet}" filter="url(#glow)">
<ellipse cx="64" cy="166" rx="13" ry="7" transform="rotate(28 64 166)" fill="{acc}" opacity="0.75"/>
<ellipse cx="136" cy="166" rx="13" ry="7" transform="rotate(-28 136 166)" fill="{acc}" opacity="0.75"/></g>
<circle cx="100" cy="116" r="54" fill="url(#body)" stroke="#33454C" stroke-width="1.5"/>
<path d="M58 142 Q100 176 142 142" stroke="#FFFFFF" stroke-opacity="0.35" stroke-width="3" fill="none" stroke-linecap="round"/>
<ellipse cx="82" cy="70" rx="13" ry="4.5" transform="rotate(-18 82 70)" fill="#FFFFFF" opacity="0.85"/>
<rect x="55" y="76" width="90" height="64" rx="32" fill="url(#visor)" stroke="{acc}" stroke-width="3.5" filter="url(#glow)"/>
<rect x="55" y="76" width="90" height="64" rx="32" fill="none" stroke="{acc}" stroke-width="2"/>
<path d="M68 96 Q72 84 88 82" stroke="#FFFFFF" stroke-opacity="0.32" stroke-width="5" fill="none" stroke-linecap="round"/>
<circle cx="128" cy="86" r="3" fill="#FFFFFF" opacity="0.28"/>"""


def hand(acc, trail, side, dx=0, dy=0):
    x, rot = (62, -24) if side == "L" else (138, 24)
    return f"""<g transform="translate({dx} {dy})"><g transform="translate({x} 50) rotate({rot})">
<rect x="-7" y="-66" width="14" height="62" rx="7" fill="url(#trail)" opacity="{trail}"/>
<ellipse cx="0" cy="2" rx="12" ry="13" fill="url(#body)" stroke="#33454C" stroke-width="1.5"/>
<ellipse cx="0" cy="-3" rx="7.5" ry="6.5" fill="{acc}" opacity="0.9" filter="url(#glow)"/></g></g>"""


SPARKLES = """<path d="M168 70 L171 79 L180 82 L171 85 L168 94 L165 85 L156 82 L165 79 Z" fill="#5CE8F0"/>
<path d="M30 98 L32 104 L38 106 L32 108 L30 114 L28 108 L22 106 L28 104 Z" fill="#5CE8F0"/>"""
EYES_IDLE = """<g filter="url(#glow)"><rect x="83" y="93" width="10" height="28" rx="5" fill="#5CE8F0"/>
<rect x="107" y="93" width="10" height="28" rx="5" fill="#5CE8F0"/></g>"""
EYES_SLEEP = """<path d="M80 110 H95" stroke="#6B7477" stroke-width="5" stroke-linecap="round"/>
<path d="M105 110 H120" stroke="#6B7477" stroke-width="5" stroke-linecap="round"/>"""


def svg(inner, acc="#5CE8F0", dim=False, px=240):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{px}" height="{px}" '
            f'viewBox="{VB_X} {VB_Y} {VB} {VB}">{defs(acc, dim)}{inner}</svg>')


def layers():
    """Yields (name, inner svg, accent, dim, orb size px, crop)."""
    for size_name, size in SIZES.items():
        for name, (acc, trail, jet, dim) in ACCENTS.items():
            yield f"body_{name}_{size_name}", body(acc, jet), acc, dim, size, True
            yield f"hand_l_{name}_{size_name}", hand(acc, trail, "L"), acc, dim, size, True
            yield f"hand_r_{name}_{size_name}", hand(acc, trail, "R"), acc, dim, size, True
        yield f"sparkles_{size_name}", SPARKLES, "#5CE8F0", False, size, True
    full_idle = body("#5CE8F0", 0.8) + hand("#5CE8F0", 1, "L") + hand("#5CE8F0", 1, "R") + EYES_IDLE
    full_sleep = (body("#3E484C", 0.12) + hand("#3E484C", 0.2, "L", 3, 8) + hand("#3E484C", 0.2, "R", -3, 8)
                  + EYES_SLEEP)
    yield "icon_idle_64", full_idle, "#5CE8F0", False, 64, False
    yield "icon_idle_40", full_idle, "#5CE8F0", False, 40, False
    yield "icon_sleep_120", full_sleep, "#3E484C", True, 120, False


def render(page, markup, canvas_px):
    """Render at 3x and downsample for clean anti-aliased edges."""
    scale = 3
    page.set_viewport_size({"width": canvas_px, "height": canvas_px})
    page.set_content(f'<html><body style="margin:0;background:transparent">{markup}</body></html>')
    png = page.screenshot(omit_background=True, clip={"x": 0, "y": 0, "width": canvas_px, "height": canvas_px})
    from io import BytesIO
    img = Image.open(BytesIO(png)).convert("RGBA")
    return img.resize((canvas_px // scale, canvas_px // scale), Image.LANCZOS)


def to_rgb565a8(img):
    w, h = img.size
    rgb, alpha = bytearray(), bytearray()
    for r, g, b, a in img.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb += bytes((v & 0xFF, v >> 8))
        alpha.append(a)
    return bytes(rgb + alpha), w, h


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)
    entries = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(device_scale_factor=1)
        for name, inner, acc, dim, size, crop in layers():
            canvas = round(size * VB / 200)  # px for the padded canvas
            img = render(page, svg(inner, acc, dim, canvas * 3), canvas * 3)
            ox = oy = 0
            if crop:
                box = img.getchannel("A").point(lambda a: 255 if a > 2 else 0).getbbox()
                img = img.crop(box)
                ox, oy = box[0], box[1]
            else:
                # Static icon: crop to the orb's own 200x200 box, scaled to `size`.
                pad_x = round(-VB_X * size / 200)
                pad_y = round(-VB_Y * size / 200)
                img = img.crop((pad_x - size // 10, pad_y - size // 6, pad_x + size + size // 10, pad_y + size))
                ox, oy = pad_x - size // 10, pad_y - size // 6
            img.save(PREVIEW / f"{name}.png")
            data, w, h = to_rgb565a8(img)
            # Offsets are relative to the orb's 200-unit box origin, in px.
            off_x = ox - round(-VB_X * size / 200)
            off_y = oy - round(-VB_Y * size / 200)
            entries.append((name, data, w, h, off_x, off_y))
        browser.close()
    write_c(entries)


def write_c(entries):
    total = 0
    with open(OUT / "orb_assets.c", "w") as c, open(OUT / "orb_assets.h", "w") as hfile:
        hfile.write("#pragma once\n// Generated by tools/orb_assets.py. Do not edit.\n#include \"lvgl.h\"\n\n"
                    "typedef struct { const lv_image_dsc_t *img; int16_t x, y; } orb_part_t;\n\n")
        c.write("// Generated by tools/orb_assets.py from the original flying-orb design. Do not edit.\n"
                "#include \"orb_assets.h\"\n\n")
        for name, data, w, h, ox, oy in entries:
            total += len(data)
            c.write(f"static const uint8_t {name}_map[] = {{\n")
            for i in range(0, len(data), 24):
                c.write("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
            c.write("};\n")
            c.write(f"static const lv_image_dsc_t {name}_img = {{\n"
                    f"  .header.magic = LV_IMAGE_HEADER_MAGIC, .header.cf = LV_COLOR_FORMAT_RGB565A8,\n"
                    f"  .header.w = {w}, .header.h = {h}, .header.stride = {w * 2},\n"
                    f"  .data_size = sizeof({name}_map), .data = {name}_map,\n}};\n")
            c.write(f"const orb_part_t orb_{name} = {{&{name}_img, {ox}, {oy}}};\n\n")
            hfile.write(f"extern const orb_part_t orb_{name};  // {w}x{h} at ({ox},{oy})\n")
    print(f"{len(entries)} images, {total / 1024:.0f} KiB")


if __name__ == "__main__":
    main()
