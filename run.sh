#!/bin/bash
# Build (Debug) then run the example from the repo root under gdb for a crash backtrace.
set -e
cd "$(dirname "$0")"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

gdb -batch -ex run -ex "bt full" ./build/opengl_quickstart_example
