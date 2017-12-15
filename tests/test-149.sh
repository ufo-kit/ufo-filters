#!/bin/bash

ufo-launch -q dummy-data width=1024 height=1024 ! rotate angle=1.57 center=10.0,10.0 reshape=true ! null
exit $?
