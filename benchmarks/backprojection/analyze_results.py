#!/usr/bin/env python3
"""Rebuild CSV summaries and optionally plots for an existing campaign."""

from __future__ import annotations

import argparse
from pathlib import Path

from benchmark_common import aggregate_results
from plot_results import generate_plots


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--no-plots", action="store_true")
    args = parser.parse_args()
    campaign = args.campaign.expanduser().resolve()
    summary = aggregate_results(campaign)
    print(summary)
    if not args.no_plots:
        generate_plots(campaign)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
