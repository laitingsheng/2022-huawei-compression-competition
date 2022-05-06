#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__all__ = []

import sys

import numpy as np


def _main() -> int:
    data = np.loadtxt("data/shore_public.dat", dtype=np.float64, comments=None, delimiter=" ")
    print(data.shape)
    return 0


if __name__ == "__main__":
    sys.exit(_main())
