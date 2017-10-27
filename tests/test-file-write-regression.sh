#!/bin/bash

ufo-launch dummy-data width=512 height=512 number=3 ! write filename=foo-%02i.tif

files=("foo-*.tif")
files=$(echo ${files[*]})

# cleanup
rm -f foo-*.tif

[ "$files" == "foo-00.tif foo-01.tif foo-02.tif" ] && exit 0 || exit 1
