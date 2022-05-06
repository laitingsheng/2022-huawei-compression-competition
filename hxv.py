#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__all__ = []

import lzma
import sys
from pathlib import Path


def _main() -> int:
    # ss = []
    # nn = []
    # yyyy = []
    # hhh = []
    # nnn = []
    # www = []
    # ppp = []
    with open("data/data_well_public.hxv", "r", encoding="utf-8") as f:
        lines = [
            int("".join(line.split(",")), 16)
            for line in f.read().splitlines()
        ]
        # for line in f.read().splitlines():
        #     line = "".join(line.split(","))

            # ss.append(int(line[0:2], 16))
            # nn.append(int(line[2:4], 16))
            # yyyy.append(int(line[4:8], 16))
            # hhh.append(int(line[8:11], 16))
            # nnn.append(int(line[11:14], 16))
            # www.append(int(line[14:17], 16))
            # ppp.append(int(line[17:20], 16))

    # compressed_ss = lzma.compress(b"".join(cell.to_bytes(2, "little") for cell in ss), lzma.FORMAT_ALONE)
    # print(len(compressed_ss))

    # compressed_nn = lzma.compress(b"".join(cell.to_bytes(2, "little") for cell in nn), lzma.FORMAT_ALONE)
    # print(len(compressed_nn))

    # compressed_yyyy = lzma.compress(b"".join(cell.to_bytes(4, "little") for cell in yyyy), lzma.FORMAT_ALONE)
    # print(len(compressed_yyyy))

    # compressed_hhh = lzma.compress(b"".join(cell.to_bytes(3, "little") for cell in hhh), lzma.FORMAT_ALONE)
    # print(len(compressed_hhh))

    # compressed_nnn = lzma.compress(b"".join(cell.to_bytes(3, "little") for cell in nnn), lzma.FORMAT_ALONE)
    # print(len(compressed_nnn))

    # compressed_www = lzma.compress(b"".join(cell.to_bytes(3, "little") for cell in www), lzma.FORMAT_ALONE)
    # print(len(compressed_www))

    # compressed_ppp = lzma.compress(b"".join(cell.to_bytes(3, "little") for cell in ppp), lzma.FORMAT_ALONE)
    # print(len(compressed_ppp))

    compressed_line = lzma.compress(b"".join(line.to_bytes(20, "little") for line in lines), lzma.FORMAT_ALONE)
    print(len(compressed_line))

    size = Path("data/data_well_public.hxv").stat().st_size

    # print(size / (len(compressed_ss) + len(compressed_nn) + len(compressed_yyyy) + len(compressed_hhh) + len(compressed_nnn) + len(compressed_www) + len(compressed_ppp) + 56))
    print(size / len(compressed_line))

    return 0


if __name__ == "__main__":
    sys.exit(_main())
