#!/usr/bin/env python3
"""Benchmark RGBA parity-aware single and subset-specific dual launches."""

from benchmark_common import runner_main


if __name__ == "__main__":
    raise SystemExit(runner_main("rgba-even-odd", __doc__))
