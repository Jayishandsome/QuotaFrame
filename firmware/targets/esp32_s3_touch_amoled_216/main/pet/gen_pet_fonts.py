"""Generate the pet fonts from the strings in pet_overview.c and claude_prompt.c.

Usage: python3 gen_pet_fonts.py <pet folder> <folder with NotoSansTC-500.ttf and SpaceGrotesk-500.ttf>
Needs fonttools and lv_font_conv 1.5.x (npm).
"""
import re, subprocess, sys
from pathlib import Path
from fontTools.ttLib import TTFont
SRC = Path(sys.argv[1]); FONTS = Path(sys.argv[2]); OUT = SRC / "fonts"
text = "".join((SRC / f).read_text(encoding="utf-8") for f in ("pet_overview.c", "claude_prompt.c"))
strings = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))
ascii_ = "".join(chr(c) for c in range(0x20, 0x7F))
ui = strings + ascii_ + "…（）"
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
