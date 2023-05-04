#!/bin/bash

python -c "import numpy; import tifffile; tifffile.imwrite('ones-ifft-mean-check.tif', numpy.ones((9, 9), dtype=numpy.float32))"

ufo-launch -q read path=ones-ifft-mean-check.tif ! fft dimensions=2 ! ifft dimensions=2 crop-width=9 crop-height=9 ! write filename=ones-ifft-mean-check-back.tif

python -c "import sys; import tifffile; a = tifffile.imread('ones-ifft-mean-check.tif'); b = tifffile.imread('ones-ifft-mean-check-back.tif'); sys.exit(int(abs(a - b).max() > 1e-6))"
