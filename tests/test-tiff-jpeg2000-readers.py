#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import tifffile


SHAPE = (10, 256, 256)
TILE = (128, 128)


def write_input(path, data, compression=None, compressionargs=None, tile=None):
    kwargs = {}

    if compression is not None:
        kwargs["compression"] = compression

    if compressionargs is not None:
        kwargs["compressionargs"] = compressionargs

    if tile is not None:
        kwargs["tile"] = tile

    tifffile.imwrite(path, data, **kwargs)


def read_with_ufo(input_path, output_path):
    env = os.environ.copy()
    subprocess.run(
        [
            "ufo-launch",
            "-q",
            "read",
            f"path={input_path}",
            "!",
            "write",
            f"filename={output_path}",
            "tiff-bigtiff=False",
        ],
        env=env,
        check=True,
    )


def write_tiled_jpeg2000_with_ufo(input_path, output_path):
    env = os.environ.copy()
    subprocess.run(
        [
            "ufo-launch",
            "-q",
            "read",
            f"path={input_path}",
            "!",
            "write",
            "bits=16",
            "rescale=False",
            f"filename={output_path}",
            "tiff-jpeg2000=True",
            "tile-size=128",
        ],
        env=env,
        check=True,
    )


def assert_equal_to_tifffile(input_path, output_path, name):
    expected = tifffile.imread(input_path).astype(np.float32)
    actual = tifffile.imread(output_path)

    if expected.shape != actual.shape:
        raise AssertionError(f"{name}: shape mismatch: {expected.shape} != {actual.shape}")

    diff = actual - expected
    max_abs = np.max(np.abs(diff))

    if max_abs != 0:
        index = np.unravel_index(np.argmax(np.abs(diff)), diff.shape)
        raise AssertionError(
            f"{name}: non-zero pixel difference at {index}: "
            f"actual={actual[index]}, expected={expected[index]}, diff={diff[index]}"
        )


def main():
    rng = np.random.default_rng(12345)
    data = rng.integers(0, np.iinfo(np.uint16).max + 1, size=SHAPE, dtype=np.uint16)

    cases = [
        ("plain", {}, None),
        ("jpeg2000-lossless", {"compression": "jpeg2000"}, None),
        ("jpeg2000-lossy-level80", {"compression": "jpeg2000", "compressionargs": {"level": 80}}, None),
        ("plain-tiled", {}, TILE),
        ("jpeg2000-lossless-tiled", {"compression": "jpeg2000"}, TILE),
        ("jpeg2000-lossy-level80-tiled", {"compression": "jpeg2000", "compressionargs": {"level": 80}}, TILE),
    ]

    with tempfile.TemporaryDirectory(prefix="ufo-tiff-jpeg2000-readers-") as tmp:
        tmp_path = Path(tmp)

        for name, options, tile in cases:
            input_path = tmp_path / f"{name}.tif"
            output_path = tmp_path / f"{name}-ufo.tif"

            write_input(input_path, data, tile=tile, **options)
            read_with_ufo(input_path, output_path)
            assert_equal_to_tifffile(input_path, output_path, name)

        input_path = tmp_path / "ufo-tiled-writer-source.tif"
        compressed_path = tmp_path / "ufo-tiled-writer-compressed.tif"
        output_path = tmp_path / "ufo-tiled-writer-readback.tif"

        write_input(input_path, data)
        write_tiled_jpeg2000_with_ufo(input_path, compressed_path)

        page = tifffile.TiffFile(compressed_path).pages[0]
        if not page.is_tiled:
            raise AssertionError("ufo-tiled-writer: output is not tiled")

        if page.tilewidth != 128 or page.tilelength != 128:
            raise AssertionError(
                f"ufo-tiled-writer: unexpected tile size {page.tilewidth}x{page.tilelength}"
            )

        if page.compression.value != 34712:
            raise AssertionError(f"ufo-tiled-writer: unexpected compression {page.compression}")

        read_with_ufo(compressed_path, output_path)
        assert_equal_to_tifffile(input_path, output_path, "ufo-tiled-writer")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)
