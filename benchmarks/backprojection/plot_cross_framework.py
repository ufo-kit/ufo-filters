#!/usr/bin/env python3
"""Plot UFO/ASTRA experimental benchmark summaries."""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
from typing import Any

from plot_results import ALGORITHM_LABELS, COLOR_CONFIG_PATH, load_colors


ALGORITHM_ORDER = ("general", "rgba_singular", "even_odd", "astra_accumulate")
SINGLE_VOLUME = ("general", "rgba_singular", "astra_accumulate")


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


def lookup(
    rows: list[dict[str, str]], metric: str, value_key: str, spread_key: str
) -> dict[tuple[int, int, str], tuple[float, float]]:
    return {
        (int(row["shape"]), int(row["burst"]), row["algorithm"]):
        (float(row[value_key]), float(row[spread_key]))
        for row in rows if row["metric"] == metric
    }


def grouped_by_shape(
    plt: Any, np: Any, output: Path, shapes: list[int], bursts: list[int],
    algorithms: list[str], values: dict[tuple[int, int, str], tuple[float, float]],
    colors: dict[str, str], ylabel: str, title: str, filename: str,
) -> None:
    width = min(0.22, 0.82 / max(1, len(algorithms)))
    for shape in shapes:
        figure, axis = plt.subplots(figsize=(9.5, 5.6))
        x = np.arange(len(bursts), dtype=float)
        for index, algorithm in enumerate(algorithms):
            positions = x + (index - (len(algorithms) - 1) / 2.0) * width
            points = [values.get((shape, burst, algorithm)) for burst in bursts]
            valid = [(position, point) for position, point in zip(positions, points)
                     if point is not None]
            if not valid:
                continue
            axis.bar(
                [item[0] for item in valid], [item[1][0] for item in valid], width,
                yerr=[item[1][1] for item in valid], capsize=3,
                color=colors[algorithm], edgecolor="black", linewidth=0.4,
                label=ALGORITHM_LABELS.get(algorithm, algorithm),
            )
        axis.set_xticks(x, [str(value) for value in bursts])
        axis.set_xlabel("Burst size")
        axis.set_ylabel(ylabel)
        axis.set_title(f"{title} — {shape}³ output")
        axis.grid(axis="y", alpha=0.22)
        axis.legend(frameon=False)
        figure.tight_layout()
        save_figure(figure, output / f"{filename}-n{shape}")
        plt.close(figure)


def scaling(
    plt: Any, np: Any, output: Path, shapes: list[int], bursts: list[int],
    algorithms: list[str], values: dict[tuple[int, int, str], tuple[float, float]],
    colors: dict[str, str], orientation: str,
) -> None:
    if orientation == "row":
        rows, columns = 1, len(bursts)
        figsize = (5.0 * columns, 4.8)
    else:
        rows, columns = len(bursts), 1
        figsize = (7.0, 4.2 * rows)
    figure, axes = plt.subplots(rows, columns, figsize=figsize, squeeze=False)
    for axis, burst in zip(axes.flat, bursts):
        for algorithm in algorithms:
            points = [(shape, values.get((shape, burst, algorithm))) for shape in shapes]
            valid = [item for item in points if item[1] is not None and item[1][0] > 0]
            if not valid:
                continue
            axis.errorbar(
                np.array([item[0] for item in valid]),
                np.array([item[1][0] for item in valid]),
                yerr=np.array([item[1][1] for item in valid]), marker="o", capsize=3,
                color=colors[algorithm], label=ALGORITHM_LABELS.get(algorithm, algorithm),
            )
        axis.set_xscale("log", base=2)
        axis.set_yscale("log")
        axis.set_xticks(shapes, [str(shape) for shape in shapes])
        axis.set_title(f"Burst {burst}")
        axis.set_xlabel("Cubic output side")
        axis.set_ylabel("Host-ready completion time (ms)")
        axis.grid(which="both", alpha=0.22)
    handles, labels = axes.flat[0].get_legend_handles_labels()
    figure.suptitle("UFO–ASTRA experimental completion scaling", y=0.995)
    if handles:
        figure.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.5, 0.955),
                      ncol=len(algorithms), frameon=False)
    figure.tight_layout(rect=(0.0, 0.0, 1.0, 0.90))
    save_figure(figure, output / "completion-scaling")
    plt.close(figure)


def generate_plots(campaign: Path) -> None:
    os.environ.setdefault("MPLCONFIGDIR", str(campaign / ".matplotlib"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import is_color_like

    config = json.loads((campaign / "resolved-config.json").read_text(encoding="utf-8"))
    orientation = config.get("scaling_plot_orientation", "row")
    if orientation not in ("row", "column"):
        raise ValueError("scaling_plot_orientation must be 'row' or 'column'")
    timing_rows = read_csv(campaign / "results" / "summaries.csv")
    memory_path = campaign / "results" / "memory-summaries.csv"
    memory_rows = read_csv(memory_path) if memory_path.is_file() else []
    colors, _ = load_colors(COLOR_CONFIG_PATH, is_color_like)
    available = {row["algorithm"] for row in timing_rows}
    algorithms = [item for item in ALGORITHM_ORDER if item in available]
    shapes = sorted({int(row["shape"]) for row in timing_rows})
    bursts = sorted({int(row["burst"]) for row in timing_rows})
    timing = lookup(timing_rows, "completion_time_ms", "median_ms", "mad_ms")
    output = campaign / "plots"
    output.mkdir(parents=True, exist_ok=True)
    for pattern in ("3d-*", "peak-device-memory-delta*", "scheduler-time*"):
        for obsolete in output.glob(pattern):
            obsolete.unlink()

    try:
        plt.style.use("seaborn-v0_8-whitegrid")
    except OSError:
        plt.style.use("default")
    singles = [item for item in SINGLE_VOLUME if item in algorithms]
    grouped_by_shape(
        plt, np, output, shapes, bursts, singles, timing, colors,
        "Host-ready completion time (ms)", "Matched single-volume reconstruction",
        "single-volume-completion",
    )
    grouped_by_shape(
        plt, np, output, shapes, bursts, algorithms, timing, colors,
        "Host-ready completion time (ms)",
        "Incremental reconstruction (RGBA even/odd produces two volumes)",
        "all-workloads-completion",
    )
    scaling(plt, np, output, shapes, bursts, algorithms, timing, colors, orientation)

    memory = lookup(memory_rows, "peak_device_memory_mib", "median_mib", "mad_mib")
    if memory:
        grouped_by_shape(
            plt, np, output, shapes, bursts, algorithms, memory, colors,
            "Peak device memory (MiB)", "Absolute peak device memory",
            "peak-device-memory",
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    args = parser.parse_args()
    generate_plots(args.campaign.expanduser().resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
