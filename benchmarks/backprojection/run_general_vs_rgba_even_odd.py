#!/usr/bin/env python3
"""Benchmark one General volume against two RGBA even/odd volumes."""

from benchmark_common import runner_main


if __name__ == "__main__":
    raise SystemExit(runner_main("general-vs-rgba-even-odd", __doc__))
