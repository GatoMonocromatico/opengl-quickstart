// -----------------------------------------------------------------------------
// ui2d.glsl -- what Painter2D's vertex stage hands an app's fragment shader (GL)
// -----------------------------------------------------------------------------
// An app's own program (Painter2D::addProgram, GL only) starts with
// `#version 330 core` and `#include <ui2d.glsl>`, then its main() uses these as
// it likes. The helpers are in ui2d_common.glsl; Painter2D's own shader is
// gpu/ui2d.frag, the same helpers over GLSL 450 declarations.

in vec4 vColor;    // rgba, alpha already times pushAlpha
in vec2 vLocal;    // pixels from the quad's centre, along its own axes (a line runs along x)
in vec4 vShape;    // half width, half height, corner radius, border width (0 = filled), pixels
in vec4 vUvRect;   // the part of uTex an image or glyph shows: u0, v0 (top-left), u1, v1
in float vMode;    // 0 shape, 1 image, 2 glyph (Text), 3 glyph (ShapedText), 4 colour glyph (ShapedText)
in vec4 vParam0;   // Painter2D::params()
in vec4 vParam1;
in vec2 vPixel;    // this pixel in the target, origin top-left

uniform sampler2D uTex;                 // unit 0: the image, or the font's atlas (ShapedText's is premultiplied RGBA)
uniform vec2 uDither = vec2(1.0, 1.0);  // pushDither: strength (1 = everything shows), cell px

#include <ui2d_common.glsl>
