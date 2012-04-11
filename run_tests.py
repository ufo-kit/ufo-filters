import nose
import sys
import os

if __name__ == '__main__':
    key = 'UFO_FILTER_PATH'
    os.environ[key] = sys.argv[1]
    nose.core.run(argv=sys.argv[1:])
    del os.environ[key]
