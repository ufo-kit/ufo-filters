#!/usr/bin/env python3
"""Shared execution and trace-analysis code for backprojection benchmarks."""

from __future__ import annotations

import argparse
import contextlib
import csv
import datetime as dt
import gc
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import shutil
import statistics
import subprocess
import sys
import time
import traceback
from typing import Any, Iterable


SUITES = {
    "general-vs-rgba": (
        {"id": "general", "plugin": "general-backproject", "mode": "singular"},
        {"id": "rgba_singular", "plugin": "rgba-backproject", "mode": "singular"},
    ),
    "general-vs-rgba-even-odd": (
        {"id": "general", "plugin": "general-backproject", "mode": "singular"},
        {"id": "even_odd", "plugin": "rgba-backproject", "mode": "even_odd"},
    ),
}

DEFAULTS = {
    "dataset": "/workspaces/ufo-filters/benchmarks/resources/fc.tif",
    "output_root": "./results",
    "campaign_dir": None,
    "device": 0,
    "num_projections": 3001,
    "projection_shape": [2016, 2016],
    "center_position_x": 997.2,
    "center_position_z": 1008.0,
    "general_overall_angle": -math.pi,
    "rgba_overall_angle": math.pi,
    "shapes": [256, 512, 1024],
    "bursts": [16, 32, 64],
    "warmup_runs": 1,
    "measured_runs": 10,
    "seed": 20260819,
    "dcgm_memory_enabled": False,
    "dcgm_sample_interval_ms": 50,
    "dcgm_host": "localhost",
    "dcgm_gpu_id": None,
    "dcgm_bindings_path": None,
    "resume": False,
    "dry_run": False,
    "generate_plots": True,
    "scaling_plot_orientation": "row",
}

RGBA_KERNEL_NAMES = {
    "singular": {"accumulate", "backproject", "distribute"},
    "even_odd": {"accumulate", "backproject_even", "backproject_odd", "distribute"},
}

SUMMARY_METRICS = (
    "total_profiled_kernel_ms",
    "backproject_task_active_ms",
    "output_completion_span_ms",
)


def _json_default(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    if hasattr(value, "value_nick"):
        return value.value_nick
    if isinstance(value, (set, tuple)):
        return list(value)
    return str(value)


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(data, indent=2, sort_keys=True, default=_json_default) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def utc_stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_path(value: str, base: Path) -> Path:
    path = Path(value).expanduser()
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def resolve_ufo_installation(config: dict[str, Any]) -> None:
    """Resolve and validate the UFO installation selected by pkg-config."""
    config.pop("plugin_path", None)
    config.pop("plugin_path_requested", None)
    config.pop("kernel_source_dir", None)

    variables = ("prefix", "plugindir", "kerneldir", "pcfiledir")
    installation: dict[str, str] = {}
    errors = []
    for variable in variables:
        try:
            result = subprocess.run(
                ["pkg-config", f"--variable={variable}", "ufo"],
                text=True, capture_output=True, check=False, timeout=10,
            )
        except (OSError, subprocess.SubprocessError) as exc:
            errors.append(f"{variable}: {exc}")
            continue
        value = result.stdout.strip()
        if result.returncode != 0 or not value:
            detail = result.stderr.strip() or "empty value"
            errors.append(f"{variable}: {detail}")
        else:
            installation[variable] = str(Path(value).expanduser().resolve())

    diagnostic = {
        "PKG_CONFIG_PATH": os.environ.get("PKG_CONFIG_PATH"),
        "resolved": installation,
        "errors": errors,
    }
    if errors:
        raise RuntimeError(
            "failed to resolve the active UFO installation with pkg-config:\n" +
            json.dumps(diagnostic, indent=2, sort_keys=True)
        )

    missing_directories = [
        name for name in ("plugindir", "kerneldir", "pcfiledir")
        if not Path(installation[name]).is_dir()
    ]
    if missing_directories:
        diagnostic["missing_directories"] = missing_directories
        raise FileNotFoundError(
            "the active UFO pkg-config metadata points to missing directories:\n" +
            json.dumps(diagnostic, indent=2, sort_keys=True)
        )

    cross_framework = any("framework" in item for item in config["algorithms"])
    source_plugin = "memory-in" if cross_framework else "read"
    plugin_names = {
        source_plugin,
        "null",
        *(
            item["plugin"] for item in config["algorithms"]
            if item.get("framework", "ufo") == "ufo"
        ),
    }
    plugin_dir = Path(installation["plugindir"])
    missing_plugins = [
        str(plugin_dir / f"libufofilter{name}.so") for name in sorted(plugin_names)
        if not (plugin_dir / f"libufofilter{name}.so").is_file()
    ]
    kernel_dir = Path(installation["kerneldir"])
    kernel_names = (
        "rgba-backproject.cl",
        "general_bp_definitions.in",
        "general_bp_body.in",
        "general_bp_header_scalar.in",
        "general_bp_header_vector.in",
    )
    missing_kernels = [
        str(kernel_dir / name) for name in kernel_names
        if not (kernel_dir / name).is_file()
    ]
    if missing_plugins or missing_kernels:
        diagnostic.update({
            "missing_plugins": missing_plugins,
            "missing_kernels": missing_kernels,
        })
        raise FileNotFoundError(
            "the active UFO installation is incomplete for this benchmark:\n" +
            json.dumps(diagnostic, indent=2, sort_keys=True)
        )

    config["ufo_installation"] = installation


def load_configuration(path: Path, suite: str) -> dict[str, Any]:
    raw = read_json(path)
    config = dict(DEFAULTS)
    config.update(raw)
    base = path.resolve().parent

    for key in ("dataset", "output_root"):
        config[key] = str(resolve_path(str(config[key]), base))
    config.pop("plugin_path", None)
    config.pop("plugin_path_requested", None)
    config.pop("kernel_source_dir", None)

    if config.get("dcgm_gpu_id") is None:
        config["dcgm_gpu_id"] = int(config["device"])
    if config.get("dcgm_bindings_path"):
        config["dcgm_bindings_path"] = str(resolve_path(
            str(config["dcgm_bindings_path"]), base))
    requested_campaign = config.get("campaign_dir")
    if bool(config.get("resume")) and not requested_campaign:
        raise ValueError("resume requires an explicit campaign_dir")
    if requested_campaign:
        config["campaign_dir"] = str(resolve_path(str(config["campaign_dir"]), base))
    else:
        name = f"{suite}-{utc_stamp()}"
        config["campaign_dir"] = str(Path(config["output_root"]) / name)

    config["suite"] = suite
    config["algorithms"] = [dict(item) for item in SUITES[suite]]
    validate_configuration(config)
    return config


def validate_configuration(config: dict[str, Any]) -> None:
    if not config["shapes"] or any(int(value) <= 0 for value in config["shapes"]):
        raise ValueError("shapes must contain positive integers")
    if not config["bursts"] or any(int(value) <= 0 for value in config["bursts"]):
        raise ValueError("bursts must contain positive integers")
    if int(config["warmup_runs"]) < 1:
        raise ValueError("warmup_runs must be at least one")
    if int(config["measured_runs"]) < 1:
        raise ValueError("measured_runs must be at least one")
    if int(config["measured_runs"]) < 10:
        print(
            "warning: fewer than 10 measured runs is intended only for framework smoke tests",
            file=sys.stderr,
        )
    if int(config["num_projections"]) <= 0:
        raise ValueError("num_projections must be positive")
    if len(config["projection_shape"]) != 2:
        raise ValueError("projection_shape must contain width and height")
    if int(config["device"]) < 0:
        raise ValueError("device must be a non-negative OpenCL device index")
    if int(config.get("dcgm_sample_interval_ms", 50)) <= 0:
        raise ValueError("dcgm_sample_interval_ms must be positive")
    if int(config.get("dcgm_gpu_id", config["device"])) < 0:
        raise ValueError("dcgm_gpu_id must be a non-negative DCGM device ID")
    if not str(config.get("dcgm_host", "")).strip():
        raise ValueError("dcgm_host must not be empty")
    if config.get("scaling_plot_orientation") not in ("row", "column"):
        raise ValueError("scaling_plot_orientation must be 'row' or 'column'")


def make_schedule(config: dict[str, Any]) -> dict[str, Any]:
    rng = random.Random(int(config["seed"]))
    blocks = [(int(shape), int(burst)) for shape in config["shapes"] for burst in config["bursts"]]
    rng.shuffle(blocks)
    entries: list[dict[str, Any]] = []
    sequence = 0

    for shape, burst in blocks:
        config_id = f"n{shape:04d}-b{burst:03d}"
        algorithms = [item["id"] for item in config["algorithms"]]

        for warmup in range(int(config["warmup_runs"])):
            order = list(algorithms)
            rng.shuffle(order)
            for position, algorithm in enumerate(order):
                entries.append(
                    {
                        "sequence": sequence,
                        "logical_run_id": f"warmup-{warmup:02d}-{algorithm}",
                        "config_id": config_id,
                        "shape": shape,
                        "burst": burst,
                        "algorithm": algorithm,
                        "warmup": True,
                        "repetition": warmup,
                        "pair_order": position,
                    }
                )
                sequence += 1

        for repetition in range(int(config["measured_runs"])):
            order = list(algorithms)
            rng.shuffle(order)
            for position, algorithm in enumerate(order):
                entries.append(
                    {
                        "sequence": sequence,
                        "logical_run_id": f"round-{repetition:03d}-{algorithm}",
                        "config_id": config_id,
                        "shape": shape,
                        "burst": burst,
                        "algorithm": algorithm,
                        "warmup": False,
                        "repetition": repetition,
                        "pair_order": position,
                    }
                )
                sequence += 1

    return {
        "suite": config["suite"],
        "seed": int(config["seed"]),
        "planned_runs": entries,
        "resume_warmups": [],
    }


def dry_run_report(config: dict[str, Any], schedule: dict[str, Any]) -> None:
    total = len(schedule["planned_runs"])
    warmups = sum(bool(entry["warmup"]) for entry in schedule["planned_runs"])
    print(json.dumps({
        "suite": config["suite"],
        "campaign_dir": config["campaign_dir"],
        "shapes": config["shapes"],
        "bursts": config["bursts"],
        "measured_runs": config["measured_runs"],
        "warmup_runs": config["warmup_runs"],
        "warmup_executions": warmups,
        "measured_executions": total - warmups,
        "total_executions": total,
        "dcgm_memory_enabled": bool(config.get("dcgm_memory_enabled", False)),
        "dcgm_sample_interval_ms": int(config.get("dcgm_sample_interval_ms", 50)),
        "scaling_plot_orientation": config["scaling_plot_orientation"],
    }, indent=2))


def prepare_environment(config: dict[str, Any]) -> None:
    os.environ["UFO_DEVICES"] = str(config["device"])
    plugin_path = str(config["ufo_installation"]["plugindir"])
    previous = os.environ.get("UFO_PLUGIN_PATH", "")
    parts = [plugin_path] + ([previous] if previous else [])
    os.environ["UFO_PLUGIN_PATH"] = os.pathsep.join(parts)


def import_ufo() -> Any:
    import gi

    gi.require_version("Ufo", "0.0")
    from gi.repository import Ufo
    return Ufo


def _gvalue(value: Any) -> Any:
    if hasattr(value, "get_value"):
        try:
            return value.get_value()
        except Exception:
            pass
    return _serializable(value)


def _serializable(value: Any) -> Any:
    if hasattr(value, "value_nick"):
        return value.value_nick
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, (list, tuple)):
        return [_serializable(item) for item in value]
    try:
        return list(value)
    except TypeError:
        return str(value)


def command_output(command: list[str]) -> dict[str, Any]:
    executable = shutil.which(command[0])
    if executable is None:
        return {"command": command, "available": False, "error": "executable not found"}
    try:
        result = subprocess.run(command, text=True, capture_output=True, check=False, timeout=30)
        return {
            "command": command,
            "available": True,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
        }
    except Exception as exc:
        return {"command": command, "available": True, "error": str(exc)}


def parse_nvidia_smi(text: str) -> list[dict[str, Any]]:
    devices = []
    for line in text.splitlines():
        values = [value.strip() for value in line.split(",")]
        if len(values) == 5:
            devices.append({
                "index": values[0], "name": values[1], "uuid": values[2],
                "driver_version": values[3], "memory_total_mib": values[4],
            })
    return devices


def parse_clinfo_identifiers(text: str) -> dict[str, list[str]]:
    labels = (
        "Platform Name", "Platform Vendor", "Platform Version", "Device Name",
        "Device Vendor", "Driver Version", "Device Version", "Device OpenCL C Version",
    )
    result: dict[str, list[str]] = {}
    for line in text.splitlines():
        stripped = line.strip()
        for label in labels:
            if stripped.startswith(label):
                value = re.sub(r"^" + re.escape(label) + r"\s*", "", stripped).strip()
                if value and value not in result.setdefault(label, []):
                    result[label].append(value)
                break
    return result


def collect_environment(
    config: dict[str, Any],
    Ufo: Any,
    resources: Any,
    campaign: Path,
    dcgm_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    gpu_nodes = resources.get_gpu_nodes()
    if len(gpu_nodes) != 1:
        raise RuntimeError(f"benchmark requires exactly one selected GPU, found {len(gpu_nodes)}")

    gpu = gpu_nodes[0]
    gpu_info = {}
    for label, enum_value in (
        ("name", Ufo.GpuNodeInfo.NAME),
        ("global_memory_bytes", Ufo.GpuNodeInfo.GLOBAL_MEM_SIZE),
        ("max_allocation_bytes", Ufo.GpuNodeInfo.MAX_MEM_ALLOC_SIZE),
        ("local_memory_bytes", Ufo.GpuNodeInfo.LOCAL_MEM_SIZE),
        ("max_work_group_size", Ufo.GpuNodeInfo.MAX_WORK_GROUP_SIZE),
    ):
        gpu_info[label] = _gvalue(gpu.get_info(enum_value))

    nvidia = command_output([
        "nvidia-smi",
        "--query-gpu=index,name,uuid,driver_version,memory.total",
        "--format=csv,noheader,nounits",
    ])
    clinfo = command_output(["clinfo", "--raw"])
    pkg_ufo = command_output(["pkg-config", "--modversion", "ufo"])
    repository_root = Path(__file__).resolve().parents[2]
    git_commit = command_output(["git", "-C", str(repository_root), "rev-parse", "HEAD"])
    git_status = command_output(["git", "-C", str(repository_root), "status", "--porcelain"])
    plugin_inventory = {}
    plugin_names = {"read", "null", *(item["plugin"] for item in config["algorithms"])}
    for plugin_name in sorted(plugin_names):
        filename = f"libufofilter{plugin_name}.so"
        path = Path(config["ufo_installation"]["plugindir"]) / filename
        plugin_inventory[filename] = {"path": str(path), "sha256": sha256(path)}
    kernel_inventory = {}
    for filename in (
        "rgba-backproject.cl", "general_bp_definitions.in", "general_bp_body.in",
        "general_bp_header_scalar.in", "general_bp_header_vector.in",
    ):
        path = Path(config["ufo_installation"]["kerneldir"]) / filename
        kernel_inventory[filename] = {"path": str(path), "sha256": sha256(path)}

    (campaign / "nvidia-smi.txt").write_text(
        nvidia.get("stdout", "") + nvidia.get("stderr", ""), encoding="utf-8")
    (campaign / "clinfo.txt").write_text(
        clinfo.get("stdout", "") + clinfo.get("stderr", ""), encoding="utf-8")

    nvidia_devices = parse_nvidia_smi(nvidia.get("stdout", ""))
    clinfo_identifiers = parse_clinfo_identifiers(clinfo.get("stdout", ""))
    if not nvidia_devices:
        print("warning: NVIDIA driver identifiers were not available from nvidia-smi", file=sys.stderr)
    if not clinfo_identifiers:
        print("warning: OpenCL runtime identifiers were not available from clinfo", file=sys.stderr)

    return {
        "captured_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "host": platform.node(),
        "platform": platform.platform(),
        "python": sys.version,
        "gpu": gpu_info,
        "nvidia_smi": {
            **{key: value for key, value in nvidia.items() if key != "stdout"},
            "devices": nvidia_devices,
        },
        "clinfo": {
            **{key: value for key, value in clinfo.items() if key != "stdout"},
            "identifiers": clinfo_identifiers,
        },
        "ufo_version": pkg_ufo.get("stdout", "").strip() or None,
        "git_commit": git_commit.get("stdout", "").strip() or None,
        "git_dirty": bool(git_status.get("stdout", "").strip()),
        "git_status": git_status.get("stdout", "").splitlines(),
        "environment": {
            key: os.environ.get(key)
            for key in (
                "UFO_DEVICES", "UFO_PLUGIN_PATH", "UFO_KERNEL_PATH", "LD_LIBRARY_PATH",
                "GI_TYPELIB_PATH", "PKG_CONFIG_PATH",
            )
        },
        "paths": {
            "dataset": config["dataset"],
            "ufo_installation": config["ufo_installation"],
        },
        "plugins": plugin_inventory,
        "kernel_sources": kernel_inventory,
        "dcgm_memory": dcgm_metadata or {
            "enabled": False,
            "reason": "dcgm_memory_enabled is false",
        },
    }


def algorithm_definition(config: dict[str, Any], algorithm: str) -> dict[str, str]:
    for item in config["algorithms"]:
        if item["id"] == algorithm:
            return item
    raise KeyError(algorithm)


def region_for_shape(shape: int) -> list[float]:
    half = float(shape) / 2.0
    return [-half, half, 1.0]


def expected_outputs(algorithm: dict[str, str], shape: int) -> int:
    return shape * (2 if algorithm["mode"].startswith("even_odd") else 1)


def kernel_sources(config: dict[str, Any], algorithm: dict[str, str]) -> dict[str, Path]:
    root = Path(config["ufo_installation"]["kerneldir"])
    if algorithm["plugin"] == "rgba-backproject":
        return {"rgba-backproject.cl": root / "rgba-backproject.cl"}
    names = (
        "general_bp_definitions.in",
        "general_bp_body.in",
        "general_bp_header_scalar.in",
        "general_bp_header_vector.in",
    )
    return {name: root / name for name in names}


def link_kernel_sources(run_dir: Path, sources: dict[str, Path]) -> dict[str, Any]:
    inventory = {}
    for destination_name, source in sources.items():
        if not source.is_file():
            raise FileNotFoundError(f"kernel source not found: {source}")
        destination = run_dir / destination_name
        destination.symlink_to(source.resolve())
        inventory[destination_name] = {"source": str(source.resolve()), "sha256": sha256(source)}
    return inventory


@contextlib.contextmanager
def working_directory(path: Path) -> Iterable[None]:
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def task_properties(task: Any, names: Iterable[str]) -> dict[str, Any]:
    values = {}
    for name in names:
        try:
            values[name] = _serializable(task.get_property(name))
        except Exception as exc:
            values[name] = {"unavailable": str(exc)}
    return values


def construct_graph(Ufo: Any, manager: Any, config: dict[str, Any], entry: dict[str, Any]):
    definition = algorithm_definition(config, entry["algorithm"])
    shape = int(entry["shape"])
    region = region_for_shape(shape)

    graph = Ufo.TaskGraph()
    reader = manager.get_task("read")
    backprojector = manager.get_task(definition["plugin"])
    sink = manager.get_task("null")

    reader.set_property("path", config["dataset"])
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

    sink.set_property("download", False)
    sink.set_property("finish", True)
    sink.set_property("durations", False)
    graph.connect_nodes(reader, backprojector)
    graph.connect_nodes(backprojector, sink)

    property_names = [
        "burst", "num-projections", "overall-angle", "center-position-x",
        "center-position-z", "region", "x-region", "y-region", "addressing-mode",
    ]
    if definition["plugin"] == "general-backproject":
        property_names.extend(("compute-type", "result-type", "store-type"))
    else:
        property_names.append("operation-mode")
    recorded_properties = task_properties(backprojector, property_names)
    return graph, recorded_properties


def _events(path: Path) -> list[dict[str, Any]]:
    data = read_json(path)
    if isinstance(data, dict):
        return data.get("traceEvents", [])
    return data


def paired_intervals(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    stacks: dict[tuple[Any, Any, Any, Any], list[float]] = {}
    intervals = []
    for event in events:
        phase = event.get("ph")
        if phase not in ("B", "E"):
            continue
        key = (event.get("pid"), event.get("tid"), event.get("name"), event.get("cat"))
        if phase == "B":
            stacks.setdefault(key, []).append(float(event["ts"]))
        else:
            if key not in stacks or not stacks[key]:
                raise ValueError(f"unmatched end event: {key}")
            start = stacks[key].pop()
            end = float(event["ts"])
            if end < start:
                raise ValueError(f"negative trace duration: {key}")
            intervals.append({
                "pid": key[0], "tid": key[1], "name": key[2], "cat": key[3],
                "start_us": start, "end_us": end, "duration_ms": (end - start) / 1000.0,
            })
    unmatched = [key for key, values in stacks.items() if values]
    if unmatched:
        raise ValueError(f"unmatched begin events: {unmatched[:5]}")
    return intervals


def kernel_stage(name: str) -> str:
    if name == "accumulate":
        return "projection_packing"
    if name.startswith("backproject"):
        return "backprojection"
    if name == "distribute":
        return "distribution"
    return "other"


def analyze_traces(
    opencl_path: Path,
    trace_path: Path,
    config: dict[str, Any],
    entry: dict[str, Any],
) -> dict[str, Any]:
    definition = algorithm_definition(config, entry["algorithm"])
    opencl_intervals = paired_intervals(_events(opencl_path))
    task_intervals = paired_intervals(_events(trace_path))
    kernels: dict[str, dict[str, Any]] = {}
    stages: dict[str, float] = {}

    for interval in opencl_intervals:
        name = str(interval["name"])
        item = kernels.setdefault(name, {"count": 0, "durations_ms": [], "total_ms": 0.0})
        item["count"] += 1
        item["durations_ms"].append(interval["duration_ms"])
        item["total_ms"] += interval["duration_ms"]
        stage = kernel_stage(name)
        stages[stage] = stages.get(stage, 0.0) + interval["duration_ms"]

    warnings = []
    if "other" in stages:
        warnings.append("unrecognized profiled kernels were retained in the 'other' stage")

    expected_names = ({"backproject"} if definition["plugin"] == "general-backproject"
                      else RGBA_KERNEL_NAMES[definition["mode"]])
    missing = expected_names.difference(kernels)
    if missing:
        raise ValueError(f"missing expected kernels: {sorted(missing)}")

    num_projections = int(config["num_projections"])
    burst = int(entry["burst"])
    if definition["plugin"] == "rgba-backproject":
        capacity = burst if definition["mode"] == "singular" else 2 * burst
        batches = math.ceil(num_projections / capacity)
        expected_counts = {"accumulate": batches, "distribute": 1}
        if definition["mode"] == "singular":
            expected_counts["backproject"] = batches
        else:
            expected_counts["backproject_even"] = batches
            expected_counts["backproject_odd"] = batches
            expected_counts["distribute"] = 2
        for name, expected in expected_counts.items():
            actual = kernels[name]["count"]
            if actual != expected:
                raise ValueError(f"kernel {name!r}: expected {expected} calls, found {actual}")
    else:
        batches = math.ceil(num_projections / burst)
        calls = kernels["backproject"]["count"]
        if calls < batches or calls % batches:
            raise ValueError(
                f"general backproject calls ({calls}) are not a positive multiple of batches ({batches})")
        kernels["backproject"]["inferred_chunks"] = calls // batches

    task_marker = "UfoGeneralBackprojectTask" if definition["plugin"] == "general-backproject" \
        else "UfoRGBABackprojectTask"
    backproject_calls = [item for item in task_intervals if task_marker in str(item["tid"])]
    process_calls = sorted(
        (item for item in backproject_calls if item["name"] == "process"),
        key=lambda item: item["start_us"])
    generate_calls = sorted(
        (item for item in backproject_calls if item["name"] == "generate"),
        key=lambda item: item["start_us"])
    sink_calls = sorted(
        (item for item in task_intervals
         if "UfoNullTask" in str(item["tid"]) and item["name"] == "process"),
        key=lambda item: item["start_us"])
    outputs = expected_outputs(definition, int(entry["shape"]))

    if len(process_calls) != num_projections:
        raise ValueError(f"expected {num_projections} process calls, found {len(process_calls)}")
    if len(generate_calls) != outputs + 1:
        raise ValueError(f"expected {outputs + 1} generate calls, found {len(generate_calls)}")
    if len(sink_calls) != outputs:
        raise ValueError(f"expected {outputs} sink calls, found {len(sink_calls)}")

    successful_generates = generate_calls[:outputs]
    active_ms = sum(item["duration_ms"] for item in process_calls)
    active_ms += sum(item["duration_ms"] for item in successful_generates)
    completion_ms = (sink_calls[-1]["end_us"] - process_calls[0]["start_us"]) / 1000.0

    for item in kernels.values():
        item["median_call_ms"] = statistics.median(item["durations_ms"])

    return {
        "kernels": kernels,
        "stages_ms": stages,
        "total_profiled_kernel_ms": sum(item["total_ms"] for item in kernels.values()),
        "backproject_task_active_ms": active_ms,
        "output_completion_span_ms": completion_ms,
        "counts": {
            "process": len(process_calls),
            "generate": len(generate_calls),
            "successful_generate": outputs,
            "sink": len(sink_calls),
            "profiled_kernels": len(opencl_intervals),
        },
        "warnings": warnings,
    }


def attempts_for(campaign: Path, entry: dict[str, Any]) -> list[Path]:
    parent = campaign / "runs" / entry["config_id"]
    return sorted(parent.glob(entry["logical_run_id"] + "-attempt-*")) if parent.exists() else []


def successful_attempt(campaign: Path, entry: dict[str, Any]) -> bool:
    for attempt in attempts_for(campaign, entry):
        manifest = attempt / "run.json"
        if manifest.is_file() and read_json(manifest).get("status") == "success":
            return True
    return False


def next_attempt_directory(campaign: Path, entry: dict[str, Any]) -> Path:
    existing = attempts_for(campaign, entry)
    return (campaign / "runs" / entry["config_id"] /
            f"{entry['logical_run_id']}-attempt-{len(existing):02d}")


def execute_run(
    Ufo: Any,
    manager: Any,
    resources: Any,
    config: dict[str, Any],
    entry: dict[str, Any],
    run_dir: Path,
    memory_monitor: Any | None = None,
) -> dict[str, Any]:
    definition = algorithm_definition(config, entry["algorithm"])
    run_dir.mkdir(parents=True, exist_ok=False)
    sources = link_kernel_sources(run_dir, kernel_sources(config, definition))
    region = region_for_shape(int(entry["shape"]))
    manifest: dict[str, Any] = {
        **entry,
        "suite": config["suite"],
        "attempt": int(run_dir.name.rsplit("-", 1)[-1]),
        "status": "running",
        "started_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "plugin": definition["plugin"],
        "operation_mode": definition["mode"],
        "num_projections": int(config["num_projections"]),
        "projection_shape": config["projection_shape"],
        "region": region,
        "x_region": region,
        "y_region": region,
        "expected_outputs": expected_outputs(definition, int(entry["shape"])),
        "ufo_installation": config["ufo_installation"],
        "plugin_binary": {
            "path": str(
                Path(config["ufo_installation"]["plugindir"]) /
                f"libufofilter{definition['plugin']}.so"
            ),
            "sha256": sha256(
                Path(config["ufo_installation"]["plugindir"]) /
                f"libufofilter{definition['plugin']}.so"
            ),
        },
        "kernel_sources": sources,
    }
    write_json(run_dir / "run.json", manifest)

    try:
        graph, properties = construct_graph(Ufo, manager, config, entry)
        manifest["effective_properties"] = properties
        scheduler = Ufo.Scheduler()
        scheduler.set_resources(resources)
        scheduler.set_property("enable-tracing", True)

        if memory_monitor is not None:
            memory_monitor.begin_run()
        try:
            with working_directory(run_dir):
                start = time.monotonic()
                scheduler.run(graph)
                manifest["runner_wall_time_ms"] = (time.monotonic() - start) * 1000.0
        finally:
            if memory_monitor is not None:
                memory_report = memory_monitor.end_run()
                memory_filename = "dcgm-memory.json"
                write_json(run_dir / memory_filename, memory_report)
                manifest.update({
                    "dcgm_memory_file": memory_filename,
                    "baseline_device_memory_mib":
                        memory_report["baseline_device_memory_mib"],
                    "peak_device_memory_mib": memory_report["peak_device_memory_mib"],
                    "dcgm_memory_sample_count": memory_report["sample_count"],
                })
        opencl_files = list(run_dir.glob("opencl.*.json"))
        trace_files = list(run_dir.glob("trace.*.json"))
        if len(opencl_files) != 1 or len(trace_files) != 1:
            raise RuntimeError(
                f"expected one OpenCL and one task trace, found {len(opencl_files)} and {len(trace_files)}")
        manifest["trace_files"] = {
            "opencl": opencl_files[0].name,
            "task": trace_files[0].name,
        }
        metrics = analyze_traces(opencl_files[0], trace_files[0], config, entry)
        manifest.update({key: metrics[key] for key in (
            "total_profiled_kernel_ms", "backproject_task_active_ms", "output_completion_span_ms")})
        manifest["kernels"] = metrics["kernels"]
        manifest["stages_ms"] = metrics["stages_ms"]
        manifest["trace_counts"] = metrics["counts"]
        manifest["warnings"] = metrics["warnings"]
        manifest["status"] = "success"
    except Exception as exc:
        manifest["status"] = "failed"
        manifest["error"] = str(exc)
        manifest["traceback"] = traceback.format_exc()
    finally:
        manifest["finished_at_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
        write_json(run_dir / "run.json", manifest)
        gc.collect()
    return manifest


def median_mad(values: list[float]) -> tuple[float, float]:
    median = statistics.median(values)
    mad = statistics.median(abs(value - median) for value in values)
    return median, mad


def _write_csv(path: Path, rows: list[dict[str, Any]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def all_manifests(campaign: Path) -> list[dict[str, Any]]:
    manifests = []
    for path in campaign.glob("runs/*/*/run.json"):
        manifest = read_json(path)
        manifest["manifest_path"] = str(path.relative_to(campaign))
        manifests.append(manifest)
    return sorted(
        manifests,
        key=lambda item: (item.get("sequence", 10**9), item.get("attempt", 0), item["manifest_path"]),
    )


def latest_logical_manifests(manifests: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for manifest in manifests:
        path = manifest["manifest_path"]
        fallback_id = Path(path).parent.name
        key = (manifest.get("config_id", ""), manifest.get("logical_run_id", fallback_id))
        grouped.setdefault(key, []).append(manifest)
    selected = []
    for attempts in grouped.values():
        successes = [item for item in attempts if item.get("status") == "success"]
        selected.append(max(successes or attempts, key=lambda item: int(item.get("attempt", 0))))
    return sorted(selected, key=lambda item: (item.get("sequence", 10**9), item.get("attempt", 0)))


def aggregate_results(campaign: Path) -> dict[str, Any]:
    config = read_json(campaign / "resolved-config.json")
    manifests = all_manifests(campaign)
    selected_manifests = latest_logical_manifests(manifests)
    run_rows = []
    kernel_rows = []

    for item in manifests:
        row = {
            key: item.get(key) for key in (
                "suite", "config_id", "logical_run_id", "sequence", "algorithm", "plugin",
                "operation_mode", "shape", "burst", "warmup", "repetition", "pair_order",
                "attempt", "status",
                "total_profiled_kernel_ms", "backproject_task_active_ms",
                "output_completion_span_ms", "baseline_device_memory_mib",
                "peak_device_memory_mib",
                "dcgm_memory_sample_count", "dcgm_memory_file", "manifest_path", "error",
            )
        }
        run_rows.append(row)
        if item.get("status") == "success":
            for name, kernel in item.get("kernels", {}).items():
                kernel_rows.append({
                    "suite": item["suite"], "config_id": item["config_id"],
                    "logical_run_id": item["logical_run_id"], "algorithm": item["algorithm"],
                    "shape": item["shape"], "burst": item["burst"], "warmup": item["warmup"],
                    "kernel_name": name, "stage": kernel_stage(name), "call_count": kernel["count"],
                    "total_ms": kernel["total_ms"], "median_call_ms": kernel["median_call_ms"],
                })

    measured = [
        item for item in selected_manifests
        if item.get("status") == "success" and not item.get("warmup")
    ]
    algorithm_ids = [item["id"] for item in config["algorithms"]]
    expected_runs = int(config["measured_runs"])
    complete_blocks = set()
    incomplete = []
    for shape in map(int, config["shapes"]):
        for burst in map(int, config["bursts"]):
            counts = {
                algorithm: sum(
                    item["algorithm"] == algorithm and int(item["shape"]) == shape and
                    int(item["burst"]) == burst for item in measured)
                for algorithm in algorithm_ids
            }
            if all(value == expected_runs for value in counts.values()):
                complete_blocks.add((shape, burst))
            else:
                incomplete.append({"shape": shape, "burst": burst, "successful_runs": counts})

    summary_rows = []
    memory_summary_rows = []
    stage_rows = []
    kernel_summary_rows = []
    for shape, burst in sorted(complete_blocks):
        for algorithm in algorithm_ids:
            group = [
                item for item in measured if item["algorithm"] == algorithm and
                int(item["shape"]) == shape and int(item["burst"]) == burst
            ]
            for metric in SUMMARY_METRICS:
                values = [float(item[metric]) for item in group]
                median, mad = median_mad(values)
                summary_rows.append({
                    "suite": config["suite"], "shape": shape, "burst": burst,
                    "algorithm": algorithm, "metric": metric, "n": len(values),
                    "median_ms": median, "mad_ms": mad,
                })
            metric = "peak_device_memory_mib"
            if all(item.get(metric) is not None for item in group):
                values = [float(item[metric]) for item in group]
                median, mad = median_mad(values)
                memory_summary_rows.append({
                    "suite": config["suite"], "shape": shape, "burst": burst,
                    "algorithm": algorithm, "metric": metric, "n": len(values),
                    "median_mib": median, "mad_mib": mad,
                })
            stage_names = sorted({name for item in group for name in item.get("stages_ms", {})})
            for stage in stage_names:
                values = [float(item.get("stages_ms", {}).get(stage, 0.0)) for item in group]
                median, mad = median_mad(values)
                stage_rows.append({
                    "suite": config["suite"], "shape": shape, "burst": burst,
                    "algorithm": algorithm, "stage": stage, "n": len(values),
                    "median_ms": median, "mad_ms": mad,
                })
            kernel_names = sorted({name for item in group for name in item.get("kernels", {})})
            for name in kernel_names:
                values = [float(item.get("kernels", {}).get(name, {}).get("total_ms", 0.0)) for item in group]
                counts = [int(item.get("kernels", {}).get(name, {}).get("count", 0)) for item in group]
                median, mad = median_mad(values)
                kernel_summary_rows.append({
                    "suite": config["suite"], "shape": shape, "burst": burst,
                    "algorithm": algorithm, "kernel_name": name, "stage": kernel_stage(name),
                    "n": len(values), "median_total_ms": median, "mad_total_ms": mad,
                    "median_call_count": statistics.median(counts),
                })

    results = campaign / "results"
    _write_csv(results / "runs.csv", run_rows, list(run_rows[0]) if run_rows else ["status"])
    _write_csv(results / "kernels.csv", kernel_rows, list(kernel_rows[0]) if kernel_rows else ["kernel_name"])
    _write_csv(results / "summaries.csv", summary_rows, list(summary_rows[0]) if summary_rows else ["metric"])
    _write_csv(
        results / "memory-summaries.csv",
        memory_summary_rows,
        list(memory_summary_rows[0]) if memory_summary_rows else [
            "suite", "shape", "burst", "algorithm", "metric", "n",
            "median_mib", "mad_mib",
        ],
    )
    _write_csv(results / "stage-summaries.csv", stage_rows, list(stage_rows[0]) if stage_rows else ["stage"])
    _write_csv(results / "kernel-summaries.csv", kernel_summary_rows,
               list(kernel_summary_rows[0]) if kernel_summary_rows else ["kernel_name"])
    write_json(results / "incomplete-configurations.json", incomplete)
    return {
        "manifests": len(manifests),
        "complete_blocks": len(complete_blocks),
        "incomplete_blocks": len(incomplete),
        "memory_summary_rows": len(memory_summary_rows),
    }


def resume_warmup_entries(
    campaign: Path, config: dict[str, Any], schedule: dict[str, Any]
) -> list[dict[str, Any]]:
    measured = [entry for entry in schedule["planned_runs"] if not entry["warmup"]]
    pending_blocks = sorted({
        (entry["config_id"], int(entry["shape"]), int(entry["burst"]))
        for entry in measured if not successful_attempt(campaign, entry)
    })
    cycle = len(schedule.get("resume_warmups", []))
    entries = []
    for config_id, shape, burst in pending_blocks:
        algorithms = [item["id"] for item in config["algorithms"]]
        rng = random.Random(int(config["seed"]) + cycle + shape + burst)
        rng.shuffle(algorithms)
        for position, algorithm in enumerate(algorithms):
            entries.append({
                "sequence": -1,
                "logical_run_id": f"resume-{cycle:02d}-warmup-{algorithm}",
                "config_id": config_id,
                "shape": shape,
                "burst": burst,
                "algorithm": algorithm,
                "warmup": True,
                "resume_warmup": True,
                "repetition": cycle,
                "pair_order": position,
            })
    schedule.setdefault("resume_warmups", []).append({
        "cycle": cycle, "created_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "entries": entries,
    })
    write_json(campaign / "schedule.json", schedule)
    return entries


def run_campaign(
    config: dict[str, Any], schedule: dict[str, Any], resume: bool, make_plots: bool
) -> int:
    campaign = Path(config["campaign_dir"])
    resolve_ufo_installation(config)
    prepare_environment(config)
    plugin_names = {"read", "null", *(item["plugin"] for item in config["algorithms"])}
    for plugin_name in sorted(plugin_names):
        plugin = (Path(config["ufo_installation"]["plugindir"]) /
                  f"libufofilter{plugin_name}.so")
        if not plugin.is_file():
            raise FileNotFoundError(f"benchmark plugin not found: {plugin}")
    Ufo = import_ufo()
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()

    memory_monitor = None
    failures = 0
    try:
        if bool(config.get("dcgm_memory_enabled", False)):
            from dcgm_memory import DcgmMemoryMonitor
            memory_monitor = DcgmMemoryMonitor(
                host=str(config.get("dcgm_host", "localhost")),
                gpu_id=int(config.get("dcgm_gpu_id", config["device"])),
                sample_interval_ms=int(config.get("dcgm_sample_interval_ms", 50)),
                bindings_path=config.get("dcgm_bindings_path"),
            )

        if not resume:
            environment = collect_environment(
                config,
                Ufo,
                resources,
                campaign,
                memory_monitor.metadata if memory_monitor is not None else None,
            )
            write_json(campaign / "environment.json", environment)

        entries = schedule["planned_runs"]
        if resume:
            warmups = resume_warmup_entries(campaign, config, schedule)
            entries = warmups + [entry for entry in entries if not entry["warmup"]]

        blocked: set[tuple[str, str]] = set()
        for index, entry in enumerate(entries, start=1):
            key = (entry["config_id"], entry["algorithm"])
            if successful_attempt(campaign, entry):
                continue
            if key in blocked:
                continue
            run_dir = next_attempt_directory(campaign, entry)
            print(
                f"[{index}/{len(entries)}] {entry['config_id']} {entry['algorithm']} "
                f"{'warmup' if entry['warmup'] else 'run ' + str(entry['repetition'])}",
                flush=True,
            )
            manifest = execute_run(
                Ufo, manager, resources, config, entry, run_dir, memory_monitor)
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
            from plot_results import generate_plots
            generate_plots(campaign)
        except Exception as exc:
            print(f"Plot generation skipped: {exc}", file=sys.stderr)
    print(f"Campaign: {campaign}")
    print(json.dumps(summary, indent=2))
    return 1 if failures else 0


def common_argument_parser(description: str) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--config", type=Path, required=True)
    return parser


def runner_main(suite: str, description: str) -> int:
    parser = common_argument_parser(description)
    args = parser.parse_args()
    requested = load_configuration(args.config.expanduser().resolve(), suite)
    resume = bool(requested.get("resume", False))
    campaign = Path(requested["campaign_dir"])

    if resume:
        config = read_json(campaign / "resolved-config.json")
        if config.get("suite") != suite:
            parser.error(f"campaign suite is {config.get('suite')!r}, expected {suite!r}")
        for key in (
            "resume", "dry_run", "generate_plots", "scaling_plot_orientation",
            "dcgm_memory_enabled", "dcgm_sample_interval_ms", "dcgm_host",
            "dcgm_gpu_id", "dcgm_bindings_path",
        ):
            config[key] = requested[key]
        schedule = read_json(campaign / "schedule.json")
    else:
        config = requested
        schedule = make_schedule(config)

    if bool(config.get("dry_run", False)):
        dry_run_report(config, schedule)
        return 0

    resolve_ufo_installation(config)

    if not resume:
        if campaign.exists() and any(campaign.iterdir()):
            parser.error(f"new campaign directory is not empty: {campaign}")
        campaign.mkdir(parents=True, exist_ok=True)
        write_json(campaign / "resolved-config.json", config)
        write_json(campaign / "schedule.json", schedule)
    return run_campaign(config, schedule, resume, bool(config.get("generate_plots", True)))
