#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p ../build
g++ -std=c++20 -DDEBUG -I ../src -I ../Libraries/include -o ../build/test test.cpp
g++ -std=c++20 -Wall -Wextra -I ../src -I ../Libraries/include -o ../build/ui_tests ui_tests.cpp ../src/ui/TextEdit.cpp ../src/ui/Wrap.cpp ../src/ui/Anim.cpp ../src/ui/CanvasView.cpp
echo "Build OK"
../build/test
../build/ui_tests
