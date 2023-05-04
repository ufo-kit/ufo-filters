tests = [
    'test-149',
    'test-153',
    'test-161',
    'test-177',
    'test-core-149',
    'test-file-write-regression',
    'test-nlm',
    'test-multipage-readers',
    'test-gradient'
]

tiffinfo = find_program('tiffinfo', required : false)

if tiffinfo.found()
    tests += ['test-142']
endif

test_env = [
    'UFO_PLUGIN_PATH=@0@'.format(join_paths(meson.build_root(), 'src'))
]

configure_file(input: 'make-input-multipage-readers',
               output: 'make-input-multipage-readers',
               copy: true)

configure_file(input: 'check-multipage-readers',
               output: 'check-multipage-readers',
               copy: true)

configure_file(input: 'check-gradient',
               output: 'check-gradient',
               copy: true)

foreach t: tests
    test(t, find_program('@0@.sh'.format(t)), env: test_env)
endforeach

# Python
python_tests = [
    'test_memin',
]

foreach t: python_tests
    test(t, find_program('python'),
         args: '@0@.py'.format(t),
         env: test_env,
         workdir: join_paths(meson.source_root(), 'tests'))
endforeach
