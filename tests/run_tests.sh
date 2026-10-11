#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p ../build
g++ -std=c++20 -DDEBUG -I ../src -I ../Libraries/include -o ../build/test test.cpp
g++ -std=c++20 -Wall -Wextra -I ../src -o ../build/ui_tests ui_tests.cpp ../src/ui/TextEdit.cpp ../src/ui/Wrap.cpp
echo "Build OK"
../build/test
../build/ui_tests
