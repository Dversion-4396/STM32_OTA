# -*- coding: utf-8 -*-
r"""
把一张图片转成 STM32 能直接 include 的调色板 + 4bpp 索引头文件。

用法:
    python img2lcd.py [输入图片] [输出头文件] [宽] [高] [色数]

    python img2lcd.py
    python img2lcd.py "E:\AIwork\图片\素材-其他\给大肥鱼找的素材\大肥鱼.jpg" out.h 240 92 16

为什么用 4bpp 调色板而不是 RGB565:
    本工程用的是 MDK-ARM Lite，链接器硬卡 32KB 镜像上限。
    去掉图片后的固件已经 20820 字节，只剩 11948 字节可用。
    240x112 的 RGB565 要 53760 字节，根本装不下。
    4bpp（16 色）每像素半字节，240x92 只要 11040 字节 + 48 字节调色板。

原理:
    1. 等比缩放到刚好覆盖 目标宽x高（cover，不是 contain，不会留黑边）
    2. 居中裁剪到 目标宽x高
    3. MedianCut 量化成 色数 种颜色 + Floyd-Steinberg 抖动
    4. 每像素取 4 位索引，两个像素打包成一个字节（高半字节在前）
    5. 调色板单独一张 u16 表（RGB565）

注意:
    - 头文件带 UTF-8 BOM（armcc 按 GBK 解释无 BOM 的 UTF-8 会乱码）
    - 生成的都是 static const，放 Flash 不占 RAM
    - 宽高必须是偶数，否则 4bpp 打包会多出半字节
"""
import os
import sys
from PIL import Image

DEFAULT_SRC = r"E:\AIwork\图片\素材-其他\给大肥鱼找的素材\大肥鱼.jpg"
DEFAULT_OUT = r"D:\K_Work\Niu_Ota\STM32_OTA\APP工程\user\api\lcd_img.h"
DEFAULT_W, DEFAULT_H = 240, 92
DEFAULT_COLORS = 16


def rgb_to_565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    out = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    tw = int(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_W
    th = int(sys.argv[4]) if len(sys.argv) > 4 else DEFAULT_H
    ncol = int(sys.argv[5]) if len(sys.argv) > 5 else DEFAULT_COLORS

    if not os.path.isfile(src):
        print("找不到源图:", src)
        return 1
    if tw % 2 or th % 2:
        print("宽高必须是偶数（4bpp 两像素打包一字节）")
        return 1
    if ncol != 16:
        print("只支持 16 色（4bpp）。要别的色深得改打包和解码两边。")
        return 1

    im = Image.open(src).convert("RGB")
    w, h = im.size
    print("源图:", src, im.size)

    # ---- cover 缩放 + 居中裁剪 ----
    scale = max(tw / w, th / h)
    nw, nh = int(round(w * scale)), int(round(h * scale))
    r = im.resize((nw, nh), Image.LANCZOS)
    left = (nw - tw) // 2
    top = (nh - th) // 2
    c = r.crop((left, top, left + tw, top + th))
    print("缩放:", (nw, nh), "裁剪: left=%d top=%d ->" % (left, top), c.size)

    # ---- 量化成 16 色 ----
    q = c.quantize(colors=ncol, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    idx = list(q.get_flattened_data()) if hasattr(q, "get_flattened_data") else list(q.getdata())
    raw_pal = q.getpalette()[: ncol * 3]

    pal = []
    for i in range(ncol):
        pal.append(rgb_to_565(raw_pal[i * 3], raw_pal[i * 3 + 1], raw_pal[i * 3 + 2]))

    # ---- 4bpp 打包：两个像素一字节，高半字节在前 ----
    packed = []
    for i in range(0, len(idx), 2):
        hi = idx[i] & 0x0F
        lo = idx[i + 1] & 0x0F
        packed.append((hi << 4) | lo)
    assert len(packed) == tw * th // 2

    # ---- 输出 ----
    L = []
    L.append("#ifndef _LCD_IMG_H")
    L.append("#define _LCD_IMG_H")
    L.append("")
    L.append('#include "common.h"')
    L.append("")
    L.append("/*")
    L.append("\t启动画面图片 —— %dx%d，16 色调色板 + 4bpp 索引" % (tw, th))
    L.append("")
    L.append("\t来源：%s" % os.path.basename(src))
    L.append("\t调色板：%d 个 RGB565 值 = %d 字节" % (ncol, ncol * 2))
    L.append("\t索引：%d x %d / 2 = %d 字节（每字节两个像素，高半字节是左边那个）" % (tw, th, len(packed)))
    L.append("\t合计：%d 字节 Flash" % (ncol * 2 + len(packed)))
    L.append("")
    L.append("\t★ 为什么不用 RGB565：本工程是 MDK-ARM Lite，链接器卡 32KB 镜像上限，")
    L.append("\t  去掉图片后固件已 20820 字节，只剩 11948 字节。RGB565 要 53760 字节装不下。")
    L.append("")
    L.append("\t★ 生成脚本：tools\\img2lcd.py")
    L.append("\t★ 想换图就改脚本里的 SRC 路径重跑，不要手改这个文件。")
    L.append("*/")
    L.append("")
    L.append("#define LCD_IMG_W  %d" % tw)
    L.append("#define LCD_IMG_H  %d" % th)
    L.append("")
    L.append("/* 调色板：RGB565 */")
    L.append("static const u16 lcd_img_pal[%d] = {" % ncol)
    for i in range(0, len(pal), 8):
        L.append("    " + " ".join("0x%04X," % v for v in pal[i:i + 8]))
    L.append("};")
    L.append("")
    L.append("/* 像素索引：4bpp，行主序，每字节两个像素（高半字节在前） */")
    L.append("static const u8 lcd_img_data[%d] = {" % len(packed))
    for i in range(0, len(packed), 16):
        L.append("    " + " ".join("0x%02X," % v for v in packed[i:i + 16]))
    L.append("};")
    L.append("")
    L.append("#endif")
    L.append("")

    txt = "\n".join(L)
    with open(out, "wb") as f:
        f.write(b"\xef\xbb\xbf")           # UTF-8 BOM，armcc 需要
        f.write(txt.encode("utf-8"))
    print("写出:", out)
    print("  像素 %d, 索引 %d 字节, 调色板 %d 字节, 合计 %d 字节, 文件 %d 字节"
          % (tw * th, len(packed), ncol * 2, len(packed) + ncol * 2, len(txt.encode("utf-8")) + 3))

    # 顺手存一张预览图，方便肉眼确认量化效果
    prev = os.path.join(r"D:\K_Work\Niu_Ota\STM32_OTA_检查报告_20261030\_验证工具", "_图片预览_" + os.path.basename(os.path.splitext(out)[0]) + ".png")
    q.convert("RGB").save(prev)
    print("  预览:", prev)
    return 0


if __name__ == "__main__":
    sys.exit(main())