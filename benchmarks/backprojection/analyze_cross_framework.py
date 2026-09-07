#!/usr/bin/env python3
"""Rebuild summaries and plots for a UFO/ASTRA benchmark campaign."""

from __future__ import annotations

import argparse
from pathlib import Path

from cross_framework_benchmark import aggregate_results
from plot_cross_framework import generate_plots


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--no-plots", action="store_true")
    args = parser.parse_args()
    campaign = args.campaign.expanduser().resolve()
    print(aggregate_results(campaign))
    if not args.no_plots:
        generate_plots(campaign)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
