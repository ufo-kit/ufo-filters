"""Typed FSC results and an independent NumPy shell-statistics reference."""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Sequence

import numpy as np


@dataclass(frozen=True)
class ShellGeometry:
    """Resolved spherical-shell geometry in reciprocal voxel-size units."""

    shell_width: float
    max_frequency: float
    number_of_bins: int


@dataclass(frozen=True)
class FSCShellStatistics:
    """Compact sufficient statistics emitted by the UFO ``fsc-core`` task."""

    cross_sum: np.ndarray
    power_1_sum: np.ndarray
    power_2_sum: np.ndarray
    n_shell: np.ndarray
    k_bin: np.ndarray

    @classmethod
    def from_ufo(cls, values: np.ndarray) -> "FSCShellStatistics":
        array = np.asarray(values)
        if array.ndim != 2 or array.shape[0] != 5:
            raise ValueError(
                f"fsc-core output must have shape (5, B), received {array.shape}"
            )
        if not np.isfinite(array[:4]).all() or not np.isfinite(array[4]).all():
            raise ValueError("fsc-core emitted non-finite shell statistics")

        counts = np.rint(array[3]).astype(np.int64)
        if np.any(counts < 0) or not np.array_equal(
            counts.astype(np.float32), array[3].astype(np.float32)
        ):
            raise ValueError("fsc-core emitted invalid shell counts")

        return cls(
            cross_sum=np.asarray(array[0], dtype=np.float32).copy(),
            power_1_sum=np.asarray(array[1], dtype=np.float32).copy(),
            power_2_sum=np.asarray(array[2], dtype=np.float32).copy(),
            n_shell=counts,
            k_bin=np.asarray(array[4], dtype=np.float32).copy(),
        )

    def normalized(self) -> "FSCResult":
        cross = self.cross_sum.astype(np.float64)
        power_1 = self.power_1_sum.astype(np.float64)
        power_2 = self.power_2_sum.astype(np.float64)
        denominator = np.sqrt(power_1 * power_2)
        valid = (self.n_shell > 0) & (power_1 > 0.0) & (power_2 > 0.0)
        fsc = np.full(cross.shape, np.nan, dtype=np.float64)
        np.divide(cross, denominator, out=fsc, where=valid)
        return FSCResult(
            fsc=fsc.astype(np.float32),
            k_bin=self.k_bin.copy(),
            n_shell=self.n_shell.copy(),
        )


@dataclass(frozen=True)
class FSCResult:
    """Public classic-FSC result."""

    fsc: np.ndarray
    k_bin: np.ndarray
    n_shell: np.ndarray


def resolve_shell_geometry(
    shape_zyx: Sequence[int],
    voxel_size_xyz: Sequence[float],
    shell_width: float = 0.0,
    max_frequency: float = 0.0,
) -> ShellGeometry:
    """Resolve the same conservative physical shell policy as ``fsc-core``."""
    if len(shape_zyx) != 3 or len(voxel_size_xyz) != 3:
        raise ValueError("shape and voxel size must each contain three values")

    nz, ny, nx = (int(value) for value in shape_zyx)
    dx, dy, dz = (float(value) for value in voxel_size_xyz)
    if min(nx, ny, nz) <= 0:
        raise ValueError("all volume dimensions must be positive")
    if not all(math.isfinite(value) and value > 0.0 for value in (dx, dy, dz)):
        raise ValueError("all voxel sizes must be finite and positive")
    if not math.isfinite(shell_width) or shell_width < 0.0:
        raise ValueError("shell_width must be finite and non-negative")
    if not math.isfinite(max_frequency) or max_frequency < 0.0:
        raise ValueError("max_frequency must be finite and non-negative")

    increments = (1.0 / (nx * dx), 1.0 / (ny * dy), 1.0 / (nz * dz))
    resolved_width = shell_width or max(increments)
    resolved_max = max_frequency or min(1.0 / (2.0 * dx),
                                         1.0 / (2.0 * dy),
                                         1.0 / (2.0 * dz))

    # fsc-core passes the resolved width to OpenCL as float32 before deriving
    # and emitting bin centres, so mirror that public behavior here.
    resolved_width_f32 = float(np.float32(resolved_width))
    ratio = resolved_max / resolved_width_f32
    tolerance = 16.0 * np.finfo(np.float64).eps * max(1.0, abs(ratio))
    number_of_bins = math.floor(ratio + tolerance)
    if number_of_bins <= 0:
        raise ValueError("shell configuration resolves to no bins")

    return ShellGeometry(resolved_width_f32, resolved_max, number_of_bins)


def reference_shell_statistics(
    first: np.ndarray,
    second: np.ndarray,
    voxel_size_xyz: Sequence[float],
    shell_width: float = 0.0,
    max_frequency: float = 0.0,
) -> FSCShellStatistics:
    """Compute shell statistics directly with NumPy for validation."""
    first = np.asarray(first, dtype=np.complex64)
    second = np.asarray(second, dtype=np.complex64)
    if first.ndim != 3 or second.shape != first.shape:
        raise ValueError("both spectra must have the same three-dimensional shape")

    geometry = resolve_shell_geometry(
        first.shape, voxel_size_xyz, shell_width, max_frequency
    )
    nz, ny, nx = first.shape
    dx, dy, dz = (float(value) for value in voxel_size_xyz)
    kx = np.fft.fftfreq(nx, d=dx).astype(np.float32)
    ky = np.fft.fftfreq(ny, d=dy).astype(np.float32)
    kz = np.fft.fftfreq(nz, d=dz).astype(np.float32)
    radius = np.sqrt(
        kz[:, None, None] ** 2
        + ky[None, :, None] ** 2
        + kx[None, None, :] ** 2,
        dtype=np.float32,
    )
    bins = np.floor(radius / np.float32(geometry.shell_width) + np.float32(0.5))
    bins = bins.astype(np.int64)
    valid = bins < geometry.number_of_bins
    labels = bins[valid].ravel()

    first_valid = first[valid]
    second_valid = second[valid]
    cross = np.real(first_valid * np.conjugate(second_valid)).astype(np.float64)
    power_1 = np.abs(first_valid).astype(np.float64) ** 2
    power_2 = np.abs(second_valid).astype(np.float64) ** 2
    count = geometry.number_of_bins

    return FSCShellStatistics(
        cross_sum=np.bincount(labels, weights=cross, minlength=count),
        power_1_sum=np.bincount(labels, weights=power_1, minlength=count),
        power_2_sum=np.bincount(labels, weights=power_2, minlength=count),
        n_shell=np.bincount(labels, minlength=count).astype(np.int64),
        k_bin=(np.arange(count, dtype=np.float32)
               * np.float32(geometry.shell_width)),
    )
