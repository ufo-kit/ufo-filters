#!/usr/bin/env python3
"""Online-style UFO/ASTRA incremental backprojection benchmark."""

from __future__ import annotations

import argparse
import gc
import importlib
import json
import math
from pathlib import Path
import random
import sys
import time
from typing import Any

import cross_framework_benchmark as base
from benchmark_common import DEFAULTS as UFO_DEFAULTS, resolve_path, utc_stamp


SUITE = "ufo-vs-astra-experimental"
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
        "id": "even_odd",
        "framework": "ufo",
        "plugin": "rgba-backproject",
        "mode": "even_odd",
        "output_volumes": 2,
        "normalization": "none",
    },
    {
        "id": "astra_accumulate",
        "framework": "astra",
        "plugin": "experimental.accumulate_BP",
        "mode": "incremental_singular",
        "output_volumes": 1,
        "normalization": "none",
    },
)

DEFAULTS = dict(UFO_DEFAULTS)
DEFAULTS.update({
    "dataset": "/workspaces/ufo-filters/benchmarks/resources/fc.tif",
    "output_root": "./results",
    "campaign_dir": None,
    "projection_shape": [2016, 2016],
    "center_position_x": 997.2,
    "center_position_z": 1008.0,
    "shapes": [256, 512, 1024],
    "bursts": [16, 32, 64],
    "seed": 20260821,
    "host_memory_reserve_gib": 2.0,
    "resume": False,
    "dry_run": False,
    "generate_plots": True,
    "scaling_plot_orientation": "row",
})

_ORIGINAL_EXECUTE_RUN = base.execute_run
_ORIGINAL_COLLECT_ENVIRONMENT = base.collect_environment
_PROJECTOR_IDS: list[int] = []


def validate_configuration(config: dict[str, Any]) -> None:
    shapes = [int(value) for value in config["shapes"]]
    if not shapes or any(value <= 0 for value in shapes) or len(set(shapes)) != len(shapes):
        raise ValueError("shapes must contain unique positive integers")
    if len(config["projection_shape"]) != 2:
        raise ValueError("projection_shape must contain width and height")
    width, height = map(int, config["projection_shape"])
    if width <= 0 or height <= 0 or any(value > min(width, height) for value in shapes):
        raise ValueError("projection dimensions must be positive and contain every output shape")
    center_z = float(config["center_position_z"])
    if any(center_z - value / 2.0 < 0.0 or center_z + value / 2.0 > height
           for value in shapes):
        raise ValueError("a requested z region falls outside the detector")
    if int(config["num_projections"]) <= 0:
        raise ValueError("num_projections must be positive")
    bursts = [int(value) for value in config.get("bursts", [])]
    if not bursts or any(value < 1 or value > 128 for value in bursts):
        raise ValueError("bursts must contain values in the range 1..128")
    if len(set(bursts)) != len(bursts):
        raise ValueError("bursts must not contain duplicates")
    if int(config["warmup_runs"]) < 1 or int(config["measured_runs"]) < 1:
        raise ValueError("warmup_runs and measured_runs must be positive")
    if int(config["device"]) < 0 or int(config["dcgm_sample_interval_ms"]) <= 0:
        raise ValueError("device must be non-negative and DCGM interval must be positive")
    if float(config["host_memory_reserve_gib"]) < 0.0:
        raise ValueError("host_memory_reserve_gib must be non-negative")
    if config.get("scaling_plot_orientation") not in ("row", "column"):
        raise ValueError("scaling_plot_orientation must be 'row' or 'column'")


def load_configuration(path: Path) -> dict[str, Any]:
    raw = base.read_json(path)
    config = dict(DEFAULTS)
    config.update(raw)
    root = path.resolve().parent
    for key in ("dataset", "output_root"):
        config[key] = str(resolve_path(str(config[key]), root))
    if config.get("dcgm_bindings_path"):
        config["dcgm_bindings_path"] = str(resolve_path(
            str(config["dcgm_bindings_path"]), root))
    if config.get("dcgm_gpu_id") is None:
        config["dcgm_gpu_id"] = int(config["device"])
    requested_campaign = config.get("campaign_dir")
    if bool(config.get("resume")) and not requested_campaign:
        raise ValueError("resume requires an explicit campaign_dir")
    if requested_campaign:
        config["campaign_dir"] = str(resolve_path(str(requested_campaign), root))
    else:
        config["campaign_dir"] = str(
            Path(config["output_root"]) / f"{SUITE}-{utc_stamp()}")
    config["suite"] = SUITE
    config["algorithms"] = [dict(item) for item in ALGORITHMS]
    validate_configuration(config)
    return config


def make_schedule(config: dict[str, Any]) -> dict[str, Any]:
    rng = random.Random(int(config["seed"]))
    bursts = [int(value) for value in config["bursts"]]
    rng.shuffle(bursts)
    entries = []
    sequence = 0
    algorithms = [item["id"] for item in ALGORITHMS]
    for burst in bursts:
        shapes = [int(value) for value in config["shapes"]]
        rng.shuffle(shapes)
        for shape in shapes:
            config_id = f"n{shape:04d}-b{burst:03d}"
            for warmup in range(int(config["warmup_runs"])):
                order = list(algorithms)
                rng.shuffle(order)
                for position, algorithm in enumerate(order):
                    entries.append({
                        "sequence": sequence,
                        "logical_run_id": f"warmup-{warmup:02d}-{algorithm}",
                        "config_id": config_id,
                        "shape": shape,
                        "burst": burst,
                        "algorithm": algorithm,
                        "warmup": True,
                        "repetition": warmup,
                        "round_order": position,
                    })
                    sequence += 1
            for repetition in range(int(config["measured_runs"])):
                order = list(algorithms)
                rng.shuffle(order)
                for position, algorithm in enumerate(order):
                    entries.append({
                        "sequence": sequence,
                        "logical_run_id": f"round-{repetition:03d}-{algorithm}",
                        "config_id": config_id,
                        "shape": shape,
                        "burst": burst,
                        "algorithm": algorithm,
                        "warmup": False,
                        "repetition": repetition,
                        "round_order": position,
                    })
                    sequence += 1
    return {"suite": SUITE, "seed": int(config["seed"]), "planned_runs": entries,
            "resume_warmups": []}


def burst_ranges(num_projections: int, burst: int) -> list[tuple[int, int]]:
    ranges = [
        (start, min(start + burst, num_projections))
        for start in range(0, num_projections, burst)
    ]
    covered = [index for start, stop in ranges for index in range(start, stop)]
    if covered != list(range(num_projections)):
        raise RuntimeError("ASTRA burst ranges do not cover every projection exactly once")
    return ranges


def dry_run_report(config: dict[str, Any], schedule: dict[str, Any]) -> None:
    entries = schedule["planned_runs"]
    warmups = sum(bool(item["warmup"]) for item in entries)
    print(json.dumps({
        "suite": SUITE,
        "campaign_dir": config["campaign_dir"],
        "shapes": config["shapes"],
        "bursts": config["bursts"],
        "astra_bursts": {
            str(burst): {
                "count": len(burst_ranges(int(config["num_projections"]), int(burst))),
                "tail": burst_ranges(int(config["num_projections"]), int(burst))[-1][1]
                        - burst_ranges(int(config["num_projections"]), int(burst))[-1][0],
            }
            for burst in config["bursts"]
        },
        "algorithms": [item["id"] for item in ALGORITHMS],
        "warmup_executions": warmups,
        "measured_executions": len(entries) - warmups,
        "total_executions": len(entries),
        "dcgm_memory_enabled": bool(config["dcgm_memory_enabled"]),
        "scaling_plot_orientation": config["scaling_plot_orientation"],
    }, indent=2))


def argument_parser() -> Any:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    return parser


def prepare_projection_data(config: dict[str, Any], campaign: Path) -> Any:
    import numpy as np
    import tifffile

    projections = int(config["num_projections"])
    width, height = map(int, config["projection_shape"])
    input_bytes = projections * height * width * np.dtype(np.float32).itemsize
    largest = max(map(int, config["shapes"]))
    largest_output_bytes = largest ** 3 * np.dtype(np.float32).itemsize * 2
    reserve = int(float(config["host_memory_reserve_gib"]) * 2 ** 30)
    required = 2 * input_bytes + largest_output_bytes + reserve
    available = base.available_host_memory_bytes()
    if available is not None and available < required:
        raise RuntimeError(
            f"insufficient available host memory: need approximately {required / 2**30:.2f} GiB "
            f"for UFO input, ASTRA burst staging, outputs, and reserve; "
            f"found {available / 2**30:.2f} GiB")

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

    write_record = {
        "dataset": config["dataset"],
        "dataset_size_bytes": Path(config["dataset"]).stat().st_size,
        "ufo_shape": list(ufo_data.shape),
        "ufo_contiguous": bool(ufo_data.flags.c_contiguous),
        "ufo_bytes": int(ufo_data.nbytes),
        "dtype": str(ufo_data.dtype),
        "load_time_ms": (loaded - started) / 1_000_000.0,
        "excluded_from_completion_timing": ["TIFF loading", "ASTRA burst packing"],
        "available_host_memory_before_loading_bytes": available,
        "estimated_required_host_memory_bytes": required,
        "burst_preparations": [],
    }
    base.write_json(campaign / "input-preparation.json", write_record)
    return ufo_data


def prepare_staged_bursts(
    config: dict[str, Any], campaign: Path, ufo_data: Any, burst: int
) -> list[Any]:
    import numpy as np

    projections = int(config["num_projections"])
    width, height = map(int, config["projection_shape"])
    ranges = burst_ranges(projections, burst)
    started = time.perf_counter_ns()
    staged = [
        np.ascontiguousarray(np.transpose(ufo_data[start:stop], (1, 0, 2)))
        for start, stop in ranges
    ]
    finished = time.perf_counter_ns()
    for array, (start, stop) in zip(staged, ranges):
        expected = (height, stop - start, width)
        if array.shape != expected or array.dtype != np.float32 or not array.flags.c_contiguous:
            raise RuntimeError(f"invalid ASTRA burst staging array for [{start},{stop})")

    checks = (0, projections // 2, projections - 1)
    for projection in checks:
        burst_index = projection // burst
        local_index = projection - ranges[burst_index][0]
        for row, column in ((0, 0), (height // 2, width // 2), (height - 1, width - 1)):
            if not np.array_equal(
                ufo_data[projection, row, column],
                staged[burst_index][row, local_index, column],
                equal_nan=True,
            ):
                raise RuntimeError("UFO and staged ASTRA inputs differ")

    path = campaign / "input-preparation.json"
    record = base.read_json(path)
    record.setdefault("burst_preparations", []).append({
        "burst": burst,
        "layout": "contiguous [detector_rows, burst_angles, detector_columns]",
        "burst_count": len(staged),
        "complete_bursts": sum(stop - start == burst for start, stop in ranges),
        "tail_projections": ranges[-1][1] - ranges[-1][0],
        "ranges": [list(item) for item in ranges],
        "staged_bytes": sum(int(array.nbytes) for array in staged),
        "packing_time_ms": (finished - started) / 1_000_000.0,
        "representative_projection_checks": list(checks),
        "excluded_from_completion_timing": True,
    })
    base.write_json(path, record)
    return staged


def _delete_projectors(astra: Any | None = None) -> None:
    global _PROJECTOR_IDS
    if not _PROJECTOR_IDS:
        return
    if astra is None:
        import astra
    ids = list(_PROJECTOR_IDS)
    _PROJECTOR_IDS = []
    astra.projector3d.delete(ids)


def prepare_astra(
    config: dict[str, Any], shape: int, burst: int
) -> tuple[Any, dict[str, Any], dict[int, Any], dict[str, Any]]:
    import astra
    import cupy as cp
    import numpy as np

    global _PROJECTOR_IDS
    if _PROJECTOR_IDS:
        raise RuntimeError("ASTRA incremental projectors were already prepared")
    if not astra.use_cuda():
        raise RuntimeError("astra.use_cuda() is false; incremental CUDA BP is unavailable")
    experimental = importlib.import_module("astra.experimental")
    if not callable(getattr(experimental, "accumulate_BP", None)):
        raise RuntimeError("astra.experimental.accumulate_BP is unavailable")
    if not hasattr(np.ndarray, "__dlpack__") or not hasattr(cp.ndarray, "__dlpack__"):
        raise RuntimeError(
            "NumPy and CuPy must expose the DLPack protocol required by ASTRA")

    device = int(config["device"])
    astra.set_gpu_index(device)
    cp.cuda.Device(device).use()
    projections = int(config["num_projections"])
    width, height = map(int, config["projection_shape"])
    ranges = burst_ranges(projections, burst)
    all_angles = (np.arange(projections, dtype=np.float64) *
                  (float(config["general_overall_angle"]) / projections))
    offset = -(float(config["center_position_x"]) - width / 2.0)

    volumes: dict[int, Any] = {}
    descriptors = []
    try:
        half = shape / 2.0
        volume_geometry = astra.create_vol_geom(
            shape, shape, shape, -half, half, -half, half, -half, half)
        if tuple(astra.geom_size(volume_geometry)) != (shape, shape, shape):
            raise RuntimeError(f"ASTRA volume geometry for {shape} is invalid")
        volumes[shape] = volume_geometry

        for start, stop in ranges:
            geometry = astra.create_proj_geom(
                "parallel3d", 1.0, 1.0, height, width, all_angles[start:stop])
            geometry = astra.geom_postalignment(geometry, offset)
            if tuple(astra.geom_size(geometry)) != (height, stop - start, width):
                raise RuntimeError(f"ASTRA burst geometry for [{start},{stop}) is invalid")
            projector_id = astra.create_projector(
                "cuda3d", geometry, volume_geometry,
                options={"GPUindex": device})
            projector_ids = {shape: projector_id}
            _PROJECTOR_IDS.append(projector_id)
            descriptors.append({
                "start": start,
                "stop": stop,
                "geometry": geometry,
                "projector_ids": projector_ids,
            })
    except Exception:
        _delete_projectors(astra)
        raise

    metadata = {
        "version": getattr(astra, "__version__", None),
        "module": str(Path(astra.__file__).resolve()),
        "cupy_version": getattr(cp, "__version__", None),
        "cupy_module": str(Path(cp.__file__).resolve()),
        "cupy_cuda_runtime_version": int(cp.cuda.runtime.runtimeGetVersion()),
        "cuda_available": True,
        "gpu_index": device,
        "gpu_info": str(astra.get_gpu_info()),
        "algorithm": "astra.experimental.accumulate_BP",
        "api_stability": "experimental ASTRA 2.5.0 API",
        "projection_geometry_type": descriptors[0]["geometry"]["type"],
        "detector_spacing": [1.0, 1.0],
        "detector_shape": [height, width],
        "postalignment_pixels": offset,
        "angle_formula": "theta[i] = general_overall_angle * i / num_projections",
        "normalization": "none",
        "projection_data_bytes": projections * height * width * 4,
        "burst": burst,
        "burst_count": len(ranges),
        "complete_bursts": sum(stop - start == burst for start, stop in ranges),
        "tail_projections": ranges[-1][1] - ranges[-1][0],
        "input_location": "host-linked contiguous float32 bursts",
        "accumulator_location": "CuPy CUDA float32 array",
        "output_location": "NumPy float32 after one blocking final download",
        "automatic_splitting_required": False,
        "automatic_splitting_with_gpu_linked_output": "unsupported and intentionally unused",
        "static_projector_count": len(_PROJECTOR_IDS),
    }
    return astra, {"bursts": descriptors}, volumes, metadata


def _release_incremental_state(state: dict[str, Any] | None) -> None:
    if state is None:
        return
    astra = state["astra"]
    cp = state["cupy"]
    errors = []
    try:
        cp.cuda.Device(state["device"]).synchronize()
    except Exception as exc:
        errors.append(f"CUDA synchronization failed: {exc}")
    ids = state.get("data_ids", [])
    if ids:
        try:
            astra.data3d.delete(ids)
        except Exception as exc:
            errors.append(f"ASTRA data cleanup failed: {exc}")
    state["data_ids"] = []
    state["host_output"] = None
    state["accumulator"] = None
    gc.collect()
    try:
        state["memory_pool"].free_all_blocks()
    except Exception as exc:
        errors.append(f"CuPy memory-pool cleanup failed: {exc}")
    if errors:
        raise RuntimeError("; ".join(errors))


def execute_astra_timed(
    astra: Any,
    config: dict[str, Any],
    entry: dict[str, Any],
    projection_setup: dict[str, Any],
    volume_geometry: Any,
    staged_bursts: list[Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
    import cupy as cp
    import numpy as np

    descriptors = projection_setup["bursts"]
    if len(descriptors) != len(staged_bursts):
        raise RuntimeError("ASTRA burst geometry and staging counts differ")
    shape = int(entry["shape"])
    device = int(config["device"])
    cp.cuda.Device(device).use()
    experimental = importlib.import_module("astra.experimental")
    pool = cp.cuda.MemoryPool()
    state: dict[str, Any] = {
        "astra": astra,
        "cupy": cp,
        "device": device,
        "memory_pool": pool,
        "data_ids": [],
        "host_output": None,
        "accumulator": None,
    }
    try:
        started = time.perf_counter_ns()
        with cp.cuda.using_allocator(pool.malloc):
            accumulator = cp.zeros((shape, shape, shape), dtype=cp.float32)
        state["accumulator"] = accumulator
        volume_id = astra.data3d.link("-vol", volume_geometry, accumulator)
        state["data_ids"].append(volume_id)

        for array, descriptor in zip(staged_bursts, descriptors):
            projection_id = astra.data3d.link("-sino", descriptor["geometry"], array)
            state["data_ids"].append(projection_id)
            experimental.accumulate_BP(
                descriptor["projector_ids"][shape], volume_id, projection_id)

        cp.cuda.Device(device).synchronize()
        host_output = cp.asnumpy(accumulator)
        finished = time.perf_counter_ns()
        state["host_output"] = host_output
        if host_output.shape != (shape, shape, shape):
            raise RuntimeError(
                f"ASTRA incremental output is {host_output.shape}, expected {(shape,) * 3}")
        probes = (
            host_output[0, 0, 0],
            host_output[shape // 2, shape // 2, shape // 2],
            host_output[-1, -1, -1],
        )
        if not np.isfinite(probes).all():
            raise RuntimeError("ASTRA incremental output has non-finite representative values")
        return {
            "completion_time_ms": (finished - started) / 1_000_000.0,
            "output_shape": list(host_output.shape),
            "output_dtype": str(host_output.dtype),
            "representative_values_finite": True,
        }, state
    except Exception:
        accumulator = None
        try:
            _release_incremental_state(state)
        except Exception as cleanup_exc:
            print(f"warning: incremental ASTRA cleanup failed: {cleanup_exc}", file=sys.stderr)
        raise


def cleanup_astra_state(state: dict[str, Any] | None) -> None:
    _release_incremental_state(state)


def astra_cuda_smoke(astra: Any, gpu_index: int) -> dict[str, Any]:
    import cupy as cp
    import numpy as np

    experimental = importlib.import_module("astra.experimental")
    cp.cuda.Device(gpu_index).use()
    projection_count = 5
    detector = 8
    side = 4
    burst = 2
    angles = np.arange(projection_count, dtype=np.float64) * (-math.pi / projection_count)
    full_geometry = astra.create_proj_geom(
        "parallel3d", 1.0, 1.0, detector, detector, angles)
    volume_geometry = astra.create_vol_geom(side, side, side)
    projections = np.arange(
        detector * projection_count * detector, dtype=np.float32
    ).reshape(detector, projection_count, detector) / 100.0
    full_projection_id = full_volume_id = algorithm_id = None
    incremental_ids: list[int] = []
    projector_ids: list[int] = []
    pool = cp.cuda.MemoryPool()
    accumulator = None
    try:
        full_projection_id = astra.data3d.link("-sino", full_geometry, projections)
        full_volume_id = astra.data3d.create("-vol", volume_geometry)
        cfg = astra.astra_dict("BP3D_CUDA")
        cfg["ProjectionDataId"] = full_projection_id
        cfg["ReconstructionDataId"] = full_volume_id
        cfg["option"] = {"GPUindex": int(gpu_index)}
        algorithm_id = astra.algorithm.create(cfg)
        astra.algorithm.run(algorithm_id)
        reference = np.array(astra.data3d.get_shared(full_volume_id), copy=True)

        with cp.cuda.using_allocator(pool.malloc):
            accumulator = cp.zeros((side, side, side), dtype=cp.float32)
        volume_id = astra.data3d.link("-vol", volume_geometry, accumulator)
        incremental_ids.append(volume_id)
        for start, stop in burst_ranges(projection_count, burst):
            geometry = astra.create_proj_geom(
                "parallel3d", 1.0, 1.0, detector, detector, angles[start:stop])
            projector_id = astra.create_projector(
                "cuda3d", geometry, volume_geometry,
                options={"GPUindex": int(gpu_index)})
            projector_ids.append(projector_id)
            data = np.ascontiguousarray(projections[:, start:stop, :])
            projection_id = astra.data3d.link("-sino", geometry, data)
            incremental_ids.append(projection_id)
            experimental.accumulate_BP(projector_id, volume_id, projection_id)
        incremental = cp.asnumpy(accumulator)
        np.testing.assert_allclose(incremental, reference, rtol=1e-5, atol=1e-4)
        return {
            "status": "success",
            "projection_count": projection_count,
            "burst": burst,
            "burst_count": 3,
            "output_shape": list(incremental.shape),
            "finite": bool(np.isfinite(incremental).all()),
            "rtol": 1e-5,
            "atol": 1e-4,
        }
    finally:
        try:
            cp.cuda.Device(gpu_index).synchronize()
        except Exception:
            pass
        if algorithm_id is not None:
            astra.algorithm.delete(algorithm_id)
        full_ids = [value for value in (full_projection_id, full_volume_id) if value is not None]
        if full_ids:
            astra.data3d.delete(full_ids)
        if incremental_ids:
            astra.data3d.delete(incremental_ids)
        if projector_ids:
            astra.projector3d.delete(projector_ids)
        accumulator = None
        gc.collect()
        pool.free_all_blocks()


def execute_run(*args: Any, **kwargs: Any) -> dict[str, Any]:
    manifest = _ORIGINAL_EXECUTE_RUN(*args, **kwargs)
    if manifest.get("algorithm") == "astra_accumulate":
        config = args[8] if len(args) > 8 else kwargs["config"]
        entry = args[9] if len(args) > 9 else kwargs["entry"]
        burst = int(entry["burst"])
        ranges = burst_ranges(int(config["num_projections"]), burst)
        manifest.update({
            "configured_burst": burst,
            "astra_burst_count": len(ranges),
            "astra_complete_bursts": sum(
                stop - start == burst for start, stop in ranges),
            "astra_tail_projections": ranges[-1][1] - ranges[-1][0],
            "astra_input_location": "host-linked prepacked bursts",
            "astra_accumulator_location": "GPU-linked CuPy array",
            "astra_output_location": "host after one final blocking download",
            "astra_api_stability": "experimental ASTRA 2.5.0 API",
            "astra_projector_geometry": {
                "type": "parallel3d with postalignment (parallel3d_vec internally)",
                "detector_rows": int(config["projection_shape"][1]),
                "detector_columns": int(config["projection_shape"][0]),
                "detector_spacing": [1.0, 1.0],
                "postalignment_pixels": -(
                    float(config["center_position_x"]) -
                    float(config["projection_shape"][0]) / 2.0),
                "angle_formula": "theta[i] = general_overall_angle * i / num_projections",
                "projectors_for_output_shape": len(ranges),
            },
        })
        run_dir = args[10] if len(args) > 10 else kwargs["run_dir"]
        base.write_json(Path(run_dir) / "run.json", manifest)
    return manifest


def collect_environment(*args: Any, **kwargs: Any) -> dict[str, Any]:
    environment = _ORIGINAL_COLLECT_ENVIRONMENT(*args, **kwargs)
    astra_record = environment["astra"]
    astra_record.pop("minimum_projection_plus_largest_output_bytes", None)
    astra_record.pop("automatic_splitting_expected", None)
    astra_record["benchmark_methodology"] = "incremental acquisition-order bursts"
    return environment


def execution_entries_for_resume(
    campaign: Path, config: dict[str, Any], schedule: dict[str, Any]
) -> list[dict[str, Any]]:
    cycle = len(schedule.get("resume_warmups", []))
    rng = random.Random(int(config["seed"]) + cycle + 1)
    pending = [
        item for item in schedule["planned_runs"]
        if not item["warmup"] and not base.successful_attempt(campaign, item)
    ]
    groups: dict[str, list[dict[str, Any]]] = {}
    for item in pending:
        groups.setdefault(item["config_id"], []).append(item)
    entries = []
    recorded_warmups = []
    for block in groups.values():
        first = block[0]
        algorithms = [item["id"] for item in ALGORITHMS]
        rng.shuffle(algorithms)
        for position, algorithm in enumerate(algorithms):
            warmup = {
                "sequence": -1,
                "logical_run_id": f"resume-{cycle:02d}-warmup-{algorithm}",
                "config_id": first["config_id"],
                "shape": first["shape"],
                "burst": first["burst"],
                "algorithm": algorithm,
                "warmup": True,
                "resume_warmup": True,
                "repetition": cycle,
                "round_order": position,
            }
            entries.append(warmup)
            recorded_warmups.append(warmup)
        entries.extend(block)
    schedule.setdefault("resume_warmups", []).append({
        "cycle": cycle,
        "created_at_utc": base.dt.datetime.now(base.dt.timezone.utc).isoformat(),
        "entries": recorded_warmups,
    })
    base.write_json(campaign / "schedule.json", schedule)
    return entries


def run_campaign(
    config: dict[str, Any], schedule: dict[str, Any], resume: bool
) -> int:
    install_overrides()
    campaign = Path(config["campaign_dir"])
    base.resolve_ufo_installation(config)
    base.prepare_environment(config)
    Ufo = base.import_ufo()
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()

    import astra
    if not astra.use_cuda():
        raise RuntimeError("astra.use_cuda() is false; incremental CUDA BP is unavailable")
    smoke = astra_cuda_smoke(astra, int(config["device"]))
    ufo_data = prepare_projection_data(config, campaign)
    entries = (execution_entries_for_resume(campaign, config, schedule)
               if resume else schedule["planned_runs"])

    memory_monitor = None
    failures = 0
    environment_written = resume
    try:
        if config["dcgm_memory_enabled"]:
            from dcgm_memory import DcgmMemoryMonitor
            memory_monitor = DcgmMemoryMonitor(
                str(config["dcgm_host"]), int(config["dcgm_gpu_id"]),
                int(config["dcgm_sample_interval_ms"]), config.get("dcgm_bindings_path"))

        grouped: dict[tuple[int, int], list[dict[str, Any]]] = {}
        for entry in entries:
            grouped.setdefault((int(entry["burst"]), int(entry["shape"])), []).append(entry)

        staged_bursts = None
        current_burst = None
        for block_index, ((burst, shape), block) in enumerate(grouped.items(), start=1):
            if current_burst != burst:
                staged_bursts = None
                gc.collect()
                staged_bursts = prepare_staged_bursts(config, campaign, ufo_data, burst)
                current_burst = burst

            astra_config = dict(config)
            astra_instance, projection_setup, volume_geometries, astra_metadata = prepare_astra(
                astra_config, shape, burst)
            astra_metadata["configured_bursts"] = list(map(int, config["bursts"]))
            astra_metadata["discarded_cuda_smoke"] = smoke
            if not environment_written:
                environment = collect_environment(
                    config, Ufo, resources, astra_metadata,
                    memory_monitor.metadata if memory_monitor else None)
                base.write_json(campaign / "environment.json", environment)
                environment_written = True

            try:
                blocked: set[str] = set()
                for entry in block:
                    if base.successful_attempt(campaign, entry) or entry["algorithm"] in blocked:
                        continue
                    run_dir = base.next_attempt_directory(campaign, entry)
                    print(
                        f"[block {block_index}/{len(grouped)}] n{shape:04d} b{burst:03d} "
                        f"{entry['algorithm']} "
                        f"{'warmup' if entry['warmup'] else 'run ' + str(entry['repetition'])}",
                        flush=True,
                    )
                    manifest = execute_run(
                        Ufo, manager, resources, astra_instance, projection_setup,
                        volume_geometries, ufo_data, staged_bursts, astra_config,
                        entry, run_dir, memory_monitor)
                    if manifest["status"] != "success":
                        failures += 1
                        blocked.add(entry["algorithm"])
                        print(f"  FAILED: {manifest.get('error')}", file=sys.stderr, flush=True)
            finally:
                _delete_projectors(astra_instance)
    finally:
        try:
            _delete_projectors()
        finally:
            if memory_monitor is not None:
                memory_monitor.close()

    summary = aggregate_results(campaign)
    if bool(config.get("generate_plots", True)):
        try:
            from plot_cross_framework import generate_plots
            generate_plots(campaign)
        except Exception as exc:
            print(f"Plot generation skipped: {exc}", file=sys.stderr)
    print(f"Campaign: {campaign}")
    print(json.dumps(summary, indent=2))
    return 1 if failures else 0


def install_overrides() -> None:
    base.SUITE = SUITE
    base.ALGORITHMS = ALGORITHMS
    base.DEFAULTS = DEFAULTS
    base.validate_configuration = validate_configuration
    base.load_configuration = load_configuration
    base.make_schedule = make_schedule
    base.dry_run_report = dry_run_report
    base.argument_parser = argument_parser
    base.prepare_projection_data = prepare_projection_data
    base.prepare_astra = prepare_astra
    base.astra_cuda_smoke = astra_cuda_smoke
    base.execute_astra_timed = execute_astra_timed
    base.cleanup_astra_state = cleanup_astra_state
    base.execute_run = execute_run
    base.collect_environment = collect_environment


def aggregate_results(campaign: Path) -> dict[str, Any]:
    install_overrides()
    return base.aggregate_results(campaign)


def main() -> int:
    install_overrides()
    parser = argument_parser()
    args = parser.parse_args()
    requested = load_configuration(args.config.expanduser().resolve())
    resume = bool(requested.get("resume", False))
    campaign = Path(requested["campaign_dir"])
    if resume:
        config = base.read_json(campaign / "resolved-config.json")
        if config.get("suite") != SUITE:
            parser.error(f"campaign suite is {config.get('suite')!r}, expected {SUITE!r}")
        for key in (
            "resume", "dry_run", "generate_plots", "scaling_plot_orientation",
            "dcgm_memory_enabled", "dcgm_sample_interval_ms", "dcgm_host",
            "dcgm_gpu_id", "dcgm_bindings_path",
        ):
            config[key] = requested[key]
        schedule = base.read_json(campaign / "schedule.json")
    else:
        config = requested
        schedule = make_schedule(config)

    if bool(config.get("dry_run", False)):
        dry_run_report(config, schedule)
        return 0
    base.resolve_ufo_installation(config)
    if not resume:
        if campaign.exists() and any(campaign.iterdir()):
            parser.error(f"new campaign directory is not empty: {campaign}")
        campaign.mkdir(parents=True, exist_ok=True)
        base.write_json(campaign / "resolved-config.json", config)
        base.write_json(campaign / "schedule.json", schedule)
    return run_campaign(config, schedule, resume)
