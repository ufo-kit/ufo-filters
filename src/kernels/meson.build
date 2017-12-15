kernel_files = [
    'arithmetics.cl',
    'backproject.cl',
    'binarize.cl',
    'bin.cl',
    'clip.cl',
    'complex.cl',
    'correlate.cl',
    'cut.cl',
    'cut-sinogram.cl',
    'default.cl',
    'denoise.cl',
    'dfi.cl',
    'edge.cl',
    'ffc.cl',
    'fft.cl',
    'fftmult.cl',
    'filter.cl',
    'flip.cl',
    'forwardproject.cl',
    'gaussian.cl',
    'histthreshold.cl',
    'interpolator.cl',
    'mask.cl',
    'median.cl',
    'metaballs.cl',
    'nlm.cl',
    'ordfilt.cl',
    'pad.cl',
    'phase-retrieval.cl',
    'piv.cl',
    'polar.cl',
    'rescale.cl',
    'reductor.cl',
    'rotate.cl',
    'segment.cl',
    'swap-quadrants.cl',
    'zeropad.cl'
]

install_data(kernel_files,
    install_dir: kernel_install_dir,
)

# generate kernels for lamino_backproject
lamino_kernel_names = [
    'z',
    'lamino',
    'center',
    'roll',
]

if python.found()
    command = [
        python.path(),
        '@INPUT0@', '@INPUT1@',
        '1', '2', '4', '8', '16',
    ]

    foreach name: lamino_kernel_names
        target_name = '@0@_kernel'.format(name)
        template_name = 'templates/@0@_template.in'.format(name)
        output_name = '@0@_kernel.cl'.format(name)

        custom_target(target_name,
            build_by_default: true,
            capture: true,
            command: command,
            input: ['tools/make_burst_kernels.py', template_name],
            output: output_name,
            install: true,
            install_dir: kernel_install_dir
        )
    endforeach
endif
