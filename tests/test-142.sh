#!/bin/bash

ufo-launch --quieter dummy-data width=512 height=512 ! crop width=512 height=512 ! write filename=out.tif

width=$(tiffinfo out.tif | grep -oE "Image Width: [0-9]+" | grep -oE "[0-9]+")
height=$(tiffinfo out.tif | grep -oE "Image Length: [0-9]+" | grep -oE "[0-9]+")

[ "$width" == "512" ] && [ "$height" == "512" ] && exit 0 || exit 1
