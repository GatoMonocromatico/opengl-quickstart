#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p ../build
g++ -std=c++20 -DDEBUG -I ../src -I ../Libraries/include -o ../build/test test.cpp
g++ -std=c++20 -Wall -Wextra -I ../src -o ../build/ui_tests ui_tests.cpp ../src/ui/TextEdit.cpp ../src/ui/Wrap.cpp
gcc -std=c99 -c -I ../Libraries/include -o ../build/cgltf_impl.o ../src/render/cgltf_impl.c
g++ -std=c++20 -Wall -Wextra -I ../src -I ../Libraries/include -o ../build/model_tests model_tests.cpp ../src/render/ModelData.cpp ../build/cgltf_impl.o
# text_tests needs the vendored FreeType/HarfBuzz, so it goes through CMake
cmake -S .. -B ../build-tests -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build ../build-tests --target text_tests -j >/dev/null
echo "Build OK"
../build/test
../build/ui_tests
../build/model_tests
../build-tests/text_tests
