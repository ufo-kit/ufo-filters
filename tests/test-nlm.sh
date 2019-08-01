#!/bin/bash

python -c "import numpy; import tifffile; tifffile.imsave('nlm-noise-input.tif',
numpy.random.normal(100., 10., size=(128, 128)).astype(numpy.float32))"

ufo-launch -q read path=nlm-noise-input.tif ! non-local-means patch-radius=3 search-radius=5 fast=False estimate-sigma=True addressing-mode=mirrored_repeat window=False ! write filename=ufo-nlm-slow.tif

ufo-launch -q read path=nlm-noise-input.tif ! non-local-means patch-radius=3 search-radius=5 fast=True estimate-sigma=True addressing-mode=mirrored_repeat !  write filename=ufo-nlm-fast.tif

# Fast and slow difference with respect to the mean must be less than 0.001 %
python -c "import sys; import tifffile; a = tifffile.imread('ufo-nlm-slow.tif'); b = tifffile.imread('ufo-nlm-fast.tif'); sys.exit(int(abs(a - b).max() > 1e-3))"
