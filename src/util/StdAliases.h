#pragma once
#include <vector>
#include <array>

// Short names for long/nested std container shapes. The element type stays
// explicit through the template parameter, only the container spelling is compressed.

template<typename T>
using stdMat = std::vector<std::vector<T>>;

template<typename T>
using stdCube = std::vector<std::vector<std::vector<T>>>;

template<typename T, size_t N, size_t M>
using stdArrMat = std::array<std::array<T, N>, M>;

template<typename T, size_t N>
using vecArr = std::vector<std::array<T, N>>;

template<typename T, size_t N>
using arrVec = std::array<std::vector<T>, N>;
