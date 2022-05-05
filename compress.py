#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__all__ = []

import sys
import zipfile
from typing import List


def _main(args: List[str]) -> int:
    with zipfile.ZipFile(f"task-{args[1]}-{args[2]}.zip", "w", zipfile.ZIP_LZMA) as file:
        file.write("build/task", "task")
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
