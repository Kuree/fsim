#!/usr/bin/env bash

set -xe

if [ "$(uname)" == "Darwin" ]; then
    # directly build the python wheel
    python3 setup.py bdist_wheel
elif [ "$(uname -s)" == "Linux" ]; then
    # Do something under GNU/Linux platform
    # currently only supports linux so far. MacOS build is just so much pain
    DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
    ROOT=$(dirname "${DIR}")
    docker run -it -d --rm --name manylinux -v "${ROOT}":/fsim keyiz/manylinux2010 bash
    docker exec -i manylinux bash -c 'cd /fsim && python setup.py bdist_wheel --plat-name manylinux1_x86_64'
    # use the fix wheel script
    docker exec -i manylinux bash -c 'pip install wheeltools'
    docker exec -i manylinux bash -c 'cd /fsim && curl -OL https://github.com/Kuree/hgdb/raw/master/scripts/fix_wheel.py'
    docker exec -i manylinux bash -c 'cd /fsim && mkdir -p wheelhouse && python fix_wheel.py dist/*.whl -w wheelhouse'
    docker stop manylinux
else
  echo "Unsupported OS"
  exit 1
fi
