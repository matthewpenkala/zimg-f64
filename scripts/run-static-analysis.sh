#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cppcheck \
  --std=c++17 \
  --enable=warning,performance,portability \
  --error-exitcode=1 \
  --inline-suppr \
  --suppressions-list="${root}/tools/cppcheck-suppressions.txt" \
  -I"${root}/src" \
  "${root}/src/f64app"

cmake -S "${root}" -B "${root}/build-tidy" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DZIMG_F64_BUILD_MPFR_ORACLE=ON
run-clang-tidy -p "${root}/build-tidy" \
  '.*[\\/]src[\\/](f64app[\\/].*|zimg[\\/]resize[\\/]filter[.]cpp)$'
