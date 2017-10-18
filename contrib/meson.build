plugins = [
    'med-mad-reject',
    'med-mad-reject-2d',
    'ocl-1liner',
    'stat-monitor',
]

kernels = [
    'kernels/med-mad-reject.cl',
    'kernels/med-mad-reject-2d.cl',
    'kernels/ocl-1liner-skel.cl',
    'kernels/stat-monitor.cl',
]

foreach plugin: plugins
    name = ''.join(plugin.split('-'))

    shared_module(name,
        sources: ['ufo-@0@-task.c'.format(plugin), 'ufo-sxc-common.c'],
        dependencies: deps,
        name_prefix: 'libufofilter',
        install: true,
        install_dir: plugin_install_dir,
    )
endforeach

install_data(kernels,
    install_dir: kernel_install_dir,
)
