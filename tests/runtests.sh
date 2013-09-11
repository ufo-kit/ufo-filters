#!/bin/bash

CUR=$(pwd)

if [ ! -d "$CUR/venv" ]; then
    echo "* Creating virtualenv ..."
    virtualenv --system-site-packages -q $CUR/venv
fi

echo "* Activate virtualenv ..."
source $CUR/venv/bin/activate

# Install nose if not available
pip freeze | grep nose > /dev/null
RC=$?

if [ "$RC" -ne "0" ]; then
    echo "* Installing nose ..."
    pip install nose nose-parameterized
fi

# Install unittest2 if Python less than 2.7
python -c 'import sys; v=sys.version_info; sys.exit((v[0], v[1]) < (2,7))'
RC=$?

if [ "$RC" -ne "0" ]; then
    pip freeze | grep unittest2 > /dev/null
    RC=$?

    if [ "$RC" -ne "0" ]; then
        echo "* Installing unittest2 ..."
        pip install unittest2
    fi
fi

echo "* Running tests ..."
export CUDA_VISIBLE_DEVICES=0,1
nosetests tests.py
