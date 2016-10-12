"""Generate burst laminographic backprojection OpenCL kernels."""
import argparse
import math
import os


IDX_TO_VEC_ELEM = dict(zip(range(10), range(10)))
IDX_TO_VEC_ELEM[10] = 'a'
IDX_TO_VEC_ELEM[11] = 'b'
IDX_TO_VEC_ELEM[12] = 'c'
IDX_TO_VEC_ELEM[13] = 'd'
IDX_TO_VEC_ELEM[14] = 'e'
IDX_TO_VEC_ELEM[15] = 'f'


def fill_compute_template(tmpl, num_items, index, constant, lut_offset=0):
    """Fill the template doing the pixel computation and texture fetch."""
    operation = '+' if index or lut_offset else ''
    if constant:
        access = '[{}]'.format(index)
    else:
        access = '.s{}'.format(IDX_TO_VEC_ELEM[index % 16]) if num_items > 1 else ''

    lut_index = 0 if constant else index / 16 + lut_offset

    return tmpl.format(lut_offset * 16 + index, access, operation, lut_index)


def fill_kernel_template(input_tmpl, compute_tmpl, kernel_outer, kernel_inner, num_items,
                         constant):
    """Construct the whole kernel."""
    if constant:
        lut_str = 'constant float *sines_0,\nconstant float *cosines_0,\n'
        computes = '\n'.join([fill_compute_template(compute_tmpl, num_items, i, constant)
                              for i in range(num_items)])
    else:
        lut_tmpl = 'const float{0} sines_{1},\nconst float{0} cosines_{1},\n'
        lut_str = ''
        num_16 = num_items / 16
        for i in range(num_16):
            lut_str += lut_tmpl.format(16, i)
        computes = '\n'.join([fill_compute_template(compute_tmpl, num_items, i, constant)
                              for i in range(16 * num_16)])
        num_rest = num_items % 16
        if num_rest:
            rest_vector_length = int(2 ** math.ceil(math.log(num_rest, 2)))
            if rest_vector_length == 1:
                rest_vector_length = ''
            lut_str += lut_tmpl.format(rest_vector_length, num_16)
            if num_16:
                computes += '\n'
            computes += '\n'.join([fill_compute_template(compute_tmpl, num_rest, i,
                                                         constant, lut_offset=num_16)
                                  for i in range(num_rest)])

    inputs = '\n'.join([input_tmpl.format(i) for i in range(num_items)])
    kernel_inner = kernel_inner.format(computes)

    return kernel_outer.format(num_items, inputs, lut_str, kernel_inner)


def parse_args():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument('filename', type=str, help='File name with the kernel template')
    parser.add_argument('constant', type=int, choices=[0, 1],
                        help='Use constant memory to store sin and cos LUT')
    parser.add_argument('start', type=int,
                        help='Minimum number of projections processed by one kernel invocation')
    parser.add_argument('stop', type=int,
                        help='Maximum number of projections processed by one kernel invocation')

    args = parser.parse_args()
    if args.stop < args.start:
        raise ValueError("'stop' < 'start'")

    return args


def main():
    """execute program."""
    args = parse_args()
    in_tmpl = "read_only image2d_t projection_{},"
    common_filename = os.path.join(os.path.dirname(args.filename), 'common.in')
    defs_filename = os.path.join(os.path.dirname(args.filename), 'definitions.in')
    defs = open(defs_filename, 'r').read()
    kernel_outer = open(common_filename, 'r').read()
    comp_tmpl, kernel_inner = open(args.filename, 'r').read().split('\n%nl\n')
    kernels = defs + '\n'
    for burst in range(args.start, args.stop + 1):
        kernels += fill_kernel_template(in_tmpl, comp_tmpl, kernel_outer, kernel_inner, burst,
                                        args.constant)

    print kernels


if __name__ == '__main__':
    main()
