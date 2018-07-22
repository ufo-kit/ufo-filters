sources = [
    'changes.rst',
    'copyright.rst',
    'examples.rst',
    'filters.rst',
    'generators.rst',
    'index.rst',
    'install.rst',
    'kernels.rst',
    'piv.rst',
    'sinks.rst',
    'contrib/index.rst',
    'contrib/sxcfilters.rst',
]

sphinx = find_program('sphinx-build', required: false)

if sphinx.found()
    command = [
        sphinx.path(),
        '-q',
        '-b', 'html',
        meson.current_source_dir(),
        meson.current_build_dir(),
    ]

    manual = custom_target('manual',
        build_by_default: true,
        command: command,
        input: sources,
        output: 'index.html',
    )

    install_subdir(meson.current_build_dir(), install_dir: docdir)
else
    message('sphinx not found, cannot generate manual.')
endif

