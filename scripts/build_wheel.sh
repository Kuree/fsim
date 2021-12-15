#!/usr/bin/env bash

set -xe

# currently only supports linux so far. MacOS build is just so much pain
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT=$(dirname "${DIR}")
docker run -it -d --rm --name manylinux -v "${ROOT}":/xsim keyiz/manylinux2010 bash
docker exec -i manylinux bash -c 'cd /xsim && python setup.py bdist_wheel'
docker exec -i manylinux bash -c 'cd /xsim && auditwheel repair --plat manylinux_2_24_x86_64 dist/* -w wheels'
# use the fix wheel script
docker exec -i manylinux bash -c 'pip install wheeltools'
docker exec -i manylinux bash -c 'cd /xsim && curl -OL https://github.com/Kuree/hgdb/raw/master/scripts/fix_wheel.py'
docker exec -i manylinux bash -c 'cd /xsim && mkdir -p wheelhouse && python fix_wheel.py wheels/*.whl -w wheelhouse'
docker stop manylinux