#include <glm/glm.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include "DebugLog.h"

// ---------------------------------------------------------------------------
// Minimal stubs — only the fields the two functions under test actually touch.
// ---------------------------------------------------------------------------

constexpr float kEpsilon = 1e-5f;

struct Vertex
{
    struct { float x, y, z; } position{};
};

struct ParametricDesc
{
    double coeficient = 0;
    double constant   = 0;
};

struct Line
{
    glm::vec2                   direction{};
    std::vector<ParametricDesc> parametricEquations{};
};

// ---------------------------------------------------------------------------

static glm::vec2 safeNormalize2(const glm::vec2& v)
{
    const float len = glm::length(v);
    if (len < kEpsilon) return glm::vec2(0.0f);
    return v / len;
}

// Extracted verbatim from the lambda at RoundedCorner2D.cpp:371
static double getCosBetween(glm::vec2 v1, glm::vec2 v2)
{
    return (v1.x * v2.x + v1.y * v2.y) / (double)(glm::length(v1) * glm::length(v2));
}

// Extracted verbatim from the lambda at RoundedCorner2D.cpp:375
static glm::vec2 getDistanceDirectorVecToSepLine(
    unsigned int                idx,
    const std::vector<Vertex>&  vertices,
    const Line&                 separationLine)
{
    const Vertex& myPoint = vertices[idx];
    glm::vec2 pointAtSeparationLine = glm::vec2(
        (float)separationLine.parametricEquations[0].constant,
        (float)separationLine.parametricEquations[1].constant);

    glm::vec2 myPointToPointOnSepLine =
     pointAtSeparationLine - glm::vec2(myPoint.position.x, myPoint.position.y);
    MDBG(DBG_N("myPointToPointOnSepLine.x", myPointToPointOnSepLine.x), DBG_N("myPointToPointOnSepLine.y", myPointToPointOnSepLine.y));
    
    glm::vec2 triangleAdjacentVec = 
    (float)(glm::length(myPointToPointOnSepLine)
    * getCosBetween(myPointToPointOnSepLine, separationLine.direction))
    * separationLine.direction;

    MDBG(DBG_N("triangleAdjacentVec.x", triangleAdjacentVec.x), DBG_N("triangleAdjacentVec.y", triangleAdjacentVec.y));
    
    // not ready yet, needs verification to add or subtract triangleAdjacentVec
    glm::vec2 vectorDistanceToSepLineTest1 = myPointToPointOnSepLine + triangleAdjacentVec;
    glm::vec2 vectorDistanceToSepLineTest2 = myPointToPointOnSepLine - triangleAdjacentVec;
    
    if (glm::length(vectorDistanceToSepLineTest1) < glm::length(vectorDistanceToSepLineTest2))
    {
        return safeNormalize2(vectorDistanceToSepLineTest1);
    }
    else return safeNormalize2(vectorDistanceToSepLineTest2);
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

static int gPassed = 0, gFailed = 0;

static bool approxEq(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

static bool approxEqVec(glm::vec2 a, glm::vec2 b, float eps = 1e-4f)
{
    MDBG(DBG_N("a.x", a.x), DBG_N("b.x", b.x), DBG_N("a.y", a.y), DBG_N("b.y", b.y));
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps);
}

#define TEST(name, cond) \
    do { \
        bool _ok = (cond); \
        std::cout << (_ok ? "[PASS] " : "[FAIL] ") << (name) << "\n\n"; \
        if (_ok) ++gPassed; else ++gFailed; \
    } while (0)

// ---------------------------------------------------------------------------
// Tests: getCosBetween
// ---------------------------------------------------------------------------

static void testGetCosBetween()
{
    // Perpendicular: cos(90°) = 0.0
    TEST("cosBetween: perpendicular (1,0)·(0,1)  == 0.0",
         approxEq((float)getCosBetween({1,0}, {0,1}), 0.0f));

    // Parallel: cos(0°) = 1.0
    TEST("cosBetween: parallel      (1,0)·(1,0)  == 1.0",
         approxEq((float)getCosBetween({1,0}, {1,0}), 1.0f));

    // Antiparallel: cos(180°) = -1.0
    TEST("cosBetween: antiparallel  (1,0)·(-1,0) == -1.0",
         approxEq((float)getCosBetween({1,0}, {-1,0}), -1.0f));

    // 45°: cos(45°) = 1/√2 ≈ 0.7071
    TEST("cosBetween: 45-degree     (1,0)·(1,1)  ≈ 0.7071",
         approxEq((float)getCosBetween({1,0}, {1,1}), 1.0f / std::sqrt(2.0f)));

    // Symmetry: order of arguments must not matter
    TEST("cosBetween: symmetric     f(a,b) == f(b,a)",
         approxEq((float)getCosBetween({3,1}, {1,2}),
                  (float)getCosBetween({1,2}, {3,1})));
}

// ---------------------------------------------------------------------------
// Tests: getDistanceDirectorVecToSepLine
//
// parametricEquations[0].constant = point-on-line x at t=0
// parametricEquations[1].constant = point-on-line y at t=0
//
// For each test the expected value is worked out by hand using the actual
// formula: result = safeNormalize2(v + ((v·d)/2) * d)
// where v = pointOnLine - myPoint, d = separationLine.direction.
// ---------------------------------------------------------------------------

static void testGetDistanceDirectorVecToSepLine()
{
    std::vector<Vertex> verts(1);

    auto makeLine = [](glm::vec2 dir, double cx, double cy) {
        Line l;
        l.direction = dir;
        l.parametricEquations.resize(2);
        l.parametricEquations[0].constant = cx;
        l.parametricEquations[1].constant = cy;
        return l;
    };

    // Case 1 ----------------------------------------------------------------
    // Line: horizontal, d=(1,0), passes through (0,1)
    // Point: (0,0)  →  v=(0,1)
    // v·d = 0  →  addend = 0  →  result = safeNormalize2((0,1)) = (0,1)
    {
        verts[0].position = {0.0f, 0.0f, 0.0f};
        Line sep = makeLine({1.0f, 0.0f}, 0.0, 1.0);
        TEST("distVec: point below horizontal line → ( 0, 1)",
             approxEqVec(getDistanceDirectorVecToSepLine(0, verts, sep), {0.0f, 1.0f}));
    }

    // Case 2 ----------------------------------------------------------------
    // Line: vertical, d=(0,1), passes through (1,0)
    // Point: (0,0)  →  v=(1,0)
    // v·d = 0  →  addend = 0  →  result = (1,0)
    {
        verts[0].position = {0.0f, 2.0f, 0.0f};
        Line sep = makeLine({1.0f, 0.0f}, 0.0, 1.0);

        TEST("distVec: point above horizontal line  → ( 0, -1)",
             approxEqVec(getDistanceDirectorVecToSepLine(0, verts, sep), {0.0f, -1.0f}));
    }
        // Case 3 ----------------------------------------------------------------
    // Line: horizontal, d=(1,0), passes through (0,1)
    // Point: (0,0)  →  v=(0,1)
    // v·d = 0  →  addend = 0  →  result = safeNormalize2((0,1)) = (0,1)
    {
        verts[0].position = {2.0f, -12.0f, 0.0f};
        Line sep = makeLine({1.0f, 0.0f}, 0.0, 1.0);
        TEST("distVec: point below horizontal line → ( 0, 1)",
             approxEqVec(getDistanceDirectorVecToSepLine(0, verts, sep), {0.0f, 1.0f}));
    }

    // Case 4 ----------------------------------------------------------------
    // Line: vertical, d=(0,1), passes through (1,0)
    // Point: (0,0)  →  v=(1,0)
    // v·d = 0  →  addend = 0  →  result = (1,0)
    {
        verts[0].position = {2.0f, 14.0f, 0.0f};
        Line sep = makeLine({1.0f, 0.0f}, 0.0, 1.0);
        TEST("distVec: point above horizontal line  → ( 0, -1)",
             approxEqVec(getDistanceDirectorVecToSepLine(0, verts, sep), {0.0f, -1.0f}));
    }

    // Case 5 ----------------------------------------------------------------
    // Line: horizontal, d=(1,0), passes through (0,1)
    // Point: (0,2) above the line  →  v=(0,-1)
    // v·d = 0  →  addend = 0  →  result = (0,-1)
    {
        verts[0].position = {0.0f, 2.0f, 0.0f};
        Line sep = makeLine({1.0f, 0.0f}, 0.0, 1.0);
        TEST("distVec: point above horizontal line  → ( 0,-1)",
             approxEqVec(getDistanceDirectorVecToSepLine(0, verts, sep), {0.0f, -1.0f}));
    }

    // Case 6 ----------------------------------------------------------------
    // Line: diagonal, d=(1/√2,1/√2), passes through (0,0)
    // Point: (1,0)  →  v=(-1,0)
    {
        const float d = std::sqrt(2.0f) / 2.0f;
        verts[0].position = {1.0f, 0.0f, 0.0f};
        Line sep = makeLine({d, d}, 0.0, 0.0);

        glm::vec2 expected = {-d, d};
        TEST("distVec: diagonal line, off-axis point",
             approxEqVec(getDistanceDirectorVecToSepLine(0, verts, sep), expected));
    }
}

// ---------------------------------------------------------------------------

int main()
{
    std::cout << "=== getCosBetween ===\n";
    testGetCosBetween();

    std::cout << "\n=== getDistanceDirectorVecToSepLine ===\n";
    testGetDistanceDirectorVecToSepLine();

    const bool ok = (gFailed == 0);
    std::cout << "\n" << gPassed << " passed, " << gFailed << " failed.\n";
    return ok ? 0 : 1;
}
