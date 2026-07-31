#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${root}/build-upstream}"

mkdir -p "${build}"
if [[ ! -x "${root}/configure" ]]; then
    cd "${root}"
    ./autogen.sh
fi
export CMAKE_GENERATOR="${CMAKE_GENERATOR:-Unix Makefiles}"
cd "${build}"
"${root}/configure" --enable-unit-test
make -j"${JOBS:-2}"
make check
