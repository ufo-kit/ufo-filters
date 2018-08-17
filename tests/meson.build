tests = [
    'test-149',
    'test-153',
    'test-161',
    'test-core-149',
    'test-file-write-regression'
]

tiffinfo = find_program('tiffinfo', required : false)

if tiffinfo.found()
    tests += ['test-142']
endif

test_env = [
    'UFO_PLUGIN_PATH=@0@'.format(join_paths(meson.build_root(), 'src'))
]

foreach t: tests
    test(t, find_program('@0@.sh'.format(t)), env: test_env)
endforeach
