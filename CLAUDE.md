# opengl-quickstart

## always pull before analysing
Before reading or analysing this codebase, run `git pull` on `master`. Local copies
(this folder, or the `external/opengl-quickstart` submodule in a game) go stale, and
an analysis of a stale copy reports things as missing that already exist.

## branches
`master` is the only long-lived branch. A feature gets its own branch only while it
is being developed end to end, and is merged into `master` and deleted as soon as it
is done. Don't leave work on side branches or unpushed.

## Libraries
`Libraries/` is third-party code (SDL3, glm, glad, GlyphGL, cgltf, ...), not engine code.
`Libraries/include` is read-only on purpose; don't edit it. The one exception is GlyphGL's atlas fix, marked `opengl-quickstart patch` in
`glyphgl/glyph_atlas.h`; keep it when updating GlyphGL.
