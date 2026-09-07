#!/usr/bin/env python3
"""Optional NVIDIA DCGM framebuffer-memory monitoring for benchmarks."""

from __future__ import annotations

import importlib
import math
import os
from pathlib import Path
import statistics
import sys
import time
from typing import Any


DCGM_FB_USED_FIELD_ID = 252


def _decode(value: Any) -> str:
    if isinstance(value, bytes):
        return value.split(b"\0", 1)[0].decode("utf-8", errors="replace")
    return str(value)


def discover_bindings_path(configured: str | None) -> Path:
    """Return a directory containing DCGM's Python bindings."""
    if configured:
        candidates = [Path(configured).expanduser()]
    else:
        candidates = []
        dcgm_home = os.environ.get("DCGM_HOME")
        if dcgm_home:
            candidates.append(Path(dcgm_home) / "bindings" / "python3")
        candidates.extend(sorted(Path("/usr/share").glob(
            "datacenter-gpu-manager-*/bindings/python3"), reverse=True))
        candidates.extend((
            Path("/usr/local/dcgm/bindings/python3"),
            Path("/usr/local/share/dcgm/bindings/python3"),
        ))

    for candidate in candidates:
        resolved = candidate.resolve()
        if (resolved / "pydcgm.py").is_file():
            return resolved
    if configured:
        raise RuntimeError(f"DCGM Python bindings not found in {configured}")
    raise RuntimeError(
        "DCGM Python bindings were not found; set dcgm_bindings_path explicitly")


def summarize_memory_samples(
    baseline: dict[str, Any], samples: list[dict[str, Any]]
) -> dict[str, Any]:
    """Validate one active window and calculate its peak memory statistics."""
    baseline_ts = int(baseline["timestamp_us"])
    baseline_value = float(baseline["value_mib"])
    if not math.isfinite(baseline_value) or baseline_value < 0.0:
        raise RuntimeError("DCGM returned an invalid pre-run framebuffer-memory value")

    active = []
    for sample in samples:
        timestamp = int(sample["timestamp_us"])
        value = float(sample["value_mib"])
        if timestamp <= baseline_ts or not math.isfinite(value) or value < 0.0:
            continue
        active.append({"timestamp_us": timestamp, "value_mib": value})
    active.sort(key=lambda item: item["timestamp_us"])
    if not active:
        raise RuntimeError("DCGM returned no valid framebuffer-memory samples during the run")

    peak = max([baseline_value, *(sample["value_mib"] for sample in active)])
    timestamps = [baseline_ts, *(sample["timestamp_us"] for sample in active)]
    intervals_ms = [
        (second - first) / 1000.0
        for first, second in zip(timestamps, timestamps[1:])
        if second > first
    ]
    return {
        "baseline_device_memory_mib": baseline_value,
        "peak_device_memory_mib": peak,
        "peak_device_memory_delta_mib": max(0.0, peak - baseline_value),
        "sample_count": len(active),
        "first_sample_timestamp_us": active[0]["timestamp_us"],
        "last_sample_timestamp_us": active[-1]["timestamp_us"],
        "observed_interval_ms": {
            "minimum": min(intervals_ms) if intervals_ms else None,
            "median": statistics.median(intervals_ms) if intervals_ms else None,
            "maximum": max(intervals_ms) if intervals_ms else None,
        },
        "baseline_sample": {
            "timestamp_us": baseline_ts,
            "value_mib": baseline_value,
        },
        "samples": active,
    }


class DcgmMemoryMonitor:
    """A campaign-scoped watch of one GPU's used framebuffer memory."""

    def __init__(
        self,
        host: str,
        gpu_id: int,
        sample_interval_ms: int,
        bindings_path: str | None = None,
    ) -> None:
        self._closed = False
        self._baseline: dict[str, Any] | None = None
        self._bindings_path = discover_bindings_path(bindings_path)
        if str(self._bindings_path) not in sys.path:
            sys.path.insert(0, str(self._bindings_path))

        try:
            self._pydcgm = importlib.import_module("pydcgm")
            self._dcgm_agent = importlib.import_module("dcgm_agent")
            self._dcgm_fields = importlib.import_module("dcgm_fields")
            self._field_helpers = importlib.import_module("dcgm_field_helpers")
        except Exception as exc:
            raise RuntimeError(f"failed to import DCGM Python bindings: {exc}") from exc

        self._host = host
        self._gpu_id = int(gpu_id)
        self._sample_interval_ms = int(sample_interval_ms)
        self._last_blank_count = 0
        self._prerun_blank_count = 0
        self._last_sample_timestamp_us = 0
        unique = f"ufo-bp-{os.getpid()}-{id(self):x}"
        self._handle = None
        self._group = None
        self._field_group = None
        self._collection = None

        try:
            if self._dcgm_fields.DCGM_FI_DEV_FB_USED != DCGM_FB_USED_FIELD_ID:
                raise RuntimeError(
                    "the DCGM bindings expose an unexpected framebuffer-used field ID")
            self._handle = self._pydcgm.DcgmHandle(ipAddress=host)
            self._system = self._handle.GetSystem()
            supported = list(self._system.discovery.GetAllSupportedGpuIds())
            if self._gpu_id not in supported:
                raise RuntimeError(
                    f"DCGM GPU {self._gpu_id} is unavailable; supported IDs are {supported}")

            attributes = self._system.discovery.GetGpuAttributes(self._gpu_id)
            identifiers = attributes.identifiers
            self._group = self._pydcgm.DcgmGroup(
                self._handle, groupName=f"{unique}-gpu")
            self._group.AddGpu(self._gpu_id)
            self._field_group = self._pydcgm.DcgmFieldGroup(
                self._handle,
                f"{unique}-fb-used",
                [self._dcgm_fields.DCGM_FI_DEV_FB_USED],
            )
            self._group.samples.WatchFields(
                self._field_group,
                self._sample_interval_ms * 1000,
                300.0,
                0,
            )
            self._collection = self._field_helpers.DcgmFieldValueCollection(
                self._handle.handle, self._group.GetId())
            initial, initial_blanks = self._force_valid_samples()
            self._last_sample_timestamp_us = initial[-1]["timestamp_us"]

            version = self._dcgm_agent.dcgmVersionInfo()
            self.metadata = {
                "enabled": True,
                "bindings_path": str(self._bindings_path),
                "bindings_module": str(Path(self._pydcgm.__file__).resolve()),
                "version": _decode(version.rawBuildInfoString),
                "host": host,
                "gpu_id": self._gpu_id,
                "gpu_uuid": _decode(identifiers.uuid),
                "pci_bus_id": _decode(identifiers.pciBusId),
                "device_name": _decode(identifiers.deviceName),
                "driver_version": _decode(identifiers.driverVersion),
                "field": "DCGM_FI_DEV_FB_USED",
                "field_id": int(self._dcgm_fields.DCGM_FI_DEV_FB_USED),
                "unit": "MiB",
                "sample_interval_ms": self._sample_interval_ms,
                "retention_seconds": 300.0,
                "discarded_initial_blank_samples": initial_blanks,
            }
        except Exception as exc:
            self.close()
            if isinstance(exc, RuntimeError):
                raise
            raise RuntimeError(
                f"failed to initialize DCGM memory monitoring through {host}: {exc}") from exc

    def _drain(self) -> list[dict[str, Any]]:
        assert self._collection is not None
        assert self._field_group is not None
        self._collection.GetAllSinceLastCall(self._field_group)
        series = self._collection.values.get(self._gpu_id, {}).get(
            self._dcgm_fields.DCGM_FI_DEV_FB_USED, [])
        samples = []
        blank_count = 0
        for value in series:
            if value.isBlank or value.value is None:
                blank_count += 1
                continue
            numeric = float(value.value)
            if not math.isfinite(numeric) or numeric < 0.0:
                self._collection.EmptyValues()
                raise RuntimeError("DCGM returned an invalid framebuffer-memory sample")
            samples.append({"timestamp_us": int(value.ts), "value_mib": numeric})
        self._collection.EmptyValues()
        self._last_blank_count = blank_count
        return sorted(samples, key=lambda item: item["timestamp_us"])

    def _force_valid_samples(
        self, after_timestamp_us: int = 0
    ) -> tuple[list[dict[str, Any]], int]:
        blank_count = 0
        for attempt in range(5):
            self._system.UpdateAllFields(1)
            samples = self._drain()
            blank_count += self._last_blank_count
            fresh = [
                sample for sample in samples
                if sample["timestamp_us"] > after_timestamp_us
            ]
            if fresh:
                return fresh, blank_count
            if attempt < 4:
                time.sleep(self._sample_interval_ms / 1000.0)
        raise RuntimeError(
            "DCGM returned no fresh, valid framebuffer-memory sample after five updates")

    def begin_run(self) -> dict[str, Any]:
        if self._closed:
            raise RuntimeError("DCGM memory monitor is closed")
        if self._baseline is not None:
            raise RuntimeError("DCGM memory window is already active")
        samples, blank_count = self._force_valid_samples(
            self._last_sample_timestamp_us)
        self._baseline = samples[-1]
        self._last_sample_timestamp_us = self._baseline["timestamp_us"]
        self._prerun_blank_count = blank_count
        return dict(self._baseline)

    def end_run(self) -> dict[str, Any]:
        if self._baseline is None:
            raise RuntimeError("DCGM memory window was not started")
        baseline = self._baseline
        self._baseline = None
        samples, blank_count = self._force_valid_samples(baseline["timestamp_us"])
        self._last_sample_timestamp_us = samples[-1]["timestamp_us"]
        report = summarize_memory_samples(baseline, samples)
        report["discarded_prerun_blank_samples"] = self._prerun_blank_count
        report["discarded_active_blank_samples"] = blank_count
        self._prerun_blank_count = 0
        return report

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._group is not None and self._field_group is not None:
            try:
                self._group.samples.UnwatchFields(self._field_group)
            except Exception:
                pass
        if self._field_group is not None:
            try:
                self._field_group.Delete()
            except Exception:
                pass
            self._field_group = None
        if self._group is not None:
            try:
                self._group.Delete()
            except Exception:
                pass
            self._group = None
        if self._handle is not None:
            try:
                self._handle.Shutdown()
            except Exception:
                pass
            self._handle = None

    def __enter__(self) -> "DcgmMemoryMonitor":
        return self

    def __exit__(self, exc_type: Any, exc: Any, traceback: Any) -> None:
        self.close()
