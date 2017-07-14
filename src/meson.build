plugins = [
    'average',
    'backproject',
    'bin',
    'binarize',
    'blur',
    'buffer',
    'calculate',
    'clip',
    'crop',
    'cut',
    'cut-sinogram',
    'center-of-rotation',
    'concatenate-result',
    'contrast',
    'correlate-stacks',
    'dfi-sinc',
    'denoise',
    'detect-edge',
    'rescale',
    'dummy-data',
    'dump-ring',
    'duplicate',
    'filter',
    'flatten',
    'flatten-inplace',
    'flat-field-correct',
    'fftmult',
    'filter-particle',
    'filter-stripes',
    'filter-stripes1d',
    'flip',
    'forwardproject',
    'get-dup-circ',
    'interpolate',
    'loop',
    'map-slice',
    'measure-sharpness',
    'median-filter',
    'memory-in',
    'memory-out',
    'merge',
    'metaballs',
    'monitor',
    'null',
    'opencl',
    'ordfilt',
    'pad',
    'polar-coordinates',
    'reduce',
    'refeed',
    'remove-circle',
    'ring-pattern',
    'ringwriter',
    'replicate',
    'sleep',
    'slice',
    'stack',
    'stdin',
    'transpose',
    'transpose-projections',
    'swap-quadrants',
    'subtract',
    'volume-render',
    'zeropad',
]

fft_plugins = [
    'fft',
    'ifft',
    'retrieve-phase',
]

kernels = [
]

read_sources = [
    'ufo-read-task.c',
    'readers/ufo-reader.c',
    'readers/ufo-edf-reader.c',
    'readers/ufo-raw-reader.c',
]

write_sources = [
    'ufo-write-task.c',
    'writers/ufo-writer.c',
    'writers/ufo-raw-writer.c',
]

tiff_dep = dependency('libtiff-4', required: false)
hdf5_dep = dependency('hdf5', required: false)
jpeg_dep = dependency('libjpeg', required: false)
gsl_dep = dependency('gsl', required: false)

conf = configuration_data()
conf.set('HAVE_TIFF', tiff_dep.found())
conf.set('HAVE_JPEG', jpeg_dep.found())
conf.set('WITH_HDF5', hdf5_dep.found())

configure_file(
    input: 'config.h.meson.in',
    output: 'config.h',
    configuration: conf,
)

# standard plugins

foreach plugin: plugins
    name = ''.join(plugin.split('-'))

    shared_module(name,
        'ufo-@0@-task.c'.format(plugin),
        dependencies: deps,
        name_prefix: 'libufofilter',
        install: true,
        install_dir: plugin_install_dir,
    )
endforeach

# fft plugins

common_fft = static_library('commonfft',
    'common/ufo-fft.c',
    dependencies: deps + [oclfft_dep]
)

foreach plugin: fft_plugins
    name = ''.join(plugin.split('-'))

    shared_module(name,
        'ufo-@0@-task.c'.format(plugin),
        dependencies: deps,
        name_prefix: 'libufofilter',
        link_with: common_fft,
        install: true,
        install_dir: plugin_install_dir,
    )
endforeach

# lamino plugin

shared_module('lamino_backproject',
    sources: ['ufo-lamino-backproject-task.c'],
    dependencies: deps,
    name_prefix: 'libufofilter',
    install: true,
    install_dir: plugin_install_dir,
    c_args: [
        '-DBURST=@0@'.format(get_option('lamino_backproject_burst_mode')),
    ],
)

# i/o plugins

read_deps = deps
write_deps = deps

if tiff_dep.found()
    read_sources += ['readers/ufo-tiff-reader.c']
    read_deps += [tiff_dep]

    write_sources += ['writers/ufo-tiff-writer.c']
    write_deps += [tiff_dep]
endif

if hdf5_dep.found()
    read_sources += ['readers/ufo-hdf5-reader.c', 'common/hdf5.c']
    read_deps += [hdf5_dep]

    write_sources += ['writers/ufo-hdf5-writer.c', 'common/hdf5.c']
    write_deps += [hdf5_dep]
endif

if jpeg_dep.found()
    write_sources += ['writers/ufo-jpeg-writer.c']
    write_deps += [jpeg_dep]
endif

shared_module('read',
    sources: read_sources,
    dependencies: read_deps,
    name_prefix: 'libufofilter',
    install: true,
    install_dir: plugin_install_dir,
)

shared_module('write',
    sources: write_sources,
    dependencies: write_deps,
    name_prefix: 'libufofilter',
    install: true,
    install_dir: plugin_install_dir,
)

if gsl_dep.found()
    shared_module('measure', 'ufo-measure-task.c',
        dependencies: deps + [gsl_dep],
        name_prefix: 'libufofilter',
        install: true,
        install_dir: plugin_install_dir,
    )
endif

subdir('kernels')
