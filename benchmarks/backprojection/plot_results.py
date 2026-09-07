#!/usr/bin/env python3
"""Create robust summary plots from a backprojection benchmark campaign."""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
from typing import Any


ALGORITHM_LABELS = {
    "general": "General",
    "rgba_singular": "RGBA singular",
    "even_odd_single": "Even/odd single launch",
    "even_odd_dual": "Even/odd dual launch",
    "astra_bp3d": "ASTRA BP3D_CUDA",
    "astra_accumulate": "ASTRA incremental BP",
}

ALGORITHM_HATCHES = {
    "general": "",
    "rgba_singular": "\\\\\\",
    "even_odd_single": "",
    "even_odd_dual": "\\\\\\",
}

STAGE_ORDER = ("projection_packing", "backprojection", "distribution", "other")
STAGE_LABELS = {
    "projection_packing": "Projection packing (accumulate)",
    "backprojection": "Backprojection (backproject*)",
    "distribution": "Distribution (distribute)",
    "other": "Other profiled kernel",
}
COLOR_CONFIG_PATH = Path(__file__).with_name("colors.example.json")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise FileNotFoundError(f"benchmark result is missing: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def save_figure(figure: Any, base: Path) -> None:
    figure.savefig(base.with_suffix(".png"), dpi=180, bbox_inches="tight")
    obsolete_pdf = base.with_suffix(".pdf")
    if obsolete_pdf.exists():
        obsolete_pdf.unlink()


def metric_lookup(rows: list[dict[str, str]]) -> dict[tuple[int, int, str, str], tuple[float, float]]:
    return {
        (int(row["shape"]), int(row["burst"]), row["algorithm"], row["metric"]):
        (float(row["median_ms"]), float(row["mad_ms"]))
        for row in rows
    }


def memory_metric_lookup(
    rows: list[dict[str, str]],
) -> dict[tuple[int, int, str, str], tuple[float, float]]:
    return {
        (int(row["shape"]), int(row["burst"]), row["algorithm"], row["metric"]):
        (float(row["median_mib"]), float(row["mad_mib"]))
        for row in rows
    }


def stage_lookup(rows: list[dict[str, str]]) -> dict[tuple[int, int, str, str], float]:
    return {
        (int(row["shape"]), int(row["burst"]), row["algorithm"], row["stage"]):
        float(row["median_ms"])
        for row in rows
    }


def plot_kernel_stages(
    plt: Any,
    np: Any,
    output: Path,
    shapes: list[int],
    bursts: list[int],
    algorithms: list[str],
    metrics: dict[tuple[int, int, str, str], tuple[float, float]],
    stages: dict[tuple[int, int, str, str], float],
    stage_colors: dict[str, str],
) -> None:
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch

    width = min(0.38, 0.8 / max(1, len(algorithms)))
    for shape in shapes:
        figure, axis = plt.subplots(figsize=(11.0, 7.2))
        x = np.arange(len(bursts), dtype=float)
        for algorithm_index, algorithm in enumerate(algorithms):
            offset = (algorithm_index - (len(algorithms) - 1) / 2.0) * width
            positions = x + offset
            bottoms = np.zeros(len(bursts), dtype=float)
            for stage in STAGE_ORDER:
                values = np.array([
                    stages.get((shape, burst, algorithm, stage), 0.0) for burst in bursts
                ])
                if np.any(values):
                    axis.bar(
                        positions, values, width, bottom=bottoms,
                        color=stage_colors[stage], edgecolor="black", linewidth=0.35,
                        hatch=ALGORITHM_HATCHES.get(algorithm, ""),
                    )
                bottoms += values
            total = [metrics.get((shape, burst, algorithm, "total_profiled_kernel_ms")) for burst in bursts]
            if all(value is not None for value in total):
                medians = np.array([value[0] for value in total])
                mads = np.array([value[1] for value in total])
                axis.errorbar(
                    positions, medians, yerr=mads, fmt="o", markersize=3.5,
                    color="black", capsize=3,
                )
        axis.set_xticks(x, [str(burst) for burst in bursts])
        axis.set_xlabel("Burst size")
        axis.set_ylabel("Profiled kernel time (ms)")
        axis.grid(axis="y", alpha=0.22)
        figure.suptitle(f"Profiled kernel stages — {shape}³ output", y=0.985)

        present_stages = [
            stage for stage in STAGE_ORDER
            if any(stages.get((shape, burst, algorithm, stage), 0.0) > 0.0
                   for burst in bursts for algorithm in algorithms)
        ]
        stage_handles = [
            Patch(
                facecolor=stage_colors[stage], edgecolor="black", linewidth=0.35,
                label=STAGE_LABELS[stage],
            )
            for stage in present_stages
        ]
        algorithm_handles = [
            Patch(
                facecolor="white", edgecolor="black",
                hatch=ALGORITHM_HATCHES.get(algorithm, ""),
                label=ALGORITHM_LABELS.get(algorithm, algorithm),
            )
            for algorithm in algorithms
        ]
        total_handle = Line2D(
            [], [], color="black", marker="o", linestyle="none", markersize=4,
            label="Median total ± MAD (not a stage)",
        )
        figure.legend(
            handles=stage_handles, title="Fill color: profiled kernel stage",
            loc="upper center", bbox_to_anchor=(0.5, 0.935),
            ncol=max(1, len(stage_handles)), fontsize="small", title_fontsize="small",
            frameon=False,
        )
        figure.legend(
            handles=algorithm_handles + [total_handle], title="Hatch / marker",
            loc="upper center", bbox_to_anchor=(0.5, 0.845),
            ncol=len(algorithm_handles) + 1, fontsize="small", title_fontsize="small",
            frameon=False,
        )
        figure.text(
            0.5, 0.018,
            "Segments are medians of cumulative profiler-wrapped kernel times; the total marker is "
            "computed independently. General buffer-to-image copies and RGBA transfers/final slice "
            "copies are not profiled here.",
            ha="center", va="bottom", fontsize="small", wrap=True,
        )
        figure.tight_layout(rect=(0.0, 0.075, 1.0, 0.735))
        save_figure(figure, output / f"kernel-stages-n{shape}")
        plt.close(figure)


def plot_grouped_metric(
    plt: Any,
    np: Any,
    output: Path,
    shapes: list[int],
    bursts: list[int],
    algorithms: list[str],
    metrics: dict[tuple[int, int, str, str], tuple[float, float]],
    metric: str,
    ylabel: str,
    title: str,
    filename: str,
    algorithm_colors: dict[str, str],
) -> None:
    width = min(0.38, 0.8 / max(1, len(algorithms)))
    for shape in shapes:
        figure, axis = plt.subplots(figsize=(9.2, 5.4))
        x = np.arange(len(bursts), dtype=float)
        for index, algorithm in enumerate(algorithms):
            values = [metrics.get((shape, burst, algorithm, metric)) for burst in bursts]
            if not all(value is not None for value in values):
                continue
            positions = x + (index - (len(algorithms) - 1) / 2.0) * width
            medians = [value[0] for value in values]
            mads = [value[1] for value in values]
            axis.bar(
                positions, medians, width, yerr=mads, capsize=3,
                color=algorithm_colors[algorithm], edgecolor="black", linewidth=0.4,
                label=ALGORITHM_LABELS.get(algorithm, algorithm),
            )
        axis.set_xticks(x, [str(burst) for burst in bursts])
        axis.set_xlabel("Burst size")
        axis.set_ylabel(ylabel)
        axis.set_title(f"{title} — {shape}³ output")
        axis.grid(axis="y", alpha=0.22)
        axis.legend()
        save_figure(figure, output / f"{filename}-n{shape}")
        plt.close(figure)


def plot_scaling(
    plt: Any,
    np: Any,
    output: Path,
    shapes: list[int],
    bursts: list[int],
    algorithms: list[str],
    metrics: dict[tuple[int, int, str, str], tuple[float, float]],
    metric: str,
    title: str,
    filename: str,
    algorithm_colors: dict[str, str],
) -> None:
    columns = 2
    rows = (len(bursts) + columns - 1) // columns
    figure, axes = plt.subplots(rows, columns, figsize=(11.0, 4.0 * rows), squeeze=False)
    for axis, burst in zip(axes.flat, bursts):
        for algorithm in algorithms:
            points = [metrics.get((shape, burst, algorithm, metric)) for shape in shapes]
            valid = [(shape, value) for shape, value in zip(shapes, points) if value and value[0] > 0]
            if not valid:
                continue
            xs = np.array([item[0] for item in valid], dtype=float)
            medians = np.array([item[1][0] for item in valid])
            mads = np.array([item[1][1] for item in valid])
            axis.errorbar(
                xs, medians, yerr=mads, marker="o", capsize=3,
                color=algorithm_colors[algorithm],
                label=ALGORITHM_LABELS.get(algorithm, algorithm),
            )
        axis.set_xscale("log", base=2)
        axis.set_yscale("log")
        axis.set_xticks(shapes, [str(shape) for shape in shapes])
        axis.set_title(f"Burst {burst}")
        axis.set_xlabel("Cubic output side")
        axis.set_ylabel("Time (ms)")
        axis.grid(True, which="both", alpha=0.22)
    for axis in axes.flat[len(bursts):]:
        axis.set_visible(False)
    handles, labels = axes.flat[0].get_legend_handles_labels()
    figure.suptitle(title, y=0.995)
    if handles:
        figure.legend(
            handles, labels, loc="upper center", bbox_to_anchor=(0.5, 0.965),
            ncol=len(algorithms), frameon=False, columnspacing=1.8, handlelength=2.4,
        )
    figure.tight_layout(rect=(0.0, 0.0, 1.0, 0.92))
    save_figure(figure, output / filename)
    plt.close(figure)


def load_colors(path: Path, is_color_like: Any) -> tuple[dict[str, str], dict[str, str]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("color configuration must be a JSON object")
    allowed = {"algorithm_colors", "stage_colors"}
    unknown_sections = set(data).difference(allowed)
    if unknown_sections:
        raise ValueError(f"unknown color configuration sections: {sorted(unknown_sections)}")
    loaded = {}
    for section in ("algorithm_colors", "stage_colors"):
        colors = data.get(section)
        if not isinstance(colors, dict):
            raise ValueError(f"{section} must be a JSON object")
        for name, color in colors.items():
            if not isinstance(color, str) or not is_color_like(color):
                raise ValueError(f"invalid Matplotlib color for {section}.{name}: {color!r}")
        loaded[section] = dict(colors)
    algorithm_colors = loaded["algorithm_colors"]
    stage_colors = loaded["stage_colors"]
    missing_algorithms = set(ALGORITHM_LABELS).difference(algorithm_colors)
    missing_stages = set(STAGE_ORDER).difference(stage_colors)
    if missing_algorithms:
        raise ValueError(f"algorithm_colors is missing {sorted(missing_algorithms)}")
    if missing_stages:
        raise ValueError(f"stage_colors is missing {sorted(missing_stages)}")
    return algorithm_colors, stage_colors


def generate_plots(campaign: Path) -> None:
    os.environ.setdefault("MPLCONFIGDIR", str(campaign / ".matplotlib"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.colors import is_color_like

    summaries = read_csv(campaign / "results" / "summaries.csv")
    stage_summaries = read_csv(campaign / "results" / "stage-summaries.csv")
    memory_path = campaign / "results" / "memory-summaries.csv"
    memory_summaries = read_csv(memory_path) if memory_path.is_file() else []
    if not summaries:
        raise RuntimeError("no complete benchmark configurations are available to plot")
    shapes = sorted({int(row["shape"]) for row in summaries})
    bursts = sorted({int(row["burst"]) for row in summaries})
    algorithms = list(dict.fromkeys(row["algorithm"] for row in summaries))
    metrics = metric_lookup(summaries)
    memory_metrics = memory_metric_lookup(memory_summaries)
    stages = stage_lookup(stage_summaries)
    algorithm_colors, stage_colors = load_colors(COLOR_CONFIG_PATH, is_color_like)
    missing_colors = set(algorithms).difference(algorithm_colors)
    if missing_colors:
        raise ValueError(
            f"colors.example.json has no colors for algorithms {sorted(missing_colors)}")
    output = campaign / "plots"
    output.mkdir(parents=True, exist_ok=True)

    try:
        plt.style.use("seaborn-v0_8-whitegrid")
    except OSError:
        plt.style.use("default")
    plot_kernel_stages(
        plt, np, output, shapes, bursts, algorithms, metrics, stages, stage_colors)
    plot_grouped_metric(
        plt, np, output, shapes, bursts, algorithms, metrics,
        "output_completion_span_ms", "Completion span (ms)",
        "Output completion span", "output-completion", algorithm_colors,
    )
    plot_grouped_metric(
        plt, np, output, shapes, bursts, algorithms, metrics,
        "scheduler_time_ms", "Scheduler time (ms)",
        "Scheduler pipeline time", "scheduler-time", algorithm_colors,
    )
    plot_scaling(
        plt, np, output, shapes, bursts, algorithms, metrics,
        "total_profiled_kernel_ms", "Profiled-kernel scaling", "kernel-scaling",
        algorithm_colors,
    )
    plot_scaling(
        plt, np, output, shapes, bursts, algorithms, metrics,
        "output_completion_span_ms", "Output-completion scaling", "completion-scaling",
        algorithm_colors,
    )
    if memory_summaries:
        plot_grouped_metric(
            plt, np, output, shapes, bursts, algorithms, memory_metrics,
            "peak_device_memory_mib", "Peak device memory (MiB)",
            "Absolute peak device memory", "peak-device-memory", algorithm_colors,
        )
        plot_grouped_metric(
            plt, np, output, shapes, bursts, algorithms, memory_metrics,
            "peak_device_memory_delta_mib", "Peak increase over baseline (MiB)",
            "Incremental peak device memory", "peak-device-memory-delta", algorithm_colors,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    args = parser.parse_args()
    generate_plots(args.campaign.expanduser().resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
