#!/usr/bin/env bash
# all builds + test layers. Run inside docker/Dockerfile or CI:
#   docker run --rm -v "$PWD:/src" -w /src tlm-dev ./scripts/ci.sh [quick]
set -euo pipefail
FUZZ_SECONDS=${FUZZ_SECONDS:-30}

echo "== GCC release build + unit tests"
cmake -S . -B build/gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build/gcc
ctest --test-dir build/gcc --output-on-failure

echo "== Clang + AddressSanitizer + UBSan + unit tests"
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DTLM_SANITIZE=ON
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure

echo "== UDP integration harness (Python encoder -> C++ decoder)"
TLM_DECODE=build/asan/tlm_decode python3 -m pytest -q harness

if [[ "${1:-}" != "quick" ]]; then
  echo "== libFuzzer, ${FUZZ_SECONDS}s"
  cmake -S . -B build/fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
        -DTLM_BUILD_FUZZERS=ON -DTLM_BUILD_TESTS=OFF
  cmake --build build/fuzz
  mkdir -p fuzz/corpus-run
  cp fuzz/corpus/* fuzz/corpus-run/ 2>/dev/null || true
  build/fuzz/decode_fuzzer -max_total_time="$FUZZ_SECONDS" -print_final_stats=1 fuzz/corpus-run
fi
echo "== all checks passed"
