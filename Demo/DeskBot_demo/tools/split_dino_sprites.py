"""把 Chromium 的 T-Rex 精灵图切成 LVGL 资源 C 文件。

用法: python gen_dino_c.py <输出目录>
输出: <输出目录>/ui_img_dino_sprites.c / .h
坐标来源: dino_game/offline_sprite_definitions.ts(ldpi) + trex.ts +
          distance_meter.ts + game_over_panel.ts + night_mode.ts
"""
import os, sys
from PIL import Image

SHEET = ("/home/mumu/dino_source/chromium-dino/components/neterror/resources/images/"
         "default_100_percent/offline/100-offline-sprite.png")

CROPS = []
TREX_X, TREX_Y, RW, RH = 848, 2, 44, 47
for off, name in [(0, "dino_stand"), (44, "dino_blink"), (88, "dino_run1"),
                  (132, "dino_run2"), (220, "dino_crash")]:
    CROPS.append((name, TREX_X + off, TREX_Y, RW, RH))
for off, name in [(264, "dino_duck1"), (323, "dino_duck2")]:
    CROPS.append((name, TREX_X + off, TREX_Y, 59, RH))
for base, nm, w, h in [(228, "cactus_small", 17, 35), (332, "cactus_large", 25, 50)]:
    for size in (1, 2, 3):
        sx = int((w * size) * (0.5 * (size - 1))) + base
        CROPS.append((f"{nm}_{size}", sx, 2, w * size, h))
for f in (0, 1):
    CROPS.append((f"ptero_{f}", 134 + 46 * f, 2, 46, 40))
CROPS.append(("cloud", 86, 2, 46, 14))
for i in (0, 1):
    CROPS.append((f"horizon_{i}", 2 + 600 * i, 54, 600, 12))
for f in range(8):
    CROPS.append((f"restart_{f}", 2 + 36 * f, 68, 36, 32))
for i, ch in enumerate("0123456789HI"):
    CROPS.append((f"glyph_{ch}", 655 + 10 * i, 2, 10, 13))
CROPS.append(("text_gameover", 655, 15, 191, 11))
for p in range(4):
    CROPS.append((f"moon_{p}", 484 + 20 * p, 2, 20, 40))
for s in range(2):
    CROPS.append((f"star_{s}", 645, 2 + 9 * s, 9, 9))

out_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/dino/out"
os.makedirs(out_dir, exist_ok=True)
sheet = Image.open(SHEET).convert("RGBA")


def _prep(im):
    """把原版的 1px 白色光晕转成透明: 浅底不可见, 夜间 recolor 后等价于反色的黑色光晕。"""
    px = list(im.getdata())
    out = [(r, g, b, 0) if (r >= 250 and g >= 250 and b >= 250) else (r, g, b, a)
           for (r, g, b, a) in px]
    im.putdata(out)
    return im

c = ["""/* 自动生成, 请勿手改.
 * 来源: Chromium components/neterror/resources/images/default_100_percent/offline/100-offline-sprite.png
 * 生成脚本: tools/split_dino_sprites.py
 */
#include "../ui.h"
"""]
h = ["""/* 自动生成, 请勿手改: T-Rex 原版精灵资源 */
#ifndef _UI_IMG_DINO_SPRITES_H
#define _UI_IMG_DINO_SPRITES_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../ui.h"
"""]
total = 0
for name, x, y, w, hgt in CROPS:
    im = _prep(sheet.crop((x, y, x + w, y + hgt)))
    px = list(im.getdata())
    total += w * hgt * 4
    c.append(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t dino_sp_{name}_map[] = {{")
    line = []
    for r, g, b, a in px:
        line.append(f"0x{r:02X},0x{g:02X},0x{b:02X},0x{a:02X},")
        if len(line) == 16:
            c.append("".join(line)); line = []
    if line:
        c.append("".join(line))
    c.append("};")
    c.append(f"""const lv_image_dsc_t dino_sp_{name} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_ARGB8888,
    .header.flags = 0,
    .header.w = {w},
    .header.h = {hgt},
    .header.stride = {w} * 4,
    .data_size = sizeof(dino_sp_{name}_map),
    .data = dino_sp_{name}_map,
}};
""")
    h.append(f"LV_IMG_DECLARE(dino_sp_{name});")
h += ["#ifdef __cplusplus", "}", "#endif", "#endif"]
open(f"{out_dir}/ui_img_dino_sprites.c", "w").write("\n".join(c) + "\n")
open(f"{out_dir}/ui_img_dino_sprites.h", "w").write("\n".join(h) + "\n")
print(f"{len(CROPS)} 张精灵, 像素数据 {total/1024:.0f} KiB -> {out_dir}")
