import gi
import numpy as np
gi.require_version("Ufo", "0.0")
from gi.repository import Ufo


RESOURCES = Ufo.Resources()


def next_power_of_two(number):
    """Compute the next power of two of the *number*."""
    return 2 ** int(np.ceil(np.log2(number)))


def get_ground_truth_ft(in_numpy, dimensions, shape=None):
    if dimensions == 1:
        f_in = np.fft.fft(in_numpy, n=None if shape is None else shape[-1])
    elif dimensions == 2:
        f_in = np.fft.fft2(in_numpy, s=None if shape is None else shape[1:])
    else:
        f_in = np.fft.fftn(in_numpy, s=shape)

    return f_in.astype(np.complex64)


def main(
    input_shape,
    output_shape=None,
    crop_width=None,
    crop_height=None,
    dimensions=1,
    do_inverse=False,
    auto_zeropadding=False,
    stack=True
):
    if output_shape is None:
        if auto_zeropadding:
            output_shape = (
                next_power_of_two(input_shape[0]) if dimensions == 3 else input_shape[0],
                next_power_of_two(input_shape[1]) if dimensions >= 2 else input_shape[1],
                next_power_of_two(input_shape[2]),
            )
        else:
            output_shape = input_shape

    depth, height, width = input_shape
    in_numpy = np.linspace(-1, 2, num=width * height * depth, dtype=np.float32).reshape(depth, height, width)
    out_numpy = np.zeros(
        (
            output_shape[0],
            crop_height if do_inverse and crop_height else output_shape[1],
            crop_width if do_inverse and crop_width else output_shape[2],
        ),
        dtype=np.float32 if do_inverse else np.complex64
    )

    pm = Ufo.PluginManager()
    sched = Ufo.Scheduler()
    sched.set_resources(RESOURCES)
    graph = Ufo.TaskGraph()

    mem_in = pm.get_task("memory-in")
    mem_out = pm.get_task("memory-out")
    fft_task = pm.get_task("fft")
    ifft_task = pm.get_task("ifft")
    stack_task = pm.get_task("stack")
    slice_task = pm.get_task("slice")

    # Input
    mem_in.props.width = width
    mem_in.props.height = height
    mem_in.props.bitdepth = 32
    mem_in.props.number = depth
    mem_in.props.pointer = in_numpy.__array_interface__["data"][0]

    # Stack
    stack_task.props.number = depth

    # FFT and IFFT
    fft_task.props.dimensions = dimensions
    fft_task.props.auto_zeropadding = auto_zeropadding
    if output_shape is not None:
        fft_task.props.size_x = output_shape[2]
        fft_task.props.size_y = output_shape[1]
        fft_task.props.size_z = output_shape[0]
    if do_inverse and crop_width:
        ifft_task.props.crop_width = crop_width
    if do_inverse and crop_height:
        ifft_task.props.crop_height = crop_height
    ifft_task.props.dimensions = dimensions

    mem_out.props.pointer = out_numpy.__array_interface__['data'][0]
    mem_out.props.max_size = out_numpy.nbytes

    if stack:
        graph.connect_nodes(mem_in, stack_task)
        graph.connect_nodes(stack_task, fft_task)
        if do_inverse:
            graph.connect_nodes(fft_task, ifft_task)
            graph.connect_nodes(ifft_task, slice_task)
        else:
            graph.connect_nodes(fft_task, slice_task)
        graph.connect_nodes(slice_task, mem_out)
    else:
        graph.connect_nodes(mem_in, fft_task)
        if do_inverse:
            graph.connect_nodes(fft_task, ifft_task)
            graph.connect_nodes(ifft_task, mem_out)
        else:
            graph.connect_nodes(fft_task, mem_out)

    sched.run(graph)

    # Check results
    if do_inverse:
        if output_shape != input_shape:
            padded_input = np.pad(
                in_numpy,
                (
                    (0, output_shape[0] - input_shape[0]),
                    (0, output_shape[1] - input_shape[1]),
                    (0, output_shape[2] - input_shape[2]),
                ),
                mode="constant",
            )
        else:
            padded_input = in_numpy
        padded_input = padded_input[:, :crop_height, :crop_width]
        np.testing.assert_almost_equal(out_numpy, padded_input, decimal=5)
    else:
        f_in = get_ground_truth_ft(in_numpy, dimensions, shape=output_shape)
        np.testing.assert_almost_equal(out_numpy, f_in, decimal=2)


if __name__ == "__main__":
    input_shapes = [
        # Power of two
        (32, 32, 32),
        (32, 16, 8),
        (8, 16, 32),
        # Non-power of two
        (33, 17, 9),
        (9, 17, 33),
        (9, 17, 31),
        (33, 33, 33),
    ]

    ## Automatic size selection
    for shape in input_shapes:
        main(shape, dimensions=1)

    ## User size
    input_shape = (9, 17, 33)
    for do_inverse in [False, True]:
        main(input_shape, output_shape=(11, 25, 50), dimensions=3, do_inverse=do_inverse)
        main(input_shape, output_shape=(9, 25, 50), dimensions=2, do_inverse=do_inverse)
        main(input_shape, output_shape=(9, 17, 50), dimensions=1, do_inverse=do_inverse)
    # Larger than what Chirp-z does
    main(input_shape, output_shape=(32, 128, 128), dimensions=3, do_inverse=False)

    ## Auto zeropadding
    input_shape = (9, 17, 31)
    for do_inverse in [False, True]:
        for dimensions in range(1, 4):
            main(input_shape, auto_zeropadding=True, dimensions=dimensions, do_inverse=do_inverse)

    ## Cropping
    input_shape = (9, 17, 33)
    # 3D custom crop
    main(input_shape, output_shape=(11, 25, 50), dimensions=3, crop_width=45, crop_height=10, do_inverse=True)
    main(input_shape, dimensions=3, crop_width=25, crop_height=10, do_inverse=True)

    # 2D custom crop
    main(input_shape, output_shape=(9, 25, 50), dimensions=2, crop_width=45, crop_height=10, do_inverse=True)
    main(input_shape, dimensions=2, crop_width=25, crop_height=10, do_inverse=True)

    # 1D custom crop
    main(input_shape, output_shape=(9, 17, 50), dimensions=1, crop_width=45, do_inverse=True)
    main(input_shape, dimensions=1, crop_width=25, do_inverse=True)
    main((32, 32, 10), dimensions=2, do_inverse=True, auto_zeropadding=False, stack=False)
