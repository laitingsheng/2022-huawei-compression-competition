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
        "-f", "--file", "--files",
        action="extend",
        nargs="+",
        type=Path,
        dest="files"
    )
    parser.add_argument(
        "-G", "--generate",
        action="store_true",
        dest="generate"
    )

    args = parser.parse_args()

    if args.generate:
        if r := _execute("cmake", "--toolchain", "toolchains/gnu/host.cmake", "-G", "Ninja", "-B", "build", "."):
            print("Failed to generate build files.", file=sys.stderr)
            return r

    if args.build:
        if r := _execute("cmake", "--build", "build", "--target", "task"):
            print("Failed to build the executable.", file=sys.stderr)
            return r

    total_raw_size = 0
    total_compressed_size = 0
    total_compress_time = 0
    total_decompress_time = 0

    for file in (args.files or ()):
        print(f"{file}:")

        compressed_file = file.with_suffix(file.suffix + ".compressed")
        compressed_file.unlink(True)

        decompressed_file = file.with_suffix(file.suffix + ".decompressed")
        decompressed_file.unlink(True)

        now = time.monotonic()
        if r := _execute("build/task", "c", file, compressed_file):
            print("Failed to compress the file.", file=sys.stderr)
            return r
        compress_time = time.monotonic() - now
        print(f"    Compression took {compress_time:.3f}s")
        print(f"    Compression throughput: {file.stat().st_size / compress_time / (1 << 20):.3f}MB/s")
        print(f"    Ratio: {file.stat().st_size / compressed_file.stat().st_size: .3f}")

        total_raw_size += file.stat().st_size
        total_compressed_size += compressed_file.stat().st_size
        total_compress_time += compress_time

        now = time.monotonic()
        if r := _execute("build/task", "d", compressed_file, decompressed_file):
            print("Failed to decompress the compressed file.", file=sys.stderr)
            return r
        decompress_time = time.monotonic() - now
        print(f"    Decompression took {decompress_time:.3f}s")
        print(f"    Decompression throughput: {file.stat().st_size / decompress_time / (1 << 20):.3f}MB/s")

        total_decompress_time += decompress_time

        if not filecmp.cmp(file, decompressed_file, shallow=False):
            print("The decompressed file is not identical to the original file.", file=sys.stderr)
            return 1

        print()

    if args.files:
        print("Overall:")
        print(f"    Compression throughput: {total_raw_size / total_compress_time / (1 << 20):.3f}MB/s")
        print(f"    Decompression throughput: {total_raw_size / total_decompress_time / (1 << 20):.3f}MB/s")
        print(f"    Ratio: {total_raw_size / total_compressed_size: .3f}")

    return 0


if __name__ == "__main__":
    sys.exit(_main())
