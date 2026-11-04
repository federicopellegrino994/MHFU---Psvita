#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo 'Uso da ~/mhfu-vita: bash pmo-live-v1/run_checks.sh PERCORSO_MODEL_CAPTURE_BIN'
  exit 1
fi
pmo_package="$(cd "$(dirname "$0")" && pwd)"
pmo_check="$PWD/build/pmo-live-host-check"
mkdir -p "$pmo_check"
python3 "$pmo_package/tests/make_fixture.py" "$1" "$pmo_check/complete-model.bin"
c++ -std=c++20 -O2 -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math \
  -Iplatforms/psp/gpu -Ithird_party/psprecomp/include \
  "$pmo_package/tests/core.cpp" platforms/psp/gpu/ge.cpp \
  third_party/psprecomp/src/guest_memory.cpp -pthread -o "$pmo_check/core"
"$pmo_check/core" "$pmo_check/complete-model.bin"
c++ -std=c++20 -O2 -Wall -Wextra -Werror -Iplatforms/psp/gpu \
  "$pmo_package/tests/cache_threads.cpp" -pthread -o "$pmo_check/cache"
"$pmo_check/cache"
