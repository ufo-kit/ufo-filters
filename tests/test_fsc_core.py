#!/usr/bin/env python3
"""Numerical and stream-contract tests for the fsc-core task."""

from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
import sys
from typing import Iterator, Sequence

import gi
import numpy as np

gi.require_version("Ufo", "0.0")
from gi.repository import GLib, Ufo
import ufo.numpy


def find_root() -> Path:
    path = Path(__file__).resolve()
    for candidate in (path.parents[1], path.parents[2], Path.cwd().parent):
        if (candidate / "src/kernels/fsc-core.cl").is_file():
            return candidate
    raise RuntimeError("cannot locate the ufo-filters source tree")


ROOT = find_root()
sys.path.insert(0, str(ROOT / "benchmarks/fsc"))
from fsc_utils import FSCShellStatistics, reference_shell_statistics


@contextmanager
def working_directory(path: Path) -> Iterator[None]:
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def run_spectra(
    spectra: Sequence[np.ndarray],
    voxel_size_xyz: Sequence[float] = (1.0, 1.0, 1.0),
    shell_width: float = 0.0,
    max_frequency: float = 0.0,
) -> list[np.ndarray]:
    first_shape = np.asarray(spectra[0]).shape
    if any(np.asarray(spectrum).shape != first_shape for spectrum in spectra):
        raise ValueError("test input spectra must have one shape")
    nz, ny, nx = first_shape
    source = np.ascontiguousarray(np.stack(spectra).reshape(-1, ny, nx), dtype=np.complex64)
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()
    graph = Ufo.TaskGraph()
    memory = manager.get_task("memory-in")
    stack = manager.get_task("stack")
    fsc = manager.get_task("fsc-core")
    output = Ufo.OutputTask()

    memory.set_property("width", 2 * nx)
    memory.set_property("height", ny)
    memory.set_property("number", len(spectra) * nz)
    memory.set_property("bitdepth", 32)
    memory.set_property("complex-layout", True)
    memory.set_property("pointer", source.__array_interface__["data"][0])
    stack.set_property("number", nz)
    fsc.set_property("voxel-size-x", float(voxel_size_xyz[0]))
    fsc.set_property("voxel-size-y", float(voxel_size_xyz[1]))
    fsc.set_property("voxel-size-z", float(voxel_size_xyz[2]))
    fsc.set_property("shell-width", shell_width)
    fsc.set_property("max-frequency", max_frequency)
    output.set_property("num-dims", 2)

    graph.connect_nodes(memory, stack)
    graph.connect_nodes(stack, fsc)
    graph.connect_nodes(fsc, output)
    results: list[np.ndarray] = []

    def processed(task: Ufo.OutputTask) -> None:
        buffer = task.get_output_buffer()
        try:
            results.append(ufo.numpy.asarray(buffer).copy())
        finally:
            task.release_output_buffer(buffer)

    output.connect("processed", processed)
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.set_property("expand", False)
    with working_directory(ROOT / "src/kernels"):
        scheduler.run(graph)
    return results


def run_classic_reconstruction(
    projections: np.ndarray,
    operation_mode: str,
) -> FSCShellStatistics:
    projections = np.ascontiguousarray(projections, dtype=np.float32)
    num_projections, height, width = projections.shape
    if height != width:
        raise ValueError("the compact end-to-end fixture expects square projections")

    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()
    graph = Ufo.TaskGraph()
    memory = manager.get_task("memory-in")
    backprojector = manager.get_task("rgba-backproject")
    fft = manager.get_task("fft")
    fsc = manager.get_task("fsc-core")
    output = Ufo.OutputTask()
    memory.set_property("width", width)
    memory.set_property("height", height)
    memory.set_property("number", num_projections)
    memory.set_property("bitdepth", 32)
    memory.set_property("pointer", projections.__array_interface__["data"][0])
    backprojector.set_property("burst", 2)
    backprojector.set_property("num-projections", num_projections)
    backprojector.set_property("overall-angle", np.pi)
    backprojector.set_property("center-position-x", [width / 2.0])
    backprojector.set_property("center-position-z", [height / 2.0])
    for name in ("region", "x-region", "y-region"):
        backprojector.set_property(name, [-width / 2.0, width / 2.0, 1.0])
    backprojector.set_property("operation-mode", operation_mode)
    backprojector.set_property("output-mode", "volume")
    fft.set_property("dimensions", 3)
    fft.set_property("auto-zeropadding", False)
    fft.set_property("size-x", width)
    fft.set_property("size-y", width)
    fft.set_property("size-z", width)
    fsc.set_property("voxel-size-x", 1.0)
    fsc.set_property("voxel-size-y", 1.0)
    fsc.set_property("voxel-size-z", 1.0)
    output.set_property("num-dims", 2)
    graph.connect_nodes(memory, backprojector)
    graph.connect_nodes(backprojector, fft)
    graph.connect_nodes(fft, fsc)
    graph.connect_nodes(fsc, output)
    results: list[np.ndarray] = []

    def processed(task: Ufo.OutputTask) -> None:
        buffer = task.get_output_buffer()
        try:
            results.append(ufo.numpy.asarray(buffer).copy())
        finally:
            task.release_output_buffer(buffer)

    output.connect("processed", processed)
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.set_property("expand", False)
    with working_directory(ROOT / "src/kernels"):
        scheduler.run(graph)
    if len(results) != 1:
        raise AssertionError(
            f"classic {operation_mode} graph emitted {len(results)} results"
        )
    return FSCShellStatistics.from_ufo(results[0])


def assert_statistics_close(
    actual_values: np.ndarray,
    expected: FSCShellStatistics,
) -> FSCShellStatistics:
    actual = FSCShellStatistics.from_ufo(actual_values)
    np.testing.assert_array_equal(actual.n_shell, expected.n_shell)
    np.testing.assert_allclose(actual.k_bin, expected.k_bin, rtol=1e-6, atol=1e-7)
    np.testing.assert_allclose(actual.cross_sum, expected.cross_sum, rtol=2e-3, atol=2e-4)
    np.testing.assert_allclose(actual.power_1_sum, expected.power_1_sum, rtol=2e-3, atol=2e-4)
    np.testing.assert_allclose(actual.power_2_sum, expected.power_2_sum, rtol=2e-3, atol=2e-4)
    np.testing.assert_allclose(
        actual.normalized().fsc,
        expected.normalized().fsc,
        rtol=2e-3,
        atol=2e-4,
        equal_nan=True,
    )
    return actual


def test_cubic_pairs() -> None:
    rng = np.random.default_rng(20260904)
    first = (rng.normal(size=(8, 8, 8))
             + 1j * rng.normal(size=(8, 8, 8))).astype(np.complex64)
    second = (rng.normal(size=(8, 8, 8))
              + 1j * rng.normal(size=(8, 8, 8))).astype(np.complex64)
    expected = reference_shell_statistics(first, second, (1.0, 1.0, 1.0))
    results = run_spectra((first, second))
    if len(results) != 1:
        raise AssertionError(f"expected one result, received {len(results)}")
    assert_statistics_close(results[0], expected)


def test_anisotropic_grid() -> None:
    rng = np.random.default_rng(9)
    shape = (8, 10, 12)
    voxel = (0.7, 1.1, 0.9)
    first = (rng.normal(size=shape) + 1j * rng.normal(size=shape)).astype(np.complex64)
    second = (rng.normal(size=shape) + 1j * rng.normal(size=shape)).astype(np.complex64)
    expected = reference_shell_statistics(first, second, voxel, 0.08, 0.4)
    result = run_spectra((first, second), voxel, 0.08, 0.4)
    assert_statistics_close(result[0], expected)


def test_repeated_pairs_and_normalization() -> None:
    rng = np.random.default_rng(17)
    first = (rng.normal(size=(8, 8, 8))
             + 1j * rng.normal(size=(8, 8, 8))).astype(np.complex64)
    results = run_spectra((first, first, first, -first))
    if len(results) != 2:
        raise AssertionError(f"expected two results, received {len(results)}")

    positive = FSCShellStatistics.from_ufo(results[0]).normalized().fsc
    negative = FSCShellStatistics.from_ufo(results[1]).normalized().fsc
    np.testing.assert_allclose(positive, 1.0, rtol=1e-5, atol=1e-5)
    np.testing.assert_allclose(negative, -1.0, rtol=1e-5, atol=1e-5)


def test_zero_denominator() -> None:
    zero = np.zeros((8, 8, 8), dtype=np.complex64)
    result = FSCShellStatistics.from_ufo(run_spectra((zero, zero))[0]).normalized()
    if not np.isnan(result.fsc).all():
        raise AssertionError("zero-power shells must normalize to NaN")


def test_incomplete_pair() -> None:
    one = np.ones((8, 8, 8), dtype=np.complex64)
    if run_spectra((one,)):
        raise AssertionError("an incomplete pair must not emit a result")


def test_classic_parity_modes() -> None:
    rng = np.random.default_rng(31)
    projections = rng.uniform(0.1, 1.0, size=(8, 8, 8)).astype(np.float32)
    single = run_classic_reconstruction(projections, "even_odd_single")
    dual = run_classic_reconstruction(projections, "even_odd_dual")
    np.testing.assert_array_equal(single.n_shell, dual.n_shell)
    np.testing.assert_allclose(single.k_bin, dual.k_bin, rtol=0.0, atol=0.0)
    np.testing.assert_allclose(
        single.normalized().fsc,
        dual.normalized().fsc,
        rtol=2e-3,
        atol=2e-4,
        equal_nan=True,
    )


def test_rejects_non_3d_input() -> None:
    source = np.ones((8, 8), dtype=np.complex64)
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()
    graph = Ufo.TaskGraph()
    memory = manager.get_task("memory-in")
    fsc = manager.get_task("fsc-core")
    sink = manager.get_task("null")
    memory.set_property("width", 16)
    memory.set_property("height", 8)
    memory.set_property("number", 1)
    memory.set_property("bitdepth", 32)
    memory.set_property("complex-layout", True)
    memory.set_property("pointer", source.__array_interface__["data"][0])
    fsc.set_property("voxel-size-x", 1.0)
    fsc.set_property("voxel-size-y", 1.0)
    fsc.set_property("voxel-size-z", 1.0)
    graph.connect_nodes(memory, fsc)
    graph.connect_nodes(fsc, sink)
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.set_property("expand", False)
    try:
        with working_directory(ROOT / "src/kernels"):
            scheduler.run(graph)
    except GLib.GError:
        return
    raise AssertionError("fsc-core accepted a two-dimensional input")


def test_rejects_real_input() -> None:
    source = np.ones((8, 8, 8), dtype=np.float32)
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()
    graph = Ufo.TaskGraph()
    memory = manager.get_task("memory-in")
    stack = manager.get_task("stack")
    fsc = manager.get_task("fsc-core")
    sink = manager.get_task("null")
    memory.set_property("width", 8)
    memory.set_property("height", 8)
    memory.set_property("number", 8)
    memory.set_property("bitdepth", 32)
    memory.set_property("complex-layout", False)
    memory.set_property("pointer", source.__array_interface__["data"][0])
    stack.set_property("number", 8)
    fsc.set_property("voxel-size-x", 1.0)
    fsc.set_property("voxel-size-y", 1.0)
    fsc.set_property("voxel-size-z", 1.0)
    graph.connect_nodes(memory, stack)
    graph.connect_nodes(stack, fsc)
    graph.connect_nodes(fsc, sink)
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.set_property("expand", False)
    try:
        with working_directory(ROOT / "src/kernels"):
            scheduler.run(graph)
    except GLib.GError:
        return
    raise AssertionError("fsc-core accepted a real-valued input")


def test_rejects_copy() -> None:
    manager = Ufo.PluginManager()
    fsc = manager.get_task("fsc-core")
    try:
        fsc.copy()
    except GLib.GError as error:
        if "disable graph expansion" not in str(error):
            raise AssertionError(f"unexpected fsc-core copy error: {error}") from error
        return
    raise AssertionError("fsc-core permitted copying its pair state")


def test_reference_geometry() -> None:
    one = np.ones((8, 8, 8), dtype=np.complex64)
    statistics = reference_shell_statistics(one, one, (1.0, 1.0, 1.0))
    np.testing.assert_array_equal(statistics.n_shell, [1, 18, 62, 98])
    np.testing.assert_allclose(statistics.k_bin, [0.0, 0.125, 0.25, 0.375])
    np.testing.assert_allclose(statistics.normalized().fsc, 1.0)


def main() -> int:
    test_reference_geometry()
    test_rejects_copy()
    try:
        Ufo.Resources.new()
    except GLib.GError as error:
        print(f"OpenCL is unavailable, skipping fsc-core runtime tests: {error}")
        return 77
    test_cubic_pairs()
    test_anisotropic_grid()
    test_repeated_pairs_and_normalization()
    test_zero_denominator()
    test_incomplete_pair()
    test_classic_parity_modes()
    test_rejects_non_3d_input()
    test_rejects_real_input()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
