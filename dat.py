#!/usr/bin/env python3
# -*- coding: utf-8 -*-
__all__ = []

import sys

import numpy as np


def _consecutive(data, stepsize: float = 0):
    print(np.split(data, np.where(~np.isclose(np.diff(data), stepsize))[0] + 1))


def _main() -> int:
    data = np.loadtxt("data/shore_public.dat", dtype=np.float64, comments=None, delimiter=" ")
    _consecutive(data[:, 0].flatten(), 0.04)
    _consecutive(data[:, 57].flatten(), 0)
    _consecutive(data[:, 59].flatten(), 0)
    _consecutive(data[:, 70].flatten(), 0)
    return 0


if __name__ == "__main__":
    sys.exit(_main())
