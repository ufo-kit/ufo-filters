#!/usr/bin/env python3
"""Shared execution and aggregation helpers for cross-framework benchmarks."""

from __future__ import annotations

import datetime as dt
import gc
import json
import os
from pathlib import Path
import platform
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
    sha256,
    task_properties,
    working_directory,
    write_json,
)


SUITE = "cross-framework-support"
ALGORITHMS: tuple[dict[str, Any], ...] = ()

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
        "burst": int(entry["burst"]),
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


def execute_astra_timed(*args: Any, **kwargs: Any) -> tuple[dict[str, Any], dict[str, Any]]:
    raise RuntimeError("the active cross-framework campaign did not install an ASTRA executor")


def cleanup_astra_state(state: dict[str, Any] | None) -> None:
    del state


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
        "expected_output_buffers": (
            expected_slices(definition, shape) if definition["framework"] == "ufo" else 1),
        "output_shape": [shape, shape] if definition["framework"] == "ufo"
            else [shape, shape, shape],
        "num_projections": int(config["num_projections"]),
        "projection_shape": list(config["projection_shape"]),
        "region": region_for_shape(shape),
        "x_region": region_for_shape(shape),
        "y_region": region_for_shape(shape),
        "configured_burst": int(entry["burst"]),
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
            "shape", "burst", "configured_burst", "output_volumes", "expected_output_buffers",
            "warmup", "repetition", "round_order", "attempt", "status",
            "completion_time_ms", "baseline_device_memory_mib", "peak_device_memory_mib",
            "dcgm_memory_sample_count", "dcgm_memory_file", "manifest_path", "error",
        )})

    measured = [item for item in selected
                if item.get("status") == "success" and not item.get("warmup")]
    expected = int(config["measured_runs"])
    summaries = []
    memory_summaries = []
    incomplete = []
    for shape in map(int, config["shapes"]):
        for burst in map(int, config["bursts"]):
            for definition in config["algorithms"]:
                algorithm = definition["id"]
                group = [
                    item for item in measured
                    if item["algorithm"] == algorithm and int(item["shape"]) == shape
                    and int(item["burst"]) == burst
                ]
                if len(group) != expected:
                    incomplete.append({
                        "shape": shape, "burst": burst, "algorithm": algorithm,
                        "successful_runs": len(group), "expected_runs": expected,
                    })
                    continue
                values = [float(item["completion_time_ms"]) for item in group]
                median, mad = median_mad(values)
                summaries.append({
                    "suite": SUITE, "shape": shape, "burst": burst,
                    "algorithm": algorithm, "metric": "completion_time_ms",
                    "n": len(values), "median_ms": median, "mad_ms": mad,
                })
                metric = "peak_device_memory_mib"
                if all(item.get(metric) is not None for item in group):
                    memory_values = [float(item[metric]) for item in group]
                    memory_median, memory_mad = median_mad(memory_values)
                    memory_summaries.append({
                        "suite": SUITE, "shape": shape, "burst": burst,
                        "algorithm": algorithm, "metric": metric,
                        "n": len(memory_values), "median_mib": memory_median,
                        "mad_mib": memory_mad,
                    })

    results = campaign / "results"
    _write_csv(results / "runs.csv", run_rows,
               list(run_rows[0]) if run_rows else ["status"])
    _write_csv(results / "summaries.csv", summaries,
               list(summaries[0]) if summaries else [
                   "suite", "shape", "burst", "algorithm", "metric", "n",
                   "median_ms", "mad_ms"])
    _write_csv(results / "memory-summaries.csv", memory_summaries,
               list(memory_summaries[0]) if memory_summaries else [
                   "suite", "shape", "burst", "algorithm", "metric", "n",
                   "median_mib", "mad_mib"])
    write_json(results / "incomplete-configurations.json", incomplete)
    return {
        "manifests": len(manifests),
        "summary_rows": len(summaries),
        "memory_summary_rows": len(memory_summaries),
        "incomplete_algorithm_shape_bursts": len(incomplete),
    }


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
