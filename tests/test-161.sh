#!/bin/bash

ufo-launch -q [dummy-data ! fft, dummy-data ! fft] ! subtract ! null
exit $?
