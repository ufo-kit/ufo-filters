#!/usr/bin/env python3
"""Benchmark general-backproject against singular RGBA backprojection."""

from benchmark_common import runner_main


if __name__ == "__main__":
    raise SystemExit(runner_main("general-vs-rgba", __doc__))
