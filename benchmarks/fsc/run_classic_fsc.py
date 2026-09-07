#!/usr/bin/env python3
"""Run device-resident classic FSC and save its compact result."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
from threading import Thread
import time
import traceback
from typing import Any, Iterator

import numpy as np

from fsc_utils import FSCShellStatistics, resolve_shell_geometry


ROOT = Path(__file__).resolve().parents[2]
PARITY_MODES = {"even_odd_single", "even_odd_dual"}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).with_name("config.example.json"),
    )
    parser.add_argument("--output-dir", type=Path)
    return parser.parse_args()


def resolve_path(value: str, base: Path) -> Path:
    path = Path(value).expanduser()
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def load_configuration(path: Path, output_override: Path | None) -> dict[str, Any]:
    path = path.expanduser().resolve()
    config = json.loads(path.read_text(encoding="utf-8"))
    base = path.parent
    for key in ("dataset", "plugin_path", "kernel_source_dir", "output_dir"):
        if key not in config:
            raise ValueError(f"missing configuration value: {key}")
        config[key] = str(resolve_path(str(config[key]), base))
    if output_override is not None:
        config["output_dir"] = str(output_override.expanduser().resolve())
    return validate_configuration(config)


def validate_configuration(config: dict[str, Any]) -> dict[str, Any]:
    required = (
        "num_projections", "burst", "overall_angle", "center_position_x",
        "center_position_z", "volume_shape_zyx", "voxel_size_um",
    )
    for key in required:
        if key not in config:
            raise ValueError(f"missing configuration value: {key}")

    config.setdefault("operation_mode", "even_odd_dual")
    if config["operation_mode"] not in PARITY_MODES:
        raise ValueError("operation_mode must be even_odd_single or even_odd_dual")
    shape = tuple(int(value) for value in config["volume_shape_zyx"])
    if len(shape) != 3 or min(shape) <= 0:
        raise ValueError("volume_shape_zyx must contain three positive integers")
    voxel = config["voxel_size_um"]
    if not isinstance(voxel, dict) or set(voxel) != {"x", "y", "z"}:
        raise ValueError("voxel_size_um must contain exactly x, y and z")
    voxel_xyz = tuple(float(voxel[axis]) for axis in "xyz")
    shell_width = float(config.get("shell_width", 0.0))
    max_frequency = float(config.get("max_frequency", 0.0))
    resolve_shell_geometry(shape, voxel_xyz, shell_width, max_frequency)

    if int(config["num_projections"]) <= 1 or int(config["burst"]) <= 0:
        raise ValueError("num_projections must exceed one and burst must be positive")
    for key in ("overall_angle", "center_position_x", "center_position_z"):
        if not math.isfinite(float(config[key])):
            raise ValueError(f"{key} must be finite")

    dataset = Path(config["dataset"])
    plugin_path = Path(config["plugin_path"])
    kernel_dir = Path(config["kernel_source_dir"])
    if not dataset.is_file():
        raise FileNotFoundError(dataset)
    if not plugin_path.is_dir():
        raise FileNotFoundError(plugin_path)
    for name in ("rgba-backproject.cl", "fft.cl", "fsc-core.cl"):
        if not (kernel_dir / name).is_file():
            raise FileNotFoundError(kernel_dir / name)

    return config


def configure_environment(config: dict[str, Any]) -> None:
    plugin_path = str(config["plugin_path"])
    current = os.environ.get("UFO_PLUGIN_PATH")
    os.environ["UFO_PLUGIN_PATH"] = (
        f"{plugin_path}{os.pathsep}{current}" if current else plugin_path
    )
    os.environ["UFO_DEVICES"] = str(int(config.get("device", 0)))


def import_ufo() -> tuple[Any, Any]:
    import gi
    gi.require_version("Ufo", "0.0")
    from gi.repository import Ufo
    import ufo.numpy
    return Ufo, ufo.numpy


def centered_region(size: int) -> list[float]:
    half = float(size) / 2.0
    return [-half, half, 1.0]


def task_properties(task: Any, names: tuple[str, ...]) -> dict[str, Any]:
    result = {}
    for name in names:
        value = task.get_property(name)
        if hasattr(value, "value_nick"):
            value = value.value_nick
        elif not isinstance(value, (str, int, float, bool, type(None))):
            try:
                value = list(value)
            except TypeError:
                value = str(value)
        result[name] = value
    return result


def construct_graph(Ufo: Any, config: dict[str, Any]) -> tuple[Any, Any, Any, dict[str, Any]]:
    nz, ny, nx = (int(value) for value in config["volume_shape_zyx"])
    voxel = config["voxel_size_um"]
    manager = Ufo.PluginManager()
    graph = Ufo.TaskGraph()
    reader = manager.get_task("read")
    backprojector = manager.get_task("rgba-backproject")
    fft = manager.get_task("fft")
    fsc = manager.get_task("fsc-core")
    output = Ufo.OutputTask()
    output.set_property("num-dims", 2)

    reader.set_property("path", config["dataset"])
    properties = {
        "burst": int(config["burst"]),
        "num-projections": int(config["num_projections"]),
        "overall-angle": float(config["overall_angle"]),
        "center-position-x": [float(config["center_position_x"])],
        "center-position-z": [float(config["center_position_z"])],
        "region": centered_region(nz),
        "x-region": centered_region(nx),
        "y-region": centered_region(ny),
        "operation-mode": config["operation_mode"],
        "output-mode": "volume",
    }
    for name, value in properties.items():
        backprojector.set_property(name, value)

    fft.set_property("dimensions", 3)
    fft.set_property("auto-zeropadding", False)
    fft.set_property("size-x", nx)
    fft.set_property("size-y", ny)
    fft.set_property("size-z", nz)

    fsc.set_property("voxel-size-x", float(voxel["x"]))
    fsc.set_property("voxel-size-y", float(voxel["y"]))
    fsc.set_property("voxel-size-z", float(voxel["z"]))
    fsc.set_property("shell-width", float(config.get("shell_width", 0.0)))
    fsc.set_property("max-frequency", float(config.get("max_frequency", 0.0)))

    graph.connect_nodes(reader, backprojector)
    graph.connect_nodes(backprojector, fft)
    graph.connect_nodes(fft, fsc)
    graph.connect_nodes(fsc, output)
    recorded = {
        "rgba-backproject": task_properties(backprojector, tuple(properties)),
        "fft": task_properties(
            fft, ("dimensions", "auto-zeropadding", "size-x", "size-y", "size-z")
        ),
        "fsc-core": task_properties(
            fsc,
            ("voxel-size-x", "voxel-size-y", "voxel-size-z",
             "shell-width", "max-frequency"),
        ),
    }
    return graph, output, manager, recorded


@contextmanager
def working_directory(path: Path) -> Iterator[None]:
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def run_graph(Ufo: Any, ufo_numpy: Any, config: dict[str, Any]) -> tuple[np.ndarray, dict[str, Any]]:
    graph, output, manager, properties = construct_graph(Ufo, config)
    resources = Ufo.Resources.new()
    gpu_nodes = resources.get_gpu_nodes()
    if len(gpu_nodes) != 1:
        raise RuntimeError(
            f"classic FSC requires exactly one selected GPU, received {len(gpu_nodes)}"
        )
    gpu = gpu_nodes[0]
    device = {
        "configured_index": int(config.get("device", 0)),
        "name": str(gpu.get_info(Ufo.GpuNodeInfo.NAME)),
        "global_memory_bytes": int(gpu.get_info(Ufo.GpuNodeInfo.GLOBAL_MEM_SIZE)),
        "maximum_allocation_bytes": int(
            gpu.get_info(Ufo.GpuNodeInfo.MAX_MEM_ALLOC_SIZE)
        ),
        "local_memory_bytes": int(gpu.get_info(Ufo.GpuNodeInfo.LOCAL_MEM_SIZE)),
        "maximum_work_group_size": int(
            gpu.get_info(Ufo.GpuNodeInfo.MAX_WORK_GROUP_SIZE)
        ),
    }
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.set_property("expand", False)
    arrays: list[np.ndarray] = []
    failure: list[BaseException] = []

    def execute() -> None:
        try:
            scheduler.run(graph)
        except BaseException as exc:
            failure.append(exc)

    monitor = None
    memory = {"enabled": False}
    if bool(config.get("dcgm_memory_enabled", False)):
        sys.path.insert(0, str(ROOT))
        from benchmarks.backprojection.dcgm_memory import DcgmMemoryMonitor
        dcgm_gpu_id = config.get("dcgm_gpu_id")
        if dcgm_gpu_id is None:
            dcgm_gpu_id = config.get("device", 0)
        monitor = DcgmMemoryMonitor(
            str(config.get("dcgm_host", "localhost")),
            int(dcgm_gpu_id),
            int(config.get("dcgm_sample_interval_ms", 50)),
            config.get("dcgm_bindings_path"),
        )
        memory = dict(monitor.metadata)
        monitor.begin_run()

    started = time.perf_counter()
    thread = Thread(target=execute, daemon=True)
    try:
        with working_directory(Path(config["kernel_source_dir"])):
            thread.start()
            try:
                while True:
                    buffer = output.get_output_buffer()
                    if buffer is None:
                        break
                    try:
                        arrays.append(ufo_numpy.asarray(buffer).copy())
                    finally:
                        output.release_output_buffer(buffer)
            finally:
                thread.join()
        wall_seconds = time.perf_counter() - started
        if monitor is not None:
            memory.update(monitor.end_run())
    finally:
        if monitor is not None:
            monitor.close()

    # Keep plugin manager alive until scheduler completion.
    _ = manager
    if failure:
        raise failure[0]
    if len(arrays) != 1:
        raise RuntimeError(f"classic FSC must emit one result, received {len(arrays)}")
    return arrays[0], {
        "wall_time_seconds": wall_seconds,
        "scheduler_time_seconds": float(scheduler.props.time),
        "device": device,
        "memory": memory,
        "task_properties": properties,
    }


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_revision(path: Path) -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=path, check=False,
        capture_output=True, text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def save_outputs(
    config: dict[str, Any], statistics: FSCShellStatistics, runtime: dict[str, Any]
) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    output_dir = Path(config["output_dir"])
    output_dir.mkdir(parents=True, exist_ok=True)
    result = statistics.normalized()
    np.savez_compressed(
        output_dir / "fsc-result.npz",
        fsc=result.fsc,
        k_bin=result.k_bin,
        n_shell=result.n_shell,
        cross_sum=statistics.cross_sum,
        power_1_sum=statistics.power_1_sum,
        power_2_sum=statistics.power_2_sum,
    )

    figure, axis = plt.subplots(figsize=(7.2, 4.5), constrained_layout=True)
    axis.plot(result.k_bin, result.fsc, linewidth=1.5)
    axis.axhline(0.0, color="black", linewidth=0.6)
    axis.set_xlabel("Spatial frequency (1/µm)")
    axis.set_ylabel("FSC")
    axis.set_title("Classic Fourier shell correlation")
    axis.grid(True, alpha=0.25)
    figure.savefig(output_dir / "fsc.png", dpi=160)
    plt.close(figure)

    shape = tuple(int(value) for value in config["volume_shape_zyx"])
    voxel_xyz = tuple(float(config["voxel_size_um"][axis]) for axis in "xyz")
    geometry = resolve_shell_geometry(
        shape, voxel_xyz, float(config.get("shell_width", 0.0)),
        float(config.get("max_frequency", 0.0)),
    )
    kernel_dir = Path(config["kernel_source_dir"])
    metadata = {
        "status": "complete",
        "configuration": config,
        "resolved_shell_geometry": {
            "shell_width": geometry.shell_width,
            "max_frequency": geometry.max_frequency,
            "number_of_bins": geometry.number_of_bins,
            "frequency_unit": "1/um",
        },
        "runtime": runtime,
        "source_revisions": {
            "ufo_filters": git_revision(ROOT),
            "ufo_core": git_revision(ROOT.parent / "ufo-core")
            if (ROOT.parent / "ufo-core").is_dir() else None,
        },
        "kernel_sha256": {
            name: sha256(kernel_dir / name)
            for name in ("rgba-backproject.cl", "fft.cl", "fsc-core.cl")
        },
        "outputs": {
            "arrays": "fsc-result.npz",
            "plot": "fsc.png",
        },
    }
    (output_dir / "run.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True, default=str) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    args = parse_arguments()
    try:
        config = load_configuration(args.config, args.output_dir)
        configure_environment(config)
        Ufo, ufo_numpy = import_ufo()
        raw, runtime = run_graph(Ufo, ufo_numpy, config)
        statistics = FSCShellStatistics.from_ufo(raw)
        save_outputs(config, statistics, runtime)
        print(f"FSC bins: {statistics.k_bin.size}")
        print(f"wall time: {runtime['wall_time_seconds']:.6f} s")
        print(f"results: {config['output_dir']}")
        return 0
    except Exception:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
