#!/usr/bin/env python3
"""Execution and aggregation for the full-detector UFO/ASTRA benchmark."""

from __future__ import annotations

import argparse
import datetime as dt
import gc
import json
import math
import os
from pathlib import Path
import platform
import random
import sys
import time
import traceback
from typing import Any

from benchmark_common import (
    _gvalue,
    _write_csv,
    command_output,
    link_kernel_sources,
    median_mad,
    parse_clinfo_identifiers,
    parse_nvidia_smi,
    read_json,
    resolve_ufo_installation,
    resolve_path,
    sha256,
    task_properties,
    utc_stamp,
    working_directory,
    write_json,
)


SUITE = "ufo-vs-astra"
ALGORITHMS = (
    {
        "id": "general",
        "framework": "ufo",
        "plugin": "general-backproject",
        "mode": "singular",
        "output_volumes": 1,
        "normalization": "abs(overall_angle) / num_projections",
    },
    {
        "id": "rgba_singular",
        "framework": "ufo",
        "plugin": "rgba-backproject",
        "mode": "singular",
        "output_volumes": 1,
        "normalization": "abs(overall_angle) / num_projections",
    },
    {
        "id": "even_odd_dual",
        "framework": "ufo",
        "plugin": "rgba-backproject",
        "mode": "even_odd_dual",
        "output_volumes": 2,
        "normalization": "none",
    },
    {
        "id": "astra_bp3d",
        "framework": "astra",
        "plugin": "BP3D_CUDA",
        "mode": "singular",
        "output_volumes": 1,
        "normalization": "none",
    },
)

DEFAULTS = {
    "dataset": "/home/ws/nj4412/workspace/projects/gpr/resources/fltfc.tiff",
    "output_root": "./benchmark-results",
    "device": 0,
    "num_projections": 3001,
    "projection_shape": [1024, 1024],
    "center_position_x": 540.4,
    "center_position_z": 512.0,
    "general_overall_angle": -math.pi,
    "rgba_overall_angle": math.pi,
    "ufo_burst": 16,
    "shapes": [32, 64, 128, 256, 512],
    "warmup_runs": 1,
    "measured_runs": 10,
    "seed": 20260820,
    "host_memory_reserve_gib": 2.0,
    "dcgm_memory_enabled": False,
    "dcgm_sample_interval_ms": 50,
    "dcgm_host": "localhost",
    "dcgm_gpu_id": None,
    "dcgm_bindings_path": None,
}


def algorithm_definition(algorithm: str) -> dict[str, Any]:
    for definition in ALGORITHMS:
        if definition["id"] == algorithm:
            return definition
    raise KeyError(algorithm)


def region_for_shape(shape: int) -> list[float]:
    half = float(shape) / 2.0
    return [-half, half, 1.0]


def expected_slices(definition: dict[str, Any], shape: int) -> int:
    return int(definition["output_volumes"]) * shape


def load_configuration(path: Path, args: argparse.Namespace) -> dict[str, Any]:
    raw = read_json(path)
    config = dict(DEFAULTS)
    config.update(raw)
    base = path.resolve().parent
    for key in ("dataset", "output_root"):
        config[key] = str(resolve_path(str(config[key]), base))
    config.pop("plugin_path", None)
    config.pop("plugin_path_requested", None)
    config.pop("kernel_source_dir", None)
    if config.get("dcgm_bindings_path"):
        config["dcgm_bindings_path"] = str(resolve_path(
            str(config["dcgm_bindings_path"]), base))
    if args.shapes:
        config["shapes"] = args.shapes
    if args.runs is not None:
        config["measured_runs"] = args.runs
    if args.seed is not None:
        config["seed"] = args.seed
    if args.dcgm_memory:
        config["dcgm_memory_enabled"] = True
    if config.get("dcgm_gpu_id") is None:
        config["dcgm_gpu_id"] = int(config["device"])
    if args.output:
        config["campaign_dir"] = str(Path(args.output).expanduser().resolve())
    else:
        config["campaign_dir"] = str(
            Path(config["output_root"]) / f"{SUITE}-{utc_stamp()}")
    config["suite"] = SUITE
    config["algorithms"] = [dict(item) for item in ALGORITHMS]
    validate_configuration(config)
    return config


def validate_configuration(config: dict[str, Any]) -> None:
    shapes = [int(value) for value in config["shapes"]]
    if not shapes or any(value <= 0 for value in shapes):
        raise ValueError("shapes must contain positive integers")
    if len(set(shapes)) != len(shapes):
        raise ValueError("shapes must not contain duplicates")
    if len(config["projection_shape"]) != 2:
        raise ValueError("projection_shape must contain width and height")
    width, height = map(int, config["projection_shape"])
    if width <= 0 or height <= 0:
        raise ValueError("projection dimensions must be positive")
    if any(value > min(width, height) for value in shapes):
        raise ValueError("every cubic output side must fit within the detector dimensions")
    center_z = float(config["center_position_z"])
    if any(center_z - value / 2.0 < 0.0 or center_z + value / 2.0 > height
           for value in shapes):
        raise ValueError("a requested z region falls outside the full detector")
    if int(config["num_projections"]) <= 0:
        raise ValueError("num_projections must be positive")
    if not 1 <= int(config["ufo_burst"]) <= 128:
        raise ValueError("ufo_burst must be in the range 1..128")
    if int(config["warmup_runs"]) < 1:
        raise ValueError("warmup_runs must be at least one")
    if int(config["measured_runs"]) < 1:
        raise ValueError("measured_runs must be positive")
    if int(config["measured_runs"]) < 10:
        print("warning: fewer than 10 measured runs is intended for smoke tests only",
              file=sys.stderr)
    if int(config["device"]) < 0:
        raise ValueError("device must be non-negative")
    if float(config["host_memory_reserve_gib"]) < 0.0:
        raise ValueError("host_memory_reserve_gib must be non-negative")
    if int(config["dcgm_sample_interval_ms"]) <= 0:
        raise ValueError("dcgm_sample_interval_ms must be positive")


def make_schedule(config: dict[str, Any]) -> dict[str, Any]:
    rng = random.Random(int(config["seed"]))
    shapes = [int(value) for value in config["shapes"]]
    rng.shuffle(shapes)
    algorithm_ids = [item["id"] for item in ALGORITHMS]
    entries = []
    sequence = 0
    for shape in shapes:
        config_id = f"n{shape:04d}"
        for warmup in range(int(config["warmup_runs"])):
            order = list(algorithm_ids)
            rng.shuffle(order)
            for position, algorithm in enumerate(order):
                entries.append({
                    "sequence": sequence,
                    "logical_run_id": f"warmup-{warmup:02d}-{algorithm}",
                    "config_id": config_id,
                    "shape": shape,
                    "algorithm": algorithm,
                    "warmup": True,
                    "repetition": warmup,
                    "round_order": position,
                })
                sequence += 1
        for repetition in range(int(config["measured_runs"])):
            order = list(algorithm_ids)
            rng.shuffle(order)
            for position, algorithm in enumerate(order):
                entries.append({
                    "sequence": sequence,
                    "logical_run_id": f"round-{repetition:03d}-{algorithm}",
                    "config_id": config_id,
                    "shape": shape,
                    "algorithm": algorithm,
                    "warmup": False,
                    "repetition": repetition,
                    "round_order": position,
                })
                sequence += 1
    return {"suite": SUITE, "seed": int(config["seed"]), "planned_runs": entries,
            "resume_warmups": []}


def dry_run_report(config: dict[str, Any], schedule: dict[str, Any]) -> None:
    entries = schedule["planned_runs"]
    warmups = sum(bool(item["warmup"]) for item in entries)
    print(json.dumps({
        "suite": SUITE,
        "campaign_dir": config["campaign_dir"],
        "shapes": config["shapes"],
        "ufo_burst": int(config["ufo_burst"]),
        "algorithms": [item["id"] for item in ALGORITHMS],
        "warmup_executions": warmups,
        "measured_executions": len(entries) - warmups,
        "total_executions": len(entries),
        "dcgm_memory_enabled": bool(config["dcgm_memory_enabled"]),
    }, indent=2))


def prepare_environment(config: dict[str, Any]) -> None:
    os.environ["UFO_DEVICES"] = str(config["device"])
    plugin_path = str(config["ufo_installation"]["plugindir"])
    previous = os.environ.get("UFO_PLUGIN_PATH", "")
    os.environ["UFO_PLUGIN_PATH"] = os.pathsep.join(
        [plugin_path] + ([previous] if previous else []))


def import_ufo() -> Any:
    import gi
    gi.require_version("Ufo", "0.0")
    from gi.repository import Ufo
    return Ufo


def available_host_memory_bytes() -> int | None:
    path = Path("/proc/meminfo")
    if not path.is_file():
        return None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) * 1024
    return None


def prepare_projection_data(config: dict[str, Any], campaign: Path) -> tuple[Any, Any]:
    import numpy as np
    import tifffile

    projections = int(config["num_projections"])
    width, height = map(int, config["projection_shape"])
    input_bytes = projections * height * width * np.dtype(np.float32).itemsize
    largest = max(map(int, config["shapes"]))
    largest_output_bytes = largest ** 3 * np.dtype(np.float32).itemsize * 2
    reserve = int(float(config["host_memory_reserve_gib"]) * 2 ** 30)
    required = 2 * input_bytes + largest_output_bytes + reserve
    available = available_host_memory_bytes()
    if available is not None and available < required:
        raise RuntimeError(
            f"insufficient available host memory: need approximately {required / 2**30:.2f} GiB "
            f"for both projection layouts and outputs, found {available / 2**30:.2f} GiB")

    started = time.perf_counter_ns()
    ufo_data = tifffile.imread(config["dataset"])
    loaded = time.perf_counter_ns()
    expected_shape = (projections, height, width)
    if ufo_data.shape != expected_shape:
        raise ValueError(f"dataset shape is {ufo_data.shape}, expected {expected_shape}")
    if ufo_data.dtype != np.float32:
        raise ValueError(f"dataset dtype is {ufo_data.dtype}, expected float32")
    if not ufo_data.flags.c_contiguous:
        ufo_data = np.ascontiguousarray(ufo_data)
    astra_data = np.ascontiguousarray(np.transpose(ufo_data, (1, 0, 2)))
    transposed = time.perf_counter_ns()
    if astra_data.shape != (height, projections, width):
        raise AssertionError("ASTRA projection layout has an unexpected shape")
    checks = ((0, 0, 0), (projections // 2, height // 2, width // 2),
              (projections - 1, height - 1, width - 1))
    ufo_checks = np.asarray([
        ufo_data[projection, row, column] for projection, row, column in checks])
    astra_checks = np.asarray([
        astra_data[row, projection, column] for projection, row, column in checks])
    if not np.array_equal(ufo_checks, astra_checks, equal_nan=True):
        raise RuntimeError("UFO and ASTRA projection layouts do not contain identical data")

    write_json(campaign / "input-preparation.json", {
        "dataset": config["dataset"],
        "dataset_size_bytes": Path(config["dataset"]).stat().st_size,
        "ufo_shape": list(ufo_data.shape),
        "astra_shape": list(astra_data.shape),
        "dtype": str(ufo_data.dtype),
        "ufo_contiguous": bool(ufo_data.flags.c_contiguous),
        "astra_contiguous": bool(astra_data.flags.c_contiguous),
        "ufo_bytes": int(ufo_data.nbytes),
        "astra_bytes": int(astra_data.nbytes),
        "load_time_ms": (loaded - started) / 1_000_000.0,
        "layout_conversion_time_ms": (transposed - loaded) / 1_000_000.0,
        "excluded_from_completion_timing": True,
        "representative_layout_checks": [list(item) for item in checks],
        "available_host_memory_before_loading_bytes": available,
        "estimated_required_host_memory_bytes": required,
    })
    return ufo_data, astra_data


def kernel_sources(config: dict[str, Any], definition: dict[str, Any]) -> dict[str, Path]:
    root = Path(config["ufo_installation"]["kerneldir"])
    if definition["plugin"] == "rgba-backproject":
        return {"rgba-backproject.cl": root / "rgba-backproject.cl"}
    if definition["plugin"] == "general-backproject":
        names = (
            "general_bp_definitions.in", "general_bp_body.in",
            "general_bp_header_scalar.in", "general_bp_header_vector.in",
        )
        return {name: root / name for name in names}
    return {}


def construct_ufo_graph(
    Ufo: Any, manager: Any, config: dict[str, Any], entry: dict[str, Any], ufo_data: Any
) -> tuple[Any, dict[str, Any]]:
    definition = algorithm_definition(entry["algorithm"])
    shape = int(entry["shape"])
    width, height = map(int, config["projection_shape"])
    region = region_for_shape(shape)
    graph = Ufo.TaskGraph()
    source = manager.get_task("memory-in")
    backprojector = manager.get_task(definition["plugin"])
    sink = manager.get_task("null")

    source.set_property("pointer", int(ufo_data.ctypes.data))
    source.set_property("width", width)
    source.set_property("height", height)
    source.set_property("bitdepth", 32)
    source.set_property("number", int(config["num_projections"]))
    source.set_property("memory-location", "host")

    common = {
        "burst": int(config["ufo_burst"]),
        "num-projections": int(config["num_projections"]),
        "center-position-x": [float(config["center_position_x"])],
        "center-position-z": [float(config["center_position_z"])],
        "region": region,
        "x-region": region,
        "y-region": region,
    }
    for name, value in common.items():
        backprojector.set_property(name, value)
    if definition["plugin"] == "general-backproject":
        backprojector.set_property("overall-angle", float(config["general_overall_angle"]))
    else:
        backprojector.set_property("overall-angle", float(config["rgba_overall_angle"]))
        backprojector.set_property("operation-mode", definition["mode"])

    sink.set_property("download", True)
    sink.set_property("finish", True)
    sink.set_property("durations", False)
    graph.connect_nodes(source, backprojector)
    graph.connect_nodes(backprojector, sink)

    names = [
        "burst", "num-projections", "overall-angle", "center-position-x",
        "center-position-z", "region", "x-region", "y-region", "addressing-mode",
    ]
    if definition["plugin"] == "general-backproject":
        names.extend(("compute-type", "result-type", "store-type"))
    else:
        names.append("operation-mode")
    return graph, task_properties(backprojector, names)


def prepare_astra(
    config: dict[str, Any]
) -> tuple[Any, dict[str, Any], dict[int, Any], dict[str, Any]]:
    import astra
    import numpy as np

    if not astra.use_cuda():
        raise RuntimeError("astra.use_cuda() is false; BP3D_CUDA is unavailable")
    astra.set_gpu_index(int(config["device"]))
    projections = int(config["num_projections"])
    width, height = map(int, config["projection_shape"])
    angles = (np.arange(projections, dtype=np.float64) *
              (float(config["general_overall_angle"]) / projections))
    projection_geometry = astra.create_proj_geom(
        "parallel3d", 1.0, 1.0, height, width, angles)
    offset = -(float(config["center_position_x"]) - width / 2.0)
    projection_geometry = astra.geom_postalignment(projection_geometry, offset)
    if tuple(astra.geom_size(projection_geometry)) != (height, projections, width):
        raise RuntimeError("ASTRA projection geometry has an unexpected size")

    volumes = {}
    for shape in map(int, config["shapes"]):
        half = shape / 2.0
        geometry = astra.create_vol_geom(
            shape, shape, shape,
            -half, half, -half, half, -half, half,
        )
        if tuple(astra.geom_size(geometry)) != (shape, shape, shape):
            raise RuntimeError(f"ASTRA volume geometry for {shape} has an unexpected size")
        volumes[shape] = geometry
    metadata = {
        "version": getattr(astra, "__version__", None),
        "module": str(Path(astra.__file__).resolve()),
        "cuda_available": True,
        "gpu_index": int(config["device"]),
        "gpu_info": str(astra.get_gpu_info()),
        "algorithm": "BP3D_CUDA",
        "projection_geometry_type": projection_geometry["type"],
        "projection_geometry_size": list(astra.geom_size(projection_geometry)),
        "detector_spacing": [1.0, 1.0],
        "postalignment_pixels": offset,
        "angle_formula": "theta[i] = general_overall_angle * i / num_projections",
        "normalization": "none",
        "projection_data_bytes": projections * height * width * 4,
        "automatic_splitting_supported": True,
    }
    return astra, projection_geometry, volumes, metadata


def astra_cuda_smoke(astra: Any, gpu_index: int) -> dict[str, Any]:
    import numpy as np

    angles = np.array([0.0, -math.pi / 2.0], dtype=np.float64)
    projection_geometry = astra.create_proj_geom("parallel3d", 1.0, 1.0, 4, 4, angles)
    volume_geometry = astra.create_vol_geom(4, 4, 4)
    projections = np.ones((4, 2, 4), dtype=np.float32)
    projection_id = volume_id = algorithm_id = None
    try:
        projection_id = astra.data3d.link("-sino", projection_geometry, projections)
        volume_id = astra.data3d.create("-vol", volume_geometry)
        cfg = astra.astra_dict("BP3D_CUDA")
        cfg["ProjectionDataId"] = projection_id
        cfg["ReconstructionDataId"] = volume_id
        cfg["option"] = {"GPUindex": int(gpu_index)}
        algorithm_id = astra.algorithm.create(cfg)
        astra.algorithm.run(algorithm_id)
        output = astra.data3d.get_shared(volume_id)
        finite = bool(np.isfinite(output).all())
        output_shape = list(output.shape)
        del output
        if not finite or output_shape != [4, 4, 4]:
            raise RuntimeError("ASTRA CUDA smoke reconstruction returned invalid output")
        return {"status": "success", "output_shape": output_shape, "finite": finite}
    finally:
        if algorithm_id is not None:
            astra.algorithm.delete(algorithm_id)
        ids = [value for value in (projection_id, volume_id) if value is not None]
        if ids:
            astra.data3d.delete(ids)


def execute_astra_timed(
    astra: Any,
    config: dict[str, Any],
    entry: dict[str, Any],
    projection_geometry: Any,
    volume_geometry: Any,
    astra_data: Any,
) -> tuple[dict[str, Any], dict[str, Any]]:
    import numpy as np

    projection_id = volume_id = algorithm_id = None
    output = None
    try:
        started = time.perf_counter_ns()
        projection_id = astra.data3d.link("-sino", projection_geometry, astra_data)
        volume_id = astra.data3d.create("-vol", volume_geometry)
        cfg = astra.astra_dict("BP3D_CUDA")
        cfg["ProjectionDataId"] = projection_id
        cfg["ReconstructionDataId"] = volume_id
        cfg["option"] = {"GPUindex": int(config["device"])}
        algorithm_id = astra.algorithm.create(cfg)
        astra.algorithm.run(algorithm_id)
        output = astra.data3d.get_shared(volume_id)
        finished = time.perf_counter_ns()
        shape = int(entry["shape"])
        if output.shape != (shape, shape, shape):
            raise RuntimeError(f"ASTRA output shape is {output.shape}, expected {(shape,) * 3}")
        probes = (output[0, 0, 0], output[shape // 2, shape // 2, shape // 2],
                  output[-1, -1, -1])
        if not np.isfinite(probes).all():
            raise RuntimeError("ASTRA output contains non-finite representative values")
        metrics = {
            "completion_time_ms": (finished - started) / 1_000_000.0,
            "output_shape": list(output.shape),
            "output_dtype": str(output.dtype),
            "representative_values_finite": True,
        }
        state = {
            "astra": astra,
            "algorithm_id": algorithm_id,
            "data_ids": [projection_id, volume_id],
            "output": output,
        }
        return metrics, state
    except Exception:
        if output is not None:
            del output
        if algorithm_id is not None:
            astra.algorithm.delete(algorithm_id)
        ids = [value for value in (projection_id, volume_id) if value is not None]
        if ids:
            astra.data3d.delete(ids)
        raise


def cleanup_astra_state(state: dict[str, Any] | None) -> None:
    if state is None:
        return
    astra = state["astra"]
    state["output"] = None
    astra.algorithm.delete(state["algorithm_id"])
    astra.data3d.delete(state["data_ids"])


def attempts_for(campaign: Path, entry: dict[str, Any]) -> list[Path]:
    parent = campaign / "runs" / entry["config_id"]
    if not parent.exists():
        return []
    return sorted(parent.glob(entry["logical_run_id"] + "-attempt-*"))


def successful_attempt(campaign: Path, entry: dict[str, Any]) -> bool:
    return any(
        (path / "run.json").is_file() and
        read_json(path / "run.json").get("status") == "success"
        for path in attempts_for(campaign, entry)
    )


def next_attempt_directory(campaign: Path, entry: dict[str, Any]) -> Path:
    existing = attempts_for(campaign, entry)
    return (campaign / "runs" / entry["config_id"] /
            f"{entry['logical_run_id']}-attempt-{len(existing):02d}")


def execute_run(
    Ufo: Any,
    manager: Any,
    resources: Any,
    astra: Any,
    projection_geometry: Any,
    volume_geometries: dict[int, Any],
    ufo_data: Any,
    astra_data: Any,
    config: dict[str, Any],
    entry: dict[str, Any],
    run_dir: Path,
    memory_monitor: Any | None,
) -> dict[str, Any]:
    definition = algorithm_definition(entry["algorithm"])
    shape = int(entry["shape"])
    run_dir.mkdir(parents=True, exist_ok=False)
    sources = {}
    if definition["framework"] == "ufo":
        sources = link_kernel_sources(run_dir, kernel_sources(config, definition))
    manifest: dict[str, Any] = {
        **entry,
        "suite": SUITE,
        "attempt": int(run_dir.name.rsplit("-", 1)[-1]),
        "status": "running",
        "started_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "framework": definition["framework"],
        "plugin_or_algorithm": definition["plugin"],
        "operation_mode": definition["mode"],
        "normalization": definition["normalization"],
        "output_volumes": int(definition["output_volumes"]),
        "expected_output_slices": expected_slices(definition, shape),
        "num_projections": int(config["num_projections"]),
        "projection_shape": list(config["projection_shape"]),
        "region": region_for_shape(shape),
        "x_region": region_for_shape(shape),
        "y_region": region_for_shape(shape),
        "configured_burst": int(config["ufo_burst"])
            if definition["framework"] == "ufo" else None,
        "ufo_installation": config["ufo_installation"],
        "kernel_sources": sources,
    }
    if definition["framework"] == "ufo":
        plugin_binary = (
            Path(config["ufo_installation"]["plugindir"]) /
            f"libufofilter{definition['plugin']}.so"
        )
        manifest["plugin_binary"] = {
            "path": str(plugin_binary),
            "sha256": sha256(plugin_binary),
        }
    write_json(run_dir / "run.json", manifest)

    astra_state = None
    memory_started = False
    try:
        if memory_monitor is not None:
            memory_monitor.begin_run()
            memory_started = True
        if definition["framework"] == "ufo":
            graph, properties = construct_ufo_graph(Ufo, manager, config, entry, ufo_data)
            manifest["effective_properties"] = properties
            scheduler = Ufo.Scheduler()
            scheduler.set_resources(resources)
            scheduler.set_property("enable-tracing", False)
            with working_directory(run_dir):
                started = time.perf_counter_ns()
                scheduler.run(graph)
                finished = time.perf_counter_ns()
            manifest["completion_time_ms"] = (finished - started) / 1_000_000.0
            manifest["scheduler_time_ms"] = float(scheduler.props.time) * 1000.0
        else:
            metrics, astra_state = execute_astra_timed(
                astra, config, entry, projection_geometry,
                volume_geometries[shape], astra_data)
            manifest.update(metrics)

        if memory_monitor is not None:
            report = memory_monitor.end_run()
            memory_started = False
            filename = "dcgm-memory.json"
            write_json(run_dir / filename, report)
            manifest.update({
                "dcgm_memory_file": filename,
                "baseline_device_memory_mib": report["baseline_device_memory_mib"],
                "peak_device_memory_mib": report["peak_device_memory_mib"],
                "peak_device_memory_delta_mib": report["peak_device_memory_delta_mib"],
                "dcgm_memory_sample_count": report["sample_count"],
            })
        manifest["status"] = "success"
    except Exception as exc:
        if memory_started:
            try:
                report = memory_monitor.end_run()
                write_json(run_dir / "dcgm-memory.json", report)
            except Exception as memory_exc:
                manifest["dcgm_cleanup_error"] = str(memory_exc)
        manifest["status"] = "failed"
        manifest["error"] = str(exc)
        manifest["traceback"] = traceback.format_exc()
    finally:
        try:
            cleanup_astra_state(astra_state)
        except Exception as cleanup_exc:
            manifest["astra_cleanup_error"] = str(cleanup_exc)
            if manifest.get("status") == "success":
                manifest["status"] = "failed"
                manifest["error"] = f"ASTRA cleanup failed: {cleanup_exc}"
        manifest["finished_at_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
        write_json(run_dir / "run.json", manifest)
        gc.collect()
    return manifest


def all_manifests(campaign: Path) -> list[dict[str, Any]]:
    result = []
    for path in campaign.glob("runs/*/*/run.json"):
        item = read_json(path)
        item["manifest_path"] = str(path.relative_to(campaign))
        result.append(item)
    return sorted(result, key=lambda item: (
        item.get("sequence", 10**9), item.get("attempt", 0), item["manifest_path"]))


def latest_logical_manifests(manifests: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for item in manifests:
        key = (item.get("config_id", ""), item.get("logical_run_id", ""))
        grouped.setdefault(key, []).append(item)
    selected = []
    for attempts in grouped.values():
        successes = [item for item in attempts if item.get("status") == "success"]
        selected.append(max(successes or attempts, key=lambda item: int(item.get("attempt", 0))))
    return selected


def aggregate_results(campaign: Path) -> dict[str, Any]:
    config = read_json(campaign / "resolved-config.json")
    manifests = all_manifests(campaign)
    selected = latest_logical_manifests(manifests)
    run_rows = []
    for item in manifests:
        run_rows.append({key: item.get(key) for key in (
            "suite", "config_id", "logical_run_id", "sequence", "algorithm",
            "framework", "plugin_or_algorithm", "operation_mode", "normalization",
            "shape", "configured_burst", "output_volumes", "expected_output_slices",
            "warmup", "repetition", "round_order", "attempt", "status",
            "completion_time_ms", "scheduler_time_ms", "baseline_device_memory_mib",
            "peak_device_memory_mib", "peak_device_memory_delta_mib",
            "dcgm_memory_sample_count", "dcgm_memory_file", "manifest_path", "error",
        )})

    measured = [item for item in selected
                if item.get("status") == "success" and not item.get("warmup")]
    expected = int(config["measured_runs"])
    summaries = []
    memory_summaries = []
    incomplete = []
    for shape in map(int, config["shapes"]):
        for definition in config["algorithms"]:
            algorithm = definition["id"]
            group = [item for item in measured
                     if item["algorithm"] == algorithm and int(item["shape"]) == shape]
            if len(group) != expected:
                incomplete.append({
                    "shape": shape, "algorithm": algorithm,
                    "successful_runs": len(group), "expected_runs": expected,
                })
                continue
            values = [float(item["completion_time_ms"]) for item in group]
            median, mad = median_mad(values)
            summaries.append({
                "suite": SUITE, "shape": shape, "algorithm": algorithm,
                "metric": "completion_time_ms", "n": len(values),
                "median_ms": median, "mad_ms": mad,
            })
            for metric in ("peak_device_memory_mib", "peak_device_memory_delta_mib"):
                if all(item.get(metric) is not None for item in group):
                    memory_values = [float(item[metric]) for item in group]
                    memory_median, memory_mad = median_mad(memory_values)
                    memory_summaries.append({
                        "suite": SUITE, "shape": shape, "algorithm": algorithm,
                        "metric": metric, "n": len(memory_values),
                        "median_mib": memory_median, "mad_mib": memory_mad,
                    })

    results = campaign / "results"
    _write_csv(results / "runs.csv", run_rows,
               list(run_rows[0]) if run_rows else ["status"])
    _write_csv(results / "summaries.csv", summaries,
               list(summaries[0]) if summaries else [
                   "suite", "shape", "algorithm", "metric", "n", "median_ms", "mad_ms"])
    _write_csv(results / "memory-summaries.csv", memory_summaries,
               list(memory_summaries[0]) if memory_summaries else [
                   "suite", "shape", "algorithm", "metric", "n",
                   "median_mib", "mad_mib"])
    write_json(results / "incomplete-configurations.json", incomplete)
    return {
        "manifests": len(manifests),
        "summary_rows": len(summaries),
        "memory_summary_rows": len(memory_summaries),
        "incomplete_algorithm_shapes": len(incomplete),
    }


def resume_warmups(
    campaign: Path, config: dict[str, Any], schedule: dict[str, Any]
) -> list[dict[str, Any]]:
    pending_shapes = sorted({
        int(entry["shape"]) for entry in schedule["planned_runs"]
        if not entry["warmup"] and not successful_attempt(campaign, entry)
    })
    cycle = len(schedule.get("resume_warmups", []))
    rng = random.Random(int(config["seed"]) + cycle + 1)
    entries = []
    for shape in pending_shapes:
        algorithms = [item["id"] for item in ALGORITHMS]
        rng.shuffle(algorithms)
        for position, algorithm in enumerate(algorithms):
            entries.append({
                "sequence": -1,
                "logical_run_id": f"resume-{cycle:02d}-warmup-{algorithm}",
                "config_id": f"n{shape:04d}",
                "shape": shape,
                "algorithm": algorithm,
                "warmup": True,
                "resume_warmup": True,
                "repetition": cycle,
                "round_order": position,
            })
    schedule.setdefault("resume_warmups", []).append({
        "cycle": cycle,
        "created_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "entries": entries,
    })
    write_json(campaign / "schedule.json", schedule)
    return entries


def collect_environment(
    config: dict[str, Any], Ufo: Any, resources: Any, astra_metadata: dict[str, Any],
    dcgm_metadata: dict[str, Any] | None,
) -> dict[str, Any]:
    gpu_nodes = resources.get_gpu_nodes()
    if len(gpu_nodes) != 1:
        raise RuntimeError(f"benchmark requires one selected OpenCL GPU, found {len(gpu_nodes)}")
    gpu = gpu_nodes[0]
    gpu_info = {}
    for label, value in (
        ("name", Ufo.GpuNodeInfo.NAME),
        ("global_memory_bytes", Ufo.GpuNodeInfo.GLOBAL_MEM_SIZE),
        ("max_allocation_bytes", Ufo.GpuNodeInfo.MAX_MEM_ALLOC_SIZE),
    ):
        gpu_info[label] = _gvalue(gpu.get_info(value))

    astra_record = dict(astra_metadata)
    largest = max(map(int, config["shapes"]))
    minimum_gpu_data_bytes = int(astra_record["projection_data_bytes"]) + largest ** 3 * 4
    astra_record["minimum_projection_plus_largest_output_bytes"] = minimum_gpu_data_bytes
    astra_record["automatic_splitting_expected"] = (
        minimum_gpu_data_bytes > int(gpu_info["global_memory_bytes"]))

    nvidia = command_output([
        "nvidia-smi", "--query-gpu=index,name,uuid,driver_version,memory.total",
        "--format=csv,noheader,nounits",
    ])
    clinfo = command_output(["clinfo", "--raw"])
    pkg_ufo = command_output(["pkg-config", "--modversion", "ufo"])
    repository = Path(__file__).resolve().parents[2]
    git_commit = command_output(["git", "-C", str(repository), "rev-parse", "HEAD"])
    git_status = command_output(["git", "-C", str(repository), "status", "--porcelain"])
    plugins = {}
    for name in ("memory-in", "null", "general-backproject", "rgba-backproject"):
        path = (Path(config["ufo_installation"]["plugindir"]) /
                f"libufofilter{name}.so")
        plugins[name] = {"path": str(path), "sha256": sha256(path)}
    kernel_sources_inventory = {}
    for name in (
        "rgba-backproject.cl", "general_bp_definitions.in", "general_bp_body.in",
        "general_bp_header_scalar.in", "general_bp_header_vector.in",
    ):
        path = Path(config["ufo_installation"]["kerneldir"]) / name
        kernel_sources_inventory[name] = {"path": str(path), "sha256": sha256(path)}
    return {
        "captured_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "host": platform.node(),
        "platform": platform.platform(),
        "python": sys.version,
        "ufo_version": pkg_ufo.get("stdout", "").strip() or None,
        "ufo_gpu": gpu_info,
        "astra": astra_record,
        "nvidia_smi": {
            **{key: value for key, value in nvidia.items() if key != "stdout"},
            "devices": parse_nvidia_smi(nvidia.get("stdout", "")),
        },
        "clinfo": {
            **{key: value for key, value in clinfo.items() if key != "stdout"},
            "identifiers": parse_clinfo_identifiers(clinfo.get("stdout", "")),
        },
        "git_commit": git_commit.get("stdout", "").strip() or None,
        "git_dirty": bool(git_status.get("stdout", "").strip()),
        "git_status": git_status.get("stdout", "").splitlines(),
        "dataset": {
            "path": config["dataset"],
            "size_bytes": Path(config["dataset"]).stat().st_size,
            "modified_ns": Path(config["dataset"]).stat().st_mtime_ns,
        },
        "paths": {
            "ufo_installation": config["ufo_installation"],
        },
        "plugins": plugins,
        "kernel_sources": kernel_sources_inventory,
        "environment": {key: os.environ.get(key) for key in (
            "UFO_DEVICES", "UFO_PLUGIN_PATH", "UFO_KERNEL_PATH", "CUDA_VISIBLE_DEVICES",
            "LD_LIBRARY_PATH", "GI_TYPELIB_PATH", "PKG_CONFIG_PATH")},
        "dcgm_memory": dcgm_metadata or {
            "enabled": False, "reason": "dcgm_memory_enabled is false"},
    }


def run_campaign(
    config: dict[str, Any], schedule: dict[str, Any], resume: bool, make_plots: bool
) -> int:
    campaign = Path(config["campaign_dir"])
    resolve_ufo_installation(config)
    prepare_environment(config)
    for name in ("memory-in", "null", "general-backproject", "rgba-backproject"):
        path = (Path(config["ufo_installation"]["plugindir"]) /
                f"libufofilter{name}.so")
        if not path.is_file():
            raise FileNotFoundError(f"benchmark plugin not found: {path}")

    Ufo = import_ufo()
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()
    astra, projection_geometry, volume_geometries, astra_metadata = prepare_astra(config)
    smoke = astra_cuda_smoke(astra, int(config["device"]))
    astra_metadata["discarded_cuda_smoke"] = smoke

    memory_monitor = None
    failures = 0
    try:
        if config["dcgm_memory_enabled"]:
            from dcgm_memory import DcgmMemoryMonitor
            memory_monitor = DcgmMemoryMonitor(
                str(config["dcgm_host"]), int(config["dcgm_gpu_id"]),
                int(config["dcgm_sample_interval_ms"]), config.get("dcgm_bindings_path"))
        if not resume:
            environment = collect_environment(
                config, Ufo, resources, astra_metadata,
                memory_monitor.metadata if memory_monitor else None)
            write_json(campaign / "environment.json", environment)

        ufo_data, astra_data = prepare_projection_data(config, campaign)
        entries = schedule["planned_runs"]
        if resume:
            entries = resume_warmups(campaign, config, schedule) + [
                entry for entry in entries if not entry["warmup"]]

        blocked: set[tuple[int, str]] = set()
        for index, entry in enumerate(entries, start=1):
            if successful_attempt(campaign, entry):
                continue
            key = (int(entry["shape"]), entry["algorithm"])
            if key in blocked:
                continue
            run_dir = next_attempt_directory(campaign, entry)
            print(
                f"[{index}/{len(entries)}] n{int(entry['shape']):04d} "
                f"{entry['algorithm']} "
                f"{'warmup' if entry['warmup'] else 'run ' + str(entry['repetition'])}",
                flush=True,
            )
            manifest = execute_run(
                Ufo, manager, resources, astra, projection_geometry, volume_geometries,
                ufo_data, astra_data, config, entry, run_dir, memory_monitor)
            if manifest["status"] != "success":
                failures += 1
                blocked.add(key)
                print(f"  FAILED: {manifest.get('error')}", file=sys.stderr, flush=True)
    finally:
        if memory_monitor is not None:
            memory_monitor.close()

    summary = aggregate_results(campaign)
    if make_plots:
        try:
            from plot_cross_framework import generate_plots
            generate_plots(campaign)
        except Exception as exc:
            print(f"Plot generation skipped: {exc}", file=sys.stderr)
    print(f"Campaign: {campaign}")
    print(json.dumps(summary, indent=2))
    return 1 if failures else 0


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config", type=Path,
        default=Path(__file__).with_name("cross-framework-config.example.json"))
    parser.add_argument("--output", type=Path, help="exact directory for a new campaign")
    parser.add_argument("--shapes", type=int, nargs="+", help="override output side lengths")
    parser.add_argument("--runs", type=int, help="override measured repetitions")
    parser.add_argument("--seed", type=int, help="override randomization seed")
    parser.add_argument("--dcgm-memory", action="store_true",
                        help="strictly monitor framebuffer memory with DCGM")
    parser.add_argument("--resume", type=Path, help="resume an existing campaign")
    parser.add_argument("--dry-run", action="store_true", help="print schedule without running")
    parser.add_argument("--no-plots", action="store_true")
    return parser


def main() -> int:
    parser = argument_parser()
    args = parser.parse_args()
    if args.resume:
        if any((args.output, args.shapes, args.runs is not None, args.seed is not None,
                args.dcgm_memory)):
            parser.error("--resume cannot be combined with configuration overrides")
        campaign = args.resume.expanduser().resolve()
        config = read_json(campaign / "resolved-config.json")
        if config.get("suite") != SUITE:
            parser.error(f"campaign suite is {config.get('suite')!r}, expected {SUITE!r}")
        schedule = read_json(campaign / "schedule.json")
        resume = True
    else:
        config = load_configuration(args.config.expanduser().resolve(), args)
        schedule = make_schedule(config)
        campaign = Path(config["campaign_dir"])
        resume = False

    if args.dry_run:
        dry_run_report(config, schedule)
        return 0
    resolve_ufo_installation(config)
    if not resume:
        if campaign.exists() and any(campaign.iterdir()):
            parser.error(f"new campaign directory is not empty: {campaign}")
        campaign.mkdir(parents=True, exist_ok=True)
        write_json(campaign / "resolved-config.json", config)
        write_json(campaign / "schedule.json", schedule)
    return run_campaign(config, schedule, resume, not args.no_plots)


if __name__ == "__main__":
    raise SystemExit(main())
