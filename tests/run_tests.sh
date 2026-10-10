#!/usr/bin/env bash
# Host-side tests (no ESP32 needed): lunar calendar + new firmware logic.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/extract.py
CXXFLAGS="-std=c++17 -O1 -Wall -Wno-unused -Wno-misleading-indentation -include cstring -include cstdlib"
g++ $CXXFLAGS -o tests/build/lunar_test tests/lunar_test.cpp
g++ $CXXFLAGS -o tests/build/logic_test tests/logic_test.cpp
./tests/build/lunar_test
./tests/build/logic_test
