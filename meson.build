project('ufo-filters', 
    ['c', 'cpp'],
    version: '0.16.0'
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
ufo_dep = dependency('ufo', version: '>= 0.16')

m_dep = declare_dependency(
    dependencies: cc.find_library('m')
)

plugin_install_dir = ufo_dep.get_pkgconfig_variable('plugindir')
kernel_install_dir = ufo_dep.get_pkgconfig_variable('kerneldir')

prefixdir = get_option('prefix')
datadir = join_paths(prefixdir, get_option('datadir'))
docdir = join_paths(datadir, 'doc', 'ufo-filters')

deps = [
    ufo_dep,
    opencl_dep,
    m_dep,
]

subdir('deps')
subdir('docs')
subdir('src')
subdir('tests')

if get_option('contrib_filters')
  subdir('contrib')
endif
