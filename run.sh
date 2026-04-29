#!/bin/bash

cmake --build build || exit 1

gdb -batch -ex run -ex "bt full" ./build/fluxograme