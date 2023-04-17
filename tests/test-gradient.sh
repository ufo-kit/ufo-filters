#!/bin/sh

# Make input
ufo-launch dummy-data width=8 height=8 number=1 ! calculate expression="'x'" ! write tiff-bigtiff=False filename=gradient-input.tif

for direction in horizontal vertical both both_abs
do
    for fd_type in forward backward central
    do
        # Do not test sampler boundary values, fix to "repeat"
        ufo-launch -q read path=gradient-input.tif ! gradient direction=$direction addressing-mode=repeat finite-difference-type=$fd_type ! write tiff-bigtiff=False filename=ufo-gradient.tif
        tests/check-gradient $direction $fd_type
    done
done

# Cleanup
rm gradient-input.tif
rm ufo-gradient.tif
