#!/usr/bin/env python3
"""Compare RGBA slice and volume outputs using a real projection dataset."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import math
import os
from pathlib import Path
import shutil
import sys
import tempfile
from typing import Any, Iterator


ROOT = Path(__file__).resolve().parents[2]


@contextmanager
def working_directory(path: Path) -> Iterator[None]:
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def positive_region(values: list[float], name: str) -> int:
    start, stop, step = values
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{name} values must be finite")
    if step <= 0.0 or stop <= start:
        raise ValueError(f"{name} requires stop > start and step > 0")
    return int(math.ceil((stop - start) / step))


def configure_environment(plugin_path: Path, device: int) -> None:
    current = os.environ.get("UFO_PLUGIN_PATH")
    os.environ["UFO_PLUGIN_PATH"] = (
        f"{plugin_path}{os.pathsep}{current}" if current else str(plugin_path)
    )
    os.environ["UFO_DEVICES"] = str(device)


def import_runtime() -> tuple[Any, Any, Any]:
    import gi
    gi.require_version("Ufo", "0.0")
    from gi.repository import Ufo
    import numpy as np
    import tifffile
    return Ufo, np, tifffile


def configure_backprojector(task: Any, args: argparse.Namespace, output_mode: str) -> None:
    properties = {
        "burst": args.burst,
        "num-projections": args.num_projections,
        "overall-angle": args.overall_angle,
        "center-position-x": [args.center_x],
        "center-position-z": [args.center_z],
        "region": args.region,
        "x-region": args.x_region,
        "y-region": args.y_region,
        "operation-mode": args.operation_mode,
        "output-mode": output_mode,
    }
    for name, value in properties.items():
        task.set_property(name, value)


def run_writer_graph(
    Ufo: Any,
    resources: Any,
    manager: Any,
    args: argparse.Namespace,
    output_mode: str,
    filename: Path,
) -> None:
    graph = Ufo.TaskGraph()
    reader = manager.get_task("read")
    backprojector = manager.get_task("rgba-backproject")
    writer = manager.get_task("write")
    reader.set_property("path", str(args.input))
    configure_backprojector(backprojector, args, output_mode)
    writer.set_property("filename", str(filename))
    writer.set_property("rescale", False)
    graph.connect_nodes(reader, backprojector)
    if output_mode == "volume":
        slicer = manager.get_task("slice")
        graph.connect_nodes(backprojector, slicer)
        graph.connect_nodes(slicer, writer)
    else:
        graph.connect_nodes(backprojector, writer)
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.run(graph)


def run_fft_smoke(
    Ufo: Any,
    resources: Any,
    manager: Any,
    args: argparse.Namespace,
) -> None:
    graph = Ufo.TaskGraph()
    reader = manager.get_task("read")
    backprojector = manager.get_task("rgba-backproject")
    fft = manager.get_task("fft")
    sink = manager.get_task("null")
    reader.set_property("path", str(args.input))
    configure_backprojector(backprojector, args, "volume")
    fft.set_property("dimensions", 3)
    sink.set_property("download", False)
    sink.set_property("finish", True)
    sink.set_property("durations", False)
    graph.connect_nodes(reader, backprojector)
    graph.connect_nodes(backprojector, fft)
    graph.connect_nodes(fft, sink)
    scheduler = Ufo.Scheduler()
    scheduler.set_resources(resources)
    scheduler.run(graph)


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--num-projections", type=int, required=True)
    parser.add_argument("--burst", type=int, default=16)
    parser.add_argument("--overall-angle", type=float, default=math.pi)
    parser.add_argument("--center-x", type=float, required=True)
    parser.add_argument("--center-z", type=float, required=True)
    parser.add_argument("--region", type=float, nargs=3, required=True,
                        metavar=("FROM", "TO", "STEP"))
    parser.add_argument("--x-region", type=float, nargs=3, required=True,
                        metavar=("FROM", "TO", "STEP"))
    parser.add_argument("--y-region", type=float, nargs=3, required=True,
                        metavar=("FROM", "TO", "STEP"))
    parser.add_argument(
        "--operation-mode",
        choices=("singular", "even_odd_single", "even_odd_dual"),
        default="singular",
    )
    parser.add_argument("--rtol", type=float, default=1e-5)
    parser.add_argument("--atol", type=float, default=1e-4)
    parser.add_argument("--fft-smoke", action="store_true")
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--plugin-path", type=Path, default=ROOT / "build" / "src")
    parser.add_argument("--kernel-dir", type=Path, default=ROOT / "src" / "kernels")
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="retain generated TIFF files in this directory; otherwise use a temporary directory",
    )
    return parser


def main() -> int:
    args = argument_parser().parse_args()
    args.input = args.input.expanduser().resolve()
    args.plugin_path = args.plugin_path.expanduser().resolve()
    args.kernel_dir = args.kernel_dir.expanduser().resolve()
    if not args.input.is_file():
        raise FileNotFoundError(args.input)
    if not args.plugin_path.is_dir():
        raise FileNotFoundError(args.plugin_path)
    if not (args.kernel_dir / "rgba-backproject.cl").is_file():
        raise FileNotFoundError(args.kernel_dir / "rgba-backproject.cl")
    if args.num_projections <= 0 or args.burst <= 0:
        raise ValueError("num-projections and burst must be positive")
    if args.rtol < 0.0 or args.atol < 0.0:
        raise ValueError("rtol and atol must be non-negative")

    width = positive_region(args.x_region, "x-region")
    height = positive_region(args.y_region, "y-region")
    depth = positive_region(args.region, "region")
    if depth < 5:
        raise ValueError(
            "select at least five z slices to exercise RGBA padding and avoid three-plane RGB TIFF")
    output_volumes = 1 if args.operation_mode == "singular" else 2
    expected_shape = (output_volumes * depth, height, width)

    configure_environment(args.plugin_path, args.device)
    Ufo, np, tifffile = import_runtime()
    resources = Ufo.Resources.new()
    manager = Ufo.PluginManager()

    temporary = None
    if args.output_dir is None:
        temporary = tempfile.mkdtemp(prefix="rgba-output-mode-")
        output_dir = Path(temporary)
    else:
        output_dir = args.output_dir.expanduser().resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
    slices_path = output_dir / "rgba-slices.tif"
    volume_path = output_dir / "rgba-volume.tif"

    try:
        with working_directory(args.kernel_dir):
            run_writer_graph(Ufo, resources, manager, args, "slices", slices_path)
            run_writer_graph(Ufo, resources, manager, args, "volume", volume_path)
            if args.fft_smoke:
                run_fft_smoke(Ufo, resources, manager, args)

        slices = tifffile.imread(slices_path)
        volume = tifffile.imread(volume_path)
        if slices.shape != expected_shape:
            raise RuntimeError(f"slice output shape is {slices.shape}, expected {expected_shape}")
        if volume.shape != expected_shape:
            raise RuntimeError(f"volume output shape is {volume.shape}, expected {expected_shape}")
        if not np.isfinite(slices).all() or not np.isfinite(volume).all():
            raise RuntimeError("one or both reconstructions contain non-finite values")

        difference = np.abs(slices.astype(np.float64) - volume.astype(np.float64))
        maximum = float(difference.max())
        close = np.isclose(slices, volume, rtol=args.rtol, atol=args.atol)
        print(f"operation mode: {args.operation_mode}")
        print(f"slice output shape:  {slices.shape}")
        print(f"volume output shape: {volume.shape}")
        print(f"maximum absolute difference: {maximum:.9g}")
        print(f"tolerances: rtol={args.rtol:g}, atol={args.atol:g}")
        if not bool(close.all()):
            mismatch = tuple(int(index) for index in np.argwhere(~close)[0])
            print(
                f"first mismatch at {mismatch}: slices={slices[mismatch]!r}, "
                f"volume={volume[mismatch]!r}",
                file=sys.stderr,
            )
            return 1
        print("slice and volume outputs agree")
        if args.fft_smoke:
            print("device-resident 3-D FFT smoke graph completed")
        if args.output_dir is not None:
            print(f"outputs retained in {output_dir}")
        return 0
    finally:
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
