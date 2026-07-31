#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-${root}/build-sanitize}"
fixture="${build}/fixture.yuv"
scaler="${build}/f64resize_yuv"

cmake -S "${root}" -B "${build}" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DZIMG_F64_ENABLE_SANITIZERS=ON \
  -DZIMG_F64_BUILD_MPFR_ORACLE=OFF
cmake --build "${build}"
if [[ -x "${scaler}.exe" ]]; then
  scaler="${scaler}.exe"
fi
python "${root}/tools/generate_synthetic_fixture.py" --output "${fixture}"
python "${root}/tools/run_determinism.py" \
  --fixture "${fixture}" \
  --n-threads "${THREADS:-8}" \
  --executable "sanitized=${scaler}" \
  --expected-quantized-sha256 \
    914477670148e89188aee144a57629bdef0ca41c8c9d2f4731752cf46dd811ca \
  --expected-prequant-sha256 \
    cc07600cdc80284243b1efbf8c4cb59a4cea23152c9cac08627ec274bf07e343
