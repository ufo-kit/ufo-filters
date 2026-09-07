#!/usr/bin/env python3
"""Plot shape-only UFO/ASTRA benchmark summaries."""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
from typing import Any

from plot_results import ALGORITHM_LABELS, COLOR_CONFIG_PATH, load_colors


ALGORITHM_ORDER = (
    "general", "rgba_singular", "even_odd_dual", "astra_bp3d", "astra_accumulate",
)
SINGLE_VOLUME = ("general", "rgba_singular", "astra_bp3d", "astra_accumulate")
METRIC_LABELS = {
    "completion_time_ms": "Host-ready completion time (ms)",
    "peak_device_memory_mib": "Peak device memory (MiB)",
    "peak_device_memory_delta_mib": "Peak increase over baseline (MiB)",
}


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise FileNotFoundError(f"benchmark result is missing: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def save_figure(figure: Any, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path.with_suffix(".png"), dpi=180, bbox_inches="tight")
    obsolete = path.with_suffix(".pdf")
    if obsolete.exists():
        obsolete.unlink()


def timing_lookup(rows: list[dict[str, str]]) -> dict[tuple[int, str], tuple[float, float]]:
    return {
        (int(row["shape"]), row["algorithm"]):
        (float(row["median_ms"]), float(row["mad_ms"]))
        for row in rows if row["metric"] == "completion_time_ms"
    }


def memory_lookup(
    rows: list[dict[str, str]], metric: str
) -> dict[tuple[int, str], tuple[float, float]]:
    return {
        (int(row["shape"]), row["algorithm"]):
        (float(row["median_mib"]), float(row["mad_mib"]))
        for row in rows if row["metric"] == metric
    }


def present_algorithms(rows: list[dict[str, str]]) -> list[str]:
    available = {row["algorithm"] for row in rows}
    return [algorithm for algorithm in ALGORITHM_ORDER if algorithm in available]


def grouped_bars(
    plt: Any,
    np: Any,
    output: Path,
    shapes: list[int],
    algorithms: list[str],
    values: dict[tuple[int, str], tuple[float, float]],
    colors: dict[str, str],
    ylabel: str,
    title: str,
    filename: str,
) -> None:
    figure, axis = plt.subplots(figsize=(10.2, 5.8))
    x = np.arange(len(shapes), dtype=float)
    width = min(0.2, 0.82 / max(1, len(algorithms)))
    for index, algorithm in enumerate(algorithms):
        positions = x + (index - (len(algorithms) - 1) / 2.0) * width
        medians = []
        mads = []
        valid_positions = []
        for position, shape in zip(positions, shapes):
            value = values.get((shape, algorithm))
            if value is not None:
                valid_positions.append(position)
                medians.append(value[0])
                mads.append(value[1])
        if medians:
            axis.bar(
                valid_positions, medians, width, yerr=mads, capsize=3,
                color=colors[algorithm], edgecolor="black", linewidth=0.4,
                label=ALGORITHM_LABELS.get(algorithm, algorithm),
            )
    axis.set_xticks(x, [f"{shape}³" for shape in shapes])
    axis.set_xlabel("Reconstructed output shape")
    axis.set_ylabel(ylabel)
    axis.set_title(title)
    axis.grid(axis="y", alpha=0.22)
    axis.legend(frameon=False)
    figure.tight_layout()
    save_figure(figure, output / filename)
    plt.close(figure)


def scaling_lines(
    plt: Any,
    output: Path,
    shapes: list[int],
    algorithms: list[str],
    values: dict[tuple[int, str], tuple[float, float]],
    colors: dict[str, str],
) -> None:
    figure, axis = plt.subplots(figsize=(9.4, 5.8))
    for algorithm in algorithms:
        available = [
            (shape, values[(shape, algorithm)])
            for shape in shapes if (shape, algorithm) in values
        ]
        if not available:
            continue
        x = [item[0] for item in available]
        medians = [item[1][0] for item in available]
        mads = [item[1][1] for item in available]
        axis.errorbar(
            x, medians, yerr=mads, marker="o", capsize=3,
            color=colors[algorithm], label=ALGORITHM_LABELS.get(algorithm, algorithm),
        )
    axis.set_xscale("log", base=2)
    axis.set_yscale("log")
    axis.set_xticks(shapes, [str(shape) for shape in shapes])
    axis.set_xlabel("Cubic output side")
    axis.set_ylabel(METRIC_LABELS["completion_time_ms"])
    axis.set_title("Full-detector completion-time scaling")
    axis.grid(which="both", alpha=0.22)
    axis.legend(frameon=False)
    figure.tight_layout()
    save_figure(figure, output / "completion-scaling")
    plt.close(figure)


def generate_plots(campaign: Path) -> None:
    os.environ.setdefault("MPLCONFIGDIR", str(campaign / ".matplotlib"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import is_color_like

    timing_rows = read_csv(campaign / "results" / "summaries.csv")
    memory_path = campaign / "results" / "memory-summaries.csv"
    memory_rows = read_csv(memory_path) if memory_path.is_file() else []
    if not timing_rows:
        raise RuntimeError("no complete algorithm/shape results are available to plot")
    colors, _ = load_colors(COLOR_CONFIG_PATH, is_color_like)
    algorithms = present_algorithms(timing_rows)
    missing_colors = set(algorithms).difference(colors)
    if missing_colors:
        raise ValueError(f"colors.example.json is missing {sorted(missing_colors)}")
    shapes = sorted({int(row["shape"]) for row in timing_rows})
    timing = timing_lookup(timing_rows)
    output = campaign / "plots"

    try:
        plt.style.use("seaborn-v0_8-whitegrid")
    except OSError:
        plt.style.use("default")
    singles = [algorithm for algorithm in SINGLE_VOLUME if algorithm in algorithms]
    grouped_bars(
        plt, np, output, shapes, singles, timing, colors,
        METRIC_LABELS["completion_time_ms"],
        "Matched single-volume reconstruction", "single-volume-completion",
    )
    grouped_bars(
        plt, np, output, shapes, algorithms, timing, colors,
        METRIC_LABELS["completion_time_ms"],
        "Full-detector reconstruction (RGBA dual produces two volumes)",
        "all-workloads-completion",
    )
    scaling_lines(plt, output, shapes, algorithms, timing, colors)

    for metric, filename, title in (
        ("peak_device_memory_mib", "peak-device-memory", "Absolute peak device memory"),
        ("peak_device_memory_delta_mib", "peak-device-memory-delta",
         "Incremental peak device memory"),
    ):
        values = memory_lookup(memory_rows, metric)
        if values:
            grouped_bars(
                plt, np, output, shapes, algorithms, values, colors,
                METRIC_LABELS[metric], title, filename,
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    args = parser.parse_args()
    generate_plots(args.campaign.expanduser().resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
