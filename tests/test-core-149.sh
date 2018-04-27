#!/bin/bash

ufo-launch -q -d foo.json dummy-data ! opencl filename=complex.cl kernel=c_conj ! null
exit $?
