#!/usr/bin/env python3
"""Create a 3D shape/algorithm chart for the UFO/ASTRA campaign."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import warnings

from plot_cross_framework import (
    ALGORITHM_ORDER,
    METRIC_LABELS,
    read_csv,
)
from plot_results import ALGORITHM_LABELS, COLOR_CONFIG_PATH, load_colors


def load_metric(campaign: Path, metric: str):
    timing = read_csv(campaign / "results" / "summaries.csv")
    memory_path = campaign / "results" / "memory-summaries.csv"
    memory = read_csv(memory_path) if memory_path.is_file() else []
    selected = [row for row in timing if row["metric"] == metric]
    if selected:
        return selected, "median_ms", "mad_ms"
    selected = [row for row in memory if row["metric"] == metric]
    if selected:
        return selected, "median_mib", "mad_mib"
    available = sorted({row["metric"] for row in [*timing, *memory] if row.get("metric")})
    raise ValueError(f"metric {metric!r} is unavailable; choose one of {available}")


def generate_plot(
    campaign: Path,
    metric: str,
    elevation: float,
    azimuth: float,
    log_time: bool,
    shade: bool,
    output_path: Path | None,
    dpi: int,
) -> Path:
    os.environ.setdefault("MPLCONFIGDIR", str(campaign / ".matplotlib"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import is_color_like
    from matplotlib.patches import Patch

    rows, median_field, mad_field = load_metric(campaign, metric)
    shapes = sorted({int(row["shape"]) for row in rows})
    available = {row["algorithm"] for row in rows}
    algorithms = [algorithm for algorithm in ALGORITHM_ORDER if algorithm in available]
    values = {
        (int(row["shape"]), row["algorithm"]):
        (float(row[median_field]), float(row[mad_field]))
        for row in rows
    }
    missing = [(shape, algorithm) for shape in shapes for algorithm in algorithms
               if (shape, algorithm) not in values]
    if missing:
        warnings.warn(
            f"3D plot is missing {len(missing)} algorithm/shape results; absent bars are left empty")
    colors, _ = load_colors(COLOR_CONFIG_PATH, is_color_like)
    medians = [value[0] for value in values.values()]
    if any(value < 0.0 for value in medians):
        raise ValueError("3D bars require non-negative median values")
    if log_time and any(value <= 0.0 for value in medians):
        raise ValueError("logarithmic 3D bars require positive median values")
    baseline = min(medians) / 10.0 if log_time else 0.0

    figure = plt.figure(figsize=(12.0, 8.5))
    axis = figure.add_subplot(projection="3d")
    bar_width = 0.68
    bar_depth = 0.68
    for shape_index, shape in enumerate(shapes):
        for algorithm_index, algorithm in enumerate(algorithms):
            if (shape, algorithm) not in values:
                continue
            median, mad = values[(shape, algorithm)]
            xpos = shape_index + 0.16
            ypos = algorithm_index + 0.16
            axis.bar3d(
                xpos, ypos, baseline, bar_width, bar_depth, median - baseline,
                color=colors[algorithm], edgecolor="black", linewidth=0.35,
                shade=shade, zsort="average",
            )
            center_x = xpos + bar_width / 2.0
            center_y = ypos + bar_depth / 2.0
            low = max(baseline, median - mad)
            high = median + mad
            axis.plot([center_x, center_x], [center_y, center_y], [low, high], color="black")
            axis.plot([center_x - 0.07, center_x + 0.07], [center_y, center_y],
                      [low, low], color="black")
            axis.plot([center_x - 0.07, center_x + 0.07], [center_y, center_y],
                      [high, high], color="black")

    axis.set_xticks(np.arange(len(shapes)) + 0.5, [str(shape) for shape in shapes])
    axis.set_yticks(
        np.arange(len(algorithms)) + 0.5,
        [ALGORITHM_LABELS.get(algorithm, algorithm) for algorithm in algorithms],
    )
    axis.set_xlabel("Cubic output side", labelpad=10)
    axis.set_ylabel("Algorithm", labelpad=14)
    axis.set_zlabel(METRIC_LABELS.get(metric, metric), labelpad=10)
    if log_time:
        axis.set_zscale("log")
        axis.set_zlim(bottom=baseline)
    axis.view_init(elev=elevation, azim=azimuth)
    figure.suptitle(
        f"{METRIC_LABELS.get(metric, metric)} — full-detector UFO/ASTRA", y=0.975)
    handles = [
        Patch(facecolor=colors[algorithm], edgecolor="black",
              label=ALGORITHM_LABELS.get(algorithm, algorithm))
        for algorithm in algorithms
    ]
    figure.legend(handles=handles, loc="upper center", bbox_to_anchor=(0.5, 0.925),
                  ncol=len(handles), frameon=False)
    figure.subplots_adjust(left=0.02, right=0.93, bottom=0.06, top=0.86)

    if output_path is None:
        suffix = "-log" if log_time else ""
        output_path = campaign / "plots" / f"3d-{metric}{suffix}.png"
    else:
        output_path = output_path.with_suffix(".png")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--metric", default="completion_time_ms")
    parser.add_argument("--elev", type=float, default=24.0)
    parser.add_argument("--azim", type=float, default=-55.0)
    parser.add_argument("--log-time", action="store_true")
    parser.add_argument("--shade", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--dpi", type=int, default=180)
    args = parser.parse_args()
    campaign = args.campaign.expanduser().resolve()
    output = args.output.expanduser().resolve() if args.output else None
    generated = generate_plot(
        campaign, args.metric, args.elev, args.azim,
        args.log_time, args.shade, output, args.dpi)
    print(generated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
