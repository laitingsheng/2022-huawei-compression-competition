#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__all__ = []

import sys
import zipfile


def _main() -> int:
    with zipfile.ZipFile("task-laitingsheng-13631385096.zip", "w", zipfile.ZIP_LZMA) as file:
        file.write("build/task", "task")
    return 0


if __name__ == "__main__":
    sys.exit(_main())
