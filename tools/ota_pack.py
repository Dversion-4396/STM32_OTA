# -*- coding: utf-8 -*-
"""
ota_pack.py —— 给 Keil 编译出来的 bin 追加 28 字节 OTA 尾缀。

尾缀格式（共 28 字节，来自教程《21.OTA升级.pdf》与 36 号例程的 工程文件.bin 实测）:

    +0   CD EF CD EF  4 字节  分隔标记（十六进制字节，不是 ASCII 字符串 "cdefcdef"！）
    +4   len          4 字节  原始 bin 长度，小端
    +8   md5         16 字节  原始 bin 的 MD5（16 字节裸摘要，不是 32 位 hex 字符串）
    +24  FF FF FF FF  4 字节  结尾标记（0xFF x4）

坑点：教程 PDF 里把分隔标记写成 "cdefcdef"，很容易误读成 8 个 ASCII 字符。
    实测 36 号例程的 工程文件.bin，尾缀 28 字节是 CD EF CD EF ...，即 4 字节十六进制值。
    写成 ASCII 会让尾缀变成 32 字节，格式全错。

注意：
  * len 和 md5 都只针对 **原始 bin**，不包含这 28 字节本身。
  * STM32 端 BootLoader 按原始长度搬移，所以尾缀只是给云端/校验用的元数据。
  * 追加后文件长度 = 原始长度 + 28。

用法:
    python ota_pack.py 工程文件.bin                 # 生成 工程文件_ota.bin
    python ota_pack.py 工程文件.bin 输出.bin
    python ota_pack.py --verify 工程文件_ota.bin     # 反向校验尾缀
"""
import hashlib
import re
import struct
import sys
import os

MAGIC_HEAD = bytes.fromhex("CDEFCDEF")
MAGIC_TAIL = b"\xff\xff\xff\xff"
FOOTER_LEN = 28


def build_footer(raw: bytes) -> bytes:
    """按规范生成 28 字节尾缀。"""
    return (
        MAGIC_HEAD
        + struct.pack("<I", len(raw))
        + hashlib.md5(raw).digest()
        + MAGIC_TAIL
    )


# 版本字符串匹配：X.Y.Z 后跟 '\0'（C 字符串常量的形状）
VER_RE = re.compile(rb"(?<![0-9.])(\d{1,3}\.\d{1,3}\.\d{1,3})\x00")


def find_version(raw: bytes):
    """从镜像里把版本号找出来。

    为什么从镜像里读而不是让人手填：
      版本号在整条链路里只应该有一处定义（main.c 的 FW_VERSION_STR），
      镜像里就带着这份字符串。打包时直接读出来拼通知，
      人就不用在源码 / 控制台 / 通知 三个地方各敲一遍。
    """
    hits = VER_RE.findall(raw)
    return sorted({h.decode() for h in hits})


def pick_version(vers):
    """从候选里挑一个。

    为什么会有多个：
      镜像里除了固件自带的 FW_VERSION_STR，
      还有 eeprom.c 里“空片默认值 1.0.0”这类字符串。
    为什么取最大：
      版本号只增不减，默认值永远是最低的那个，
      取最大就能稳定挑到真正编译进去的版本。
    """
    if not vers:
        return None
    return max(vers, key=lambda v: tuple(int(x) for x in v.split(".")))


def pack(src: str, dst: str = None) -> str:
    raw = open(src, "rb").read()
    footer = build_footer(raw)
    assert len(footer) == FOOTER_LEN, len(footer)
    if dst is None:
        base, ext = os.path.splitext(src)
        dst = base + "_ota" + (ext or ".bin")
    open(dst, "wb").write(raw + footer)
    print(f"[pack] {src}")
    print(f"       原始长度 : {len(raw)}  (0x{len(raw):X})")
    print(f"       MD5      : {hashlib.md5(raw).hexdigest()}")
    print(f"       尾缀     : {footer.hex()}")
    print(f"       输出     : {dst}  ({len(raw) + FOOTER_LEN} 字节)")

    # ★版本号从镜像里取，人不手填★
    vers = find_version(raw)
    total = len(raw) + FOOTER_LEN
    pick = pick_version(vers)
    if pick is not None:
        extra = "" if len(vers) == 1 else f"   (候选 {vers}，取最大)"
        print(f"       版本号   : {pick}{extra}")
        print(f"       发送通知 : <链接>,{total},{pick}")
    elif not vers:
        print("       ★镜像里没找到 X.Y.Z 版本字符串，通知里的版本号需要手填")
        print(f"       长度记住是 : {total}")

    return dst


def verify(path: str) -> bool:
    data = open(path, "rb").read()
    if len(data) <= FOOTER_LEN:
        print("[verify] 文件太小，不含尾缀")
        return False
    raw, footer = data[:-FOOTER_LEN], data[-FOOTER_LEN:]
    ok = True

    def chk(name, got, want):
        nonlocal ok
        flag = "OK " if got == want else "BAD"
        if got != want:
            ok = False
        print(f"  [{flag}] {name}: {got}  (期望 {want})")

    chk("分隔标记", footer[0:4], MAGIC_HEAD)
    chk("长度字段", struct.unpack("<I", footer[4:8])[0], len(raw))
    chk("MD5 字段", footer[8:24], hashlib.md5(raw).digest())
    chk("结尾标记", footer[24:28], MAGIC_TAIL)
    print(f"[verify] {path} -> {'通过' if ok else '失败'}")
    return ok


if __name__ == "__main__":
    args = [a for a in sys.argv[1:]]
    if not args:
        print(__doc__)
        sys.exit(1)
    if args[0] == "--verify":
        sys.exit(0 if verify(args[1]) else 1)
    pack(args[0], args[1] if len(args) > 1 else None)
