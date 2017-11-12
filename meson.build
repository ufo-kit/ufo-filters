project('ufo-filters', 
    ['c', 'cpp'],
    version: '0.14.1'
)

version = meson.project_version()
components = version.split('.')
version_major = components[0]
version_minor = components[1]
version_patch = components[2]

cc = meson.get_compiler('c')

add_global_arguments(
    '-DGLIB_DISABLE_DEPRECATION_WARNINGS', 
    '-DCL_USE_DEPRECATED_OPENCL_1_1_APIS',
    '-DCL_USE_DEPRECATED_OPENCL_1_2_APIS',
    language: 'c'
)

if cc.get_id() == 'gcc'
    add_global_arguments(
        '-Wno-unused-parameter',
        '-fopenmp',
        language: ['c', 'cpp'])
    add_global_link_arguments('-fopenmp', language: 'c')
endif

opencl_dep = declare_dependency(dependencies: cc.find_library('OpenCL'))
ufo_dep = dependency('ufo', version: '>= 0.12')

plugin_install_dir = ufo_dep.get_pkgconfig_variable('plugindir')
kernel_install_dir = ufo_dep.get_pkgconfig_variable('kerneldir')

deps = [
    ufo_dep,
    opencl_dep,
]

subdir('bin')
subdir('deps')
subdir('src')
subdir('tests')

if get_option('contrib_filters')
  subdir('contrib')
endif
