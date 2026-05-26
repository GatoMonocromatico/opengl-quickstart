#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++20 -DDEBUG -I Libraries/include -o build/test test.cpp
echo "Build OK"
./build/test
