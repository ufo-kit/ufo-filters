#!/bin/bash

python -c "import numpy; import tifffile; tifffile.imsave('d64.tif', numpy.arange(0, 64).reshape((8, 8)).astype(numpy.float64))"

ufo-launch -q read path=d64.tif ! null
exit $?
