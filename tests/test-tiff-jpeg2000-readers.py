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


def read_with_ufo(input_path, output_path, threads=2):
    env = os.environ.copy()
    subprocess.run(
        [
            "ufo-launch",
            "-q",
            "read",
            f"path={input_path}",
            f"jpeg2000-threads={threads}",
            "!",
            "write",
            f"filename={output_path}",
            "tiff-bigtiff=False",
        ],
        env=env,
        check=True,
    )


def write_jpeg2000_with_ufo(
    input_path,
    output_path,
    bits=16,
    threads=2,
    tile_size=0,
    append=False,
    stack_number=None,
):
    env = os.environ.copy()
    command = [
        "ufo-launch",
        "-q",
        "read",
        f"path={input_path}",
    ]

    if stack_number is not None:
        command.extend(["!", "stack", f"number={stack_number}"])

    command.extend(
        [
            "!",
            "write",
            f"bits={bits}",
            "rescale=False",
            f"filename={output_path}",
            "tiff-jpeg2000=True",
            f"tile-size={tile_size}",
            f"jpeg2000-threads={threads}",
            f"append={append}",
        ]
    )

    subprocess.run(
        command,
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


def assert_jpeg2000_tiff(path, pages, tiled=False, tile_size=None, bits=None, samples=1):
    with tifffile.TiffFile(path) as tif:
        if len(tif.pages) != pages:
            raise AssertionError(f"{path.name}: expected {pages} pages, got {len(tif.pages)}")

        for page in tif.pages:
            if page.compression.value != 34712:
                raise AssertionError(f"{path.name}: unexpected compression {page.compression}")

            if page.is_tiled != tiled:
                raise AssertionError(f"{path.name}: unexpected tiled state {page.is_tiled}")

            if tile_size is not None:
                if page.tilewidth != tile_size or page.tilelength != tile_size:
                    raise AssertionError(
                        f"{path.name}: unexpected tile size "
                        f"{page.tilewidth}x{page.tilelength}"
                    )

            if bits is not None and page.bitspersample != bits:
                raise AssertionError(
                    f"{path.name}: expected {bits} bits, got {page.bitspersample}"
                )

            if page.samplesperpixel != samples:
                raise AssertionError(
                    f"{path.name}: expected {samples} samples, got {page.samplesperpixel}"
                )


def main():
    rng = np.random.default_rng(12345)
    data = rng.integers(0, np.iinfo(np.uint16).max + 1, size=SHAPE, dtype=np.uint16)

    cases = [
        ("plain", {}, None, 2),
        ("jpeg2000-lossless", {"compression": "jpeg2000"}, None, 2),
        ("jpeg2000-lossless-threads-all", {"compression": "jpeg2000"}, None, 0),
        ("jpeg2000-lossless-threads-one", {"compression": "jpeg2000"}, None, 1),
        ("jpeg2000-lossy-level80", {"compression": "jpeg2000", "compressionargs": {"level": 80}}, None, 2),
        ("plain-tiled", {}, TILE, 2),
        ("jpeg2000-lossless-tiled", {"compression": "jpeg2000"}, TILE, 2),
        ("jpeg2000-lossy-level80-tiled", {"compression": "jpeg2000", "compressionargs": {"level": 80}}, TILE, 2),
    ]

    with tempfile.TemporaryDirectory(prefix="ufo-tiff-jpeg2000-readers-") as tmp:
        tmp_path = Path(tmp)

        for name, options, tile, threads in cases:
            input_path = tmp_path / f"{name}.tif"
            output_path = tmp_path / f"{name}-ufo.tif"

            write_input(input_path, data, tile=tile, **options)
            read_with_ufo(input_path, output_path, threads=threads)
            assert_equal_to_tifffile(input_path, output_path, name)

        for bits, dtype in ((8, np.uint8), (16, np.uint16)):
            maximum = np.iinfo(dtype).max
            writer_data = rng.integers(0, maximum + 1, size=SHAPE, dtype=dtype)
            input_path = tmp_path / f"ufo-writer-{bits}-source.tif"
            write_input(input_path, writer_data)

            for threads in (0, 1):
                name = f"ufo-writer-{bits}-threads-{threads}"
                compressed_path = tmp_path / f"{name}.tif"
                output_path = tmp_path / f"{name}-readback.tif"

                write_jpeg2000_with_ufo(
                    input_path,
                    compressed_path,
                    bits=bits,
                    threads=threads,
                )
                assert_jpeg2000_tiff(compressed_path, SHAPE[0], bits=bits)
                read_with_ufo(compressed_path, output_path, threads=threads)
                assert_equal_to_tifffile(input_path, output_path, name)

        input_path = tmp_path / "ufo-tiled-writer-source.tif"
        compressed_path = tmp_path / "ufo-tiled-writer-compressed.tif"
        output_path = tmp_path / "ufo-tiled-writer-readback.tif"

        write_input(input_path, data)
        write_jpeg2000_with_ufo(input_path, compressed_path, tile_size=128)
        assert_jpeg2000_tiff(
            compressed_path,
            SHAPE[0],
            tiled=True,
            tile_size=128,
            bits=16,
        )

        read_with_ufo(compressed_path, output_path)
        assert_equal_to_tifffile(input_path, output_path, "ufo-tiled-writer")

        rgb_planes = np.empty((3, 64, 96), dtype=np.uint8)
        rgb_planes[0] = np.arange(96, dtype=np.uint8)
        rgb_planes[1] = np.arange(64, dtype=np.uint8)[:, None]
        rgb_planes[2] = 137
        rgb_input_path = tmp_path / "ufo-rgb-writer-source.tif"
        rgb_output_path = tmp_path / "ufo-rgb-writer-compressed.tif"
        rgb_readback_path = tmp_path / "ufo-rgb-writer-readback.tif"

        tifffile.imwrite(rgb_input_path, rgb_planes, photometric="minisblack")
        write_jpeg2000_with_ufo(
            rgb_input_path,
            rgb_output_path,
            bits=8,
            threads=1,
            stack_number=3,
        )
        assert_jpeg2000_tiff(rgb_output_path, 1, bits=8, samples=3)
        np.testing.assert_array_equal(
            tifffile.imread(rgb_output_path),
            np.moveaxis(rgb_planes, 0, -1),
        )
        read_with_ufo(rgb_output_path, rgb_readback_path, threads=1)
        np.testing.assert_array_equal(
            tifffile.imread(rgb_readback_path),
            np.moveaxis(rgb_planes, 0, -1),
        )

        append_source = tmp_path / "ufo-append-source.tif"
        append_pattern = tmp_path / "ufo-append-%i.tif"
        existing_path = tmp_path / "ufo-append-0.tif"
        appended_path = tmp_path / "ufo-append-1.tif"
        sentinel = np.full((8, 8), 23, dtype=np.uint8)

        write_input(append_source, data)
        write_input(existing_path, sentinel)
        write_jpeg2000_with_ufo(
            append_source,
            append_pattern,
            threads=1,
            append=True,
        )
        np.testing.assert_array_equal(tifffile.imread(existing_path), sentinel)
        assert_jpeg2000_tiff(appended_path, SHAPE[0], bits=16)
        np.testing.assert_array_equal(tifffile.imread(appended_path), data)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)
