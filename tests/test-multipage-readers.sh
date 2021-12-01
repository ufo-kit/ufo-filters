#!/bin/bash

tests/make-input-multipage-readers

# TIFF
ufo-launch -q read path=multipage-image*.tif image-start=5 image-step=8 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 5 13 21 29 37 45 53
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read path=multipage-image*.tif image-start=17 image-step=8 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 17 25 33 41 49 57
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read path=multipage-image*.tif image-start=5 image-step=30 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 5 35
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read path=multipage-image*.tif image-start=17 image-step=30 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 17 47
if [ $? -ne 0 ]; then
    exit 1;
fi

# RAW
ufo-launch -q read raw-width=128 raw-height=128 raw-bitdepth=16 path=multipage-image*.raw image-start=5 image-step=8 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 5 13 21 29 37 45 53
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read raw-width=128 raw-height=128 raw-bitdepth=16 path=multipage-image*.raw image-start=17 image-step=8 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 17 25 33 41 49 57
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read raw-width=128 raw-height=128 raw-bitdepth=16 path=multipage-image*.raw image-start=5 image-step=30 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 5 35
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read raw-width=128 raw-height=128 raw-bitdepth=16 path=multipage-image*.raw image-start=17 image-step=30 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 17 47
if [ $? -ne 0 ]; then
    exit 1;
fi

# HDF5 (no multiple files reading support, so just work with one)
ufo-launch -q read path=multipage-image-00.h5:/images image-start=5 image-step=8 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 5 13
if [ $? -ne 0 ]; then
    exit 1;
fi
ufo-launch -q read path=multipage-image-00.h5:/images image-start=5 image-step=80 ! write filename=multipage-out.tif tiff-bigtiff=False
tests/check-multipage-readers 5
if [ $? -ne 0 ]; then
    exit 1;
fi
