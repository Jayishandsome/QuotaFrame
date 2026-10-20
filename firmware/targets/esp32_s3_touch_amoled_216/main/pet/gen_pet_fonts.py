"""Generate the pet overview fonts from the strings in pet_overview.c."""
import re, subprocess, sys
from pathlib import Path
from fontTools.ttLib import TTFont
SRC = Path(sys.argv[1]); FONTS = Path(sys.argv[2]); OUT = SRC.parent / "fonts"
text = SRC.read_text(encoding="utf-8")
strings = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))
ascii_ = "".join(chr(c) for c in range(0x20, 0x7F))
ui = strings + ascii_ + "…"
def convert(ttf, size, symbols, name, bpp=4):
    cmap = TTFont(ttf).getBestCmap()
    keep = "".join(sorted({c for c in symbols if ord(c) in cmap and c not in "\n\r\t"}))
    subprocess.run(["lv_font_conv", "--font", str(ttf), "--size", str(size), "--bpp", str(bpp),
                    "--no-compress", "--format", "lvgl", "--lv-include", "lvgl.h",
                    "--symbols=" + keep, "-o", str(OUT / f"{name}.c"), "--lv-font-name", name], check=True)
    print(name, len(keep), "glyphs")
convert(FONTS / "NotoSansTC-500.ttf", 16, ui, "pet_ui_16")
convert(FONTS / "NotoSansTC-500.ttf", 20, ui, "pet_ui_20")
convert(FONTS / "SpaceGrotesk-500.ttf", 44, "0123456789%-", "pet_num_44")
