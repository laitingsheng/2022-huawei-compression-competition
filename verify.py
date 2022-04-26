#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__all__ = []

import filecmp
import subprocess
import sys
import time
from argparse import ArgumentParser
from pathlib import Path


def _execute(*args) -> int:
    return subprocess.run(args, stdout=sys.stdout, stderr=sys.stderr, encoding="utf-8").returncode


def _main() -> int:
    parser = ArgumentParser()
    parser.add_argument(
        "-B", "--build",
        action="store_true",
        dest="build"
    )
    parser.add_argument(
        "-f", "--file",
        type=Path,
        required=True,
        dest="file"
    )
    parser.add_argument(
        "-G", "--generate",
        action="store_true",
        dest="generate"
    )

    args = parser.parse_args()

    if args.generate:
        if r := _execute("cmake", "-G", "Ninja", "-B", "build", ".", "-DCMAKE_BUILD_TYPE=Release"):
            print("Failed to generate build files.", file=sys.stderr)
            return r

    if args.build:
        if r := _execute("cmake", "--build", "build", "--target", "main"):
            print("Failed to build the executable.", file=sys.stderr)
            return r

    file = args.file
    compressed_file = file.with_suffix(".compressed")
    decompressed_file = file.with_suffix(".decompressed")

    now = time.monotonic()
    if r := _execute("build/main", "c", file, compressed_file):
        print("Failed to compress the file.", file=sys.stderr)
        return r
    compress_time = time.monotonic() - now
    print(f"Compression took {compress_time:.3f}s")
    print(f"Compression throughput: {file.stat().st_size / compress_time / (2 << 20):.3f}MB/s")
    print(f"Ratio: {file.stat().st_size / compressed_file.stat().st_size: .3f}")

    now = time.monotonic()
    if r := _execute("build/main", "d", compressed_file, decompressed_file):
        print("Failed to decompress the compressed file.", file=sys.stderr)
        return r
    decompress_time = time.monotonic() - now
    print(f"Decompression took {decompress_time:.3f}s")
    print(f"Decompression throughput: {file.stat().st_size / decompress_time / (2 << 20):.3f}MB/s")

    if not filecmp.cmp(args.file, decompressed_file, shallow=False):
        print("The decompressed file is not identical to the original file.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(_main())
