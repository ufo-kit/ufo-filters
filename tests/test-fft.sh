#!/bin/sh

# Make input
python -c "import numpy; import tifffile; tifffile.imwrite('fft-input.tif',
numpy.arange(8 * 16 * 32).reshape(8, 16, 32).astype(numpy.float32))"

for NBATCH in 1 2
do
    # Save both fft and ifft and check the fft with ifft-ing by numpy in case
    # the forward pass doess nonsense and backward pass the same kind of
    # nonsense
	ufo-launch -q read path=fft-input.tif number=1 ! fft dimensions=$NBATCH ! write tiff-bigtiff=False filename=ufo-ft-2d-"$NBATCH".tif
	ufo-launch -q read path=fft-input.tif number=1 ! fft dimensions=$NBATCH ! ifft dimensions=$NBATCH ! write tiff-bigtiff=False filename=ufo-ift-2d-"$NBATCH".tif
done

for NBATCH in 1 2 3
do
    ufo-launch -q read path=fft-input.tif ! stack number=8 ! fft dimensions=$NBATCH ! slice ! write tiff-bigtiff=False filename=ufo-ft-3d-"$NBATCH".tif
    ufo-launch -q read path=fft-input.tif ! stack number=8 ! fft dimensions=$NBATCH ! ifft dimensions=$NBATCH ! slice ! write tiff-bigtiff=False filename=ufo-ift-3d-"$NBATCH".tif
done


# Test
tests/check-fft


# Cleanup
rm fft-input.tif
# Batch=3 is valid only for 3D
rm ufo-ft-3d-3.tif
rm ufo-ift-3d-3.tif

for NDIM in 2 3
do
    for NBATCH in 1 2
    do
        rm ufo-ft-"$NDIM"d-"$NBATCH".tif
        rm ufo-ift-"$NDIM"d-"$NBATCH".tif
    done
done
