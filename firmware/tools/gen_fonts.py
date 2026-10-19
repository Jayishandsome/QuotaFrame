"""Generate the LVGL fonts used by the voice-remote UI.

Needs: python fonttools, `npx lv_font_conv@1.5.3`, and the OFL fonts
NotoSansTC[wght].ttf and SpaceGrotesk[wght].ttf from github.com/google/fonts
in a folder given as the first argument.
Fonts are uncompressed so they work without CONFIG_LV_USE_FONT_COMPRESSED.

* font_text_22: Traditional Chinese (Big5 level 1, ~5,400 common characters),
  extra written-Cantonese characters, ASCII and punctuation. Used for the
  recognised command and the history list, whose text is not known in advance.
* font_ui_16/20/28: only the characters that appear in main/ui.c strings.
* font_num_16/64: Space Grotesk digits and Latin letters for status and clock.
"""
import re
import subprocess
import sys
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "main" / "fonts"
CANTONESE = (
    "嘅咗喺啲嘢冇佢哋唔嚟睇咁噉乜點解邊度幾時呢嗰晒啱搵揾俾畀攞嘥諗嘈攰瞓食飲傾講話咪嘞喇囉咩嘛啩喎嚿蚊嗌揸"
    "嬲揼撳嘟靚呀啦㗎嚹喂嗱嘩哎吖嘛嗯咦唓啫咋嘅噃喳嗦嚡揞撈抌掟㩒踎瞓擘攝搞掂匿氹咻嚇嚫嗰嗮"
    "諗住同埋重未得閒唞休息叫幫我你佢哋整理郵件電郵會議提醒聽日今日琴日朝早夜晚星期")
PUNCT = "，。、！？：；「」『』（）《》〈〉…—–·．～％＋－＝／＠＃＆＊“”‘’"


def big5_level1():
    chars = []
    for lead in range(0xA4, 0xC7):
        for trail in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
            if lead == 0xC6 and trail > 0x7E:
                break
            try:
                chars.append(bytes((lead, trail)).decode("big5"))
            except UnicodeDecodeError:
                pass
    return chars


def ui_strings():
    text = (ROOT / "main" / "ui.c").read_text(encoding="utf-8")
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))


def instance(src, weight, dest):
    font = TTFont(src)
    if "fvar" in font:
        font = instancer.instantiateVariableFont(font, {"wght": weight})
    font.save(dest)
    return dest


def convert(ttf, size, symbols, name, bpp=4):
    cmap = TTFont(ttf).getBestCmap()
    keep = "".join(sorted({c for c in symbols if ord(c) in cmap and c not in "\n\r\t"}))
    subprocess.run(["npx", "--yes", "lv_font_conv@1.5.3", "--font", str(ttf), "--size", str(size),
                    "--bpp", str(bpp), "--no-compress", "--format", "lvgl", "--lv-include", "lvgl.h",
                    "--symbols", keep, "-o", str(OUT / f"{name}.c"), "--lv-font-name", name],
                   check=True)
    print(f"{name}: {len(keep)} glyphs")


def main():
    src = Path(sys.argv[1])
    work = Path(sys.argv[2]) if len(sys.argv) > 2 else src
    OUT.mkdir(parents=True, exist_ok=True)
    tc400 = instance(src / "NotoSansTC.ttf", 400, work / "NotoSansTC-400.ttf")
    tc500 = instance(src / "NotoSansTC.ttf", 500, work / "NotoSansTC-500.ttf")
    tc700 = instance(src / "NotoSansTC.ttf", 700, work / "NotoSansTC-700.ttf")
    sg500 = instance(src / "SpaceGrotesk.ttf", 500, work / "SpaceGrotesk-500.ttf")
    sg400 = instance(src / "SpaceGrotesk.ttf", 400, work / "SpaceGrotesk-400.ttf")
    ascii_ = "".join(chr(c) for c in range(0x20, 0x7F))
    ui = ui_strings() + ascii_
    if "--static-only" not in sys.argv:
        # LVGL glyph descriptors index bitmaps with 20 bits (1 MiB), so the
        # uncompressed Chinese set is split in two fonts joined by `fallback`.
        cjk = sorted(set("".join(big5_level1()) + CANTONESE + ui) - set(ascii_ + PUNCT))
        half = len(cjk) // 2
        convert(tc500, 22, ascii_ + PUNCT + "".join(cjk[:half]), "font_text_22")
        convert(tc500, 22, "".join(cjk[half:]), "font_text_22b")
        main_font = OUT / "font_text_22.c"
        text = main_font.read_text()
        assert text.count(".fallback = NULL") == 1
        text = text.replace(".fallback = NULL", ".fallback = &font_text_22b")
        text = text.replace("#ifndef FONT_TEXT_22\n", "LV_FONT_DECLARE(font_text_22b);\n\n#ifndef FONT_TEXT_22\n", 1)
        main_font.write_text(text)
    convert(tc400, 16, ui, "font_ui_16")
    convert(tc500, 20, ui, "font_ui_20")
    convert(tc700, 28, ui, "font_ui_28")
    convert(sg500, 16, ascii_, "font_num_16")
    convert(sg400, 64, "0123456789:", "font_num_64")


if __name__ == "__main__":
    main()
