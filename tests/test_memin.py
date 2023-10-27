#!/usr/bin/env python3

import gi
import numpy as np
gi.require_version("Ufo", "0.0")
from gi.repository import Ufo
from gi.repository import GLib


RESOURCES = Ufo.Resources()


def get_opencl_buffer(depth, width, height):
    import pyopencl as cl
    import pyopencl.array as pa

    # Use UFO's OpenCL context
    ctx = cl.Context.from_int_ptr(RESOURCES.get_context())
    queue = cl.CommandQueue(ctx, ctx.devices[0])

    ref_buf = pa.arange(queue, depth * width * height, dtype=np.float32).reshape(depth, height, width)
    ref = ref_buf.get()  # For checking later

    return (ref, ref_buf)


def run_test(input_dtype, use_complex=False, use_opencl=False, depth=1):
    width = 8
    height = 4

    if input_dtype == np.float32:
        bitdepth = 32
    elif input_dtype == np.uint16:
        bitdepth = 16
    else:
        bitdepth = 8

    if use_opencl:
        try:
            in_numpy, buf = get_opencl_buffer(depth, width, height)
        except ImportError:
            print("pyopencl not installed, skipping...")
            return
    else:
        in_numpy = np.arange(depth * height * width).reshape(depth, height, width).astype(input_dtype)
        if use_complex:
            in_numpy = (in_numpy + 1j * in_numpy[::-1, ::-1]).astype(np.complex64)
    out_numpy = np.zeros_like(in_numpy, dtype=np.complex64 if use_complex else np.float32)

    pm = Ufo.PluginManager()
    sched = Ufo.Scheduler()
    sched.set_resources(RESOURCES)
    graph = Ufo.TaskGraph()

    mem_in = pm.get_task("memory-in")
    mem_out = pm.get_task("memory-out")

    # Input
    mem_in.props.width = 2 * width if use_complex else width
    mem_in.props.height = height
    mem_in.props.bitdepth = bitdepth
    mem_in.props.number = depth
    mem_in.props.complex_layout = use_complex
    if use_opencl:
        mem_in.props.memory_location = "buffer"
        mem_in.props.pointer = buf.data.int_ptr
    else:
        mem_in.props.memory_location = "host"
        mem_in.props.pointer = in_numpy.__array_interface__["data"][0]

    mem_out.props.pointer = out_numpy.__array_interface__['data'][0]
    mem_out.props.max_size = out_numpy.nbytes

    graph.connect_nodes(mem_in, mem_out)
    sched.run(graph)

    np.testing.assert_almost_equal(in_numpy, out_numpy)


def run_expect_error(dtype, msg, use_complex=False):
    try:
        run_test(dtype, use_opencl=True, use_complex=use_complex)
    except GLib.GError:
        pass
    else:
        raise RuntimeError(msg)


def main():
    run_test(np.uint8)
    run_test(np.uint16)
    run_test(np.float32)
    run_test(np.float32, use_complex=True)
    run_test(np.float32, use_opencl=True)
    run_test(np.uint8, depth=2)
    run_test(np.uint16, depth=2)
    run_test(np.float32, depth=2)
    run_test(np.float32, use_complex=True, depth=2)
    run_test(np.float32, use_opencl=True, depth=2)
    run_expect_error(np.float32, "OpenCL complex error not raised", use_complex=True)
    run_expect_error(np.uint8, "OpenCL float32 error not raised")
    run_expect_error(np.uint16, "OpenCL float32 error not raised")


if __name__ == "__main__":
    main()
