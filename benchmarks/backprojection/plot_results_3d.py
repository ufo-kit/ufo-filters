#!/usr/bin/env python3
"""Create an experimental 3D bar chart from benchmark summary metrics."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
from typing import Any

from plot_results import ALGORITHM_LABELS, COLOR_CONFIG_PATH, load_colors, read_csv


METRIC_LABELS = {
    "scheduler_time_ms": "Scheduler time (ms)",
    "runner_wall_time_ms": "Runner wall time (ms)",
    "total_profiled_kernel_ms": "Profiled kernel time (ms)",
    "backproject_task_active_ms": "Backproject task active time (ms)",
    "output_completion_span_ms": "Output completion span (ms)",
    "peak_device_memory_mib": "Peak device memory (MiB)",
    "peak_device_memory_delta_mib": "Peak increase over baseline (MiB)",
}


def load_metric(campaign: Path, metric: str) -> tuple[list[dict[str, str]], str, str]:
    timing_rows = read_csv(campaign / "results" / "summaries.csv")
    memory_path = campaign / "results" / "memory-summaries.csv"
    memory_rows = read_csv(memory_path) if memory_path.is_file() else []
    available_metrics = sorted({
        row["metric"] for row in [*timing_rows, *memory_rows]
        if row.get("metric")
    })
    selected = [row for row in timing_rows if row["metric"] == metric]
    if selected:
        return selected, "median_ms", "mad_ms"
    selected = [row for row in memory_rows if row["metric"] == metric]
    if selected:
        return selected, "median_mib", "mad_mib"
    raise ValueError(
        f"metric {metric!r} is unavailable; choose one of {available_metrics}")


def generate_3d_plot(
    campaign: Path,
    metric: str,
    elevation: float,
    azimuth: float,
    log_time: bool,
    output_path: Path | None,
    dpi: int,
    shade: bool = False,
) -> Path:
    os.environ.setdefault("MPLCONFIGDIR", str(campaign / ".matplotlib"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import is_color_like
    from matplotlib.patches import Patch

    rows, median_field, mad_field = load_metric(campaign, metric)
    if not rows:
        raise RuntimeError("no complete benchmark configurations are available to plot")

    algorithms = list(dict.fromkeys(row["algorithm"] for row in rows))
    bursts = sorted({int(row["burst"]) for row in rows})
    shapes = sorted({int(row["shape"]) for row in rows})
    values = {}
    for row in rows:
        key = (int(row["burst"]), int(row["shape"]), row["algorithm"])
        if key in values:
            raise ValueError(f"duplicate summary row for {key} and metric {metric}")
        values[key] = (float(row[median_field]), float(row[mad_field]))

    missing = [
        (burst, shape, algorithm)
        for burst in bursts for shape in shapes for algorithm in algorithms
        if (burst, shape, algorithm) not in values
    ]
    if missing:
        raise ValueError(f"3D plot requires a complete result grid; missing {missing[:5]}")

    algorithm_colors, _ = load_colors(COLOR_CONFIG_PATH, is_color_like)
    missing_colors = set(algorithms).difference(algorithm_colors)
    if missing_colors:
        raise ValueError(
            f"colors.example.json has no colors for algorithms {sorted(missing_colors)}")
    medians = [median for median, _ in values.values()]
    if any(value < 0.0 for value in medians):
        raise ValueError("3D bars require non-negative median values")
    if log_time and any(value <= 0.0 for value in medians):
        raise ValueError("logarithmic 3D bars require positive median values")
    baseline = min(medians) / 10.0 if log_time else 0.0

    figure = plt.figure(figsize=(12.0, 8.5))
    axis = figure.add_subplot(projection="3d")
    cell_width = 0.82
    algorithm_gap = 0.04
    bar_width = (cell_width - algorithm_gap * (len(algorithms) - 1)) / len(algorithms)
    bar_depth = 0.72

    for burst_index, burst in enumerate(bursts):
        for shape_index, shape in enumerate(shapes):
            for algorithm_index, algorithm in enumerate(algorithms):
                median, mad = values[(burst, shape, algorithm)]
                xpos = burst_index + 0.09 + algorithm_index * (bar_width + algorithm_gap)
                ypos = shape_index + 0.14
                height = median - baseline
                axis.bar3d(
                    xpos, ypos, baseline, bar_width, bar_depth, height,
                    color=algorithm_colors[algorithm],
                    edgecolor="black", linewidth=0.35, shade=shade, zsort="average",
                )

                center_x = xpos + bar_width / 2.0
                center_y = ypos + bar_depth / 2.0
                low = max(baseline, median - mad)
                high = median + mad
                cap = min(0.06, bar_width / 3.0)
                axis.plot([center_x, center_x], [center_y, center_y], [low, high], color="black")
                axis.plot(
                    [center_x - cap, center_x + cap], [center_y, center_y], [low, low],
                    color="black",
                )
                axis.plot(
                    [center_x - cap, center_x + cap], [center_y, center_y], [high, high],
                    color="black",
                )

    axis.set_xticks(np.arange(len(bursts)) + 0.5, [str(value) for value in bursts])
    axis.set_yticks(np.arange(len(shapes)) + 0.5, [str(value) for value in shapes])
    axis.set_xlabel("Burst size", labelpad=10)
    axis.set_ylabel("Cubic output side", labelpad=12)
    axis.set_zlabel(METRIC_LABELS.get(metric, metric), labelpad=10)
    if log_time:
        axis.set_zscale("log")
        axis.set_zlim(bottom=baseline)
    axis.view_init(elev=elevation, azim=azimuth)

    suite = rows[0]["suite"].replace("-", " ")
    scale_suffix = " — logarithmic value axis" if log_time else ""
    figure.suptitle(
        f"{METRIC_LABELS.get(metric, metric)} — {suite}{scale_suffix}", y=0.975)
    handles = [
        Patch(
            facecolor=algorithm_colors[algorithm], edgecolor="black",
            label=ALGORITHM_LABELS.get(algorithm, algorithm),
        )
        for algorithm in algorithms
    ]
    figure.legend(
        handles=handles, loc="upper center", bbox_to_anchor=(0.5, 0.925),
        ncol=len(handles), frameon=False,
    )
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
    parser.add_argument("--metric", default="total_profiled_kernel_ms")
    parser.add_argument("--elev", type=float, default=24.0, help="camera elevation in degrees")
    parser.add_argument("--azim", type=float, default=-55.0, help="camera azimuth in degrees")
    parser.add_argument(
        "--log-time", action="store_true",
        help="use a logarithmic axis for the selected metric",
    )
    parser.add_argument(
        "--shade", action="store_true",
        help="apply 3D lighting, which changes configured face colors",
    )
    parser.add_argument("--output", type=Path, help="output filename; the suffix is changed to .png")
    parser.add_argument("--dpi", type=int, default=180)
    args = parser.parse_args()

    campaign = args.campaign.expanduser().resolve()
    output = args.output.expanduser().resolve() if args.output else None
    generated = generate_3d_plot(
        campaign, args.metric, args.elev, args.azim,
        args.log_time, output, args.dpi, args.shade,
    )
    print(generated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
