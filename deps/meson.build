if get_option('oclfft')
    oclfft_lib = shared_library('oclfft',
        sources: [
            'oclfft/fft_execute.cpp',
            'oclfft/fft_kernelstring.cpp',
            'oclfft/fft_setup.cpp',
        ],
        dependencies: deps,
        install: true,
    )

    oclfft_dep = declare_dependency(
        include_directories: include_directories('oclfft'),
        link_with: oclfft_lib,
    )
endif
