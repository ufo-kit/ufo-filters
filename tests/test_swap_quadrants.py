import gi
import numpy as np
gi.require_version("Ufo", "0.0")
from gi.repository import Ufo


RESOURCES = Ufo.Resources()


def main(width, height, use_complex=False):
    in_numpy = np.arange(width * height).reshape(height, width).astype(np.float32)
    if use_complex:
        in_numpy = (in_numpy + 1j * in_numpy[::-1, ::-1]).astype(np.complex64)

    out_numpy = np.zeros_like(in_numpy)
    gt = np.fft.fftshift(in_numpy)

    pm = Ufo.PluginManager()
    sched = Ufo.Scheduler()
    sched.set_resources(RESOURCES)
    graph = Ufo.TaskGraph()

    mem_in = pm.get_task("memory-in")
    mem_out = pm.get_task("memory-out")
    swap_task = pm.get_task("swap-quadrants")

    # Input
    mem_in.props.width = 2 * width if use_complex else width
    mem_in.props.height = height
    mem_in.props.bitdepth = 32
    mem_in.props.number = 1
    mem_in.props.pointer = in_numpy.__array_interface__["data"][0]
    mem_in.props.complex_layout = use_complex

    mem_out.props.pointer = out_numpy.__array_interface__['data'][0]
    mem_out.props.max_size = out_numpy.nbytes

    graph.connect_nodes(mem_in, swap_task)
    graph.connect_nodes(swap_task, mem_out)
    sched.run(graph)

    np.testing.assert_almost_equal(gt, out_numpy)


if __name__ == "__main__":
    for use_complex in [False, True]:
        # Odd odd
        main(11, 7, use_complex=use_complex)
        main(7, 11, use_complex=use_complex)
        # Even odd
        main(16, 7, use_complex=use_complex)
        main(7, 16, use_complex=use_complex)
        # Even even
        main(8, 16, use_complex=use_complex)
        main(16, 8, use_complex=use_complex)
        # Large Even for local size check
        main(256, 256, use_complex=use_complex)
