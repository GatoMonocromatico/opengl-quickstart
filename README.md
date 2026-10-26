# opengl-quickstart

A **plug-and-play C++20 / OpenGL 3.3 Core quickstart** that builds and runs on **Linux and Windows** right after `git clone` — no system-wide library installs required. All third-party headers and pre-built binaries live inside `Libraries/`.

It is two things: a static library (`opengl_quickstart`) that apps pull in as a git submodule, and a small example app that shows how to use it. See [Use as a package](#use-as-a-package).

## Stack

| Piece | Role |
|---|---|
| **OpenGL 3.3 Core** | Graphics API (context created in `main.cpp`) |
| **GLAD** | Loads OpenGL function pointers — bundled as `glad.c` + `Libraries/include/glad/` |
| **SDL3** | Window, events, OpenGL context, timing |
| **SDL3_image** | PNG/JPG loading for `Texture` |
| **GLM** | Vectors, matrices, `lookAt`, `perspective` |
| **GlyphGL** | TrueType text for `Text` (header-only, MIT) |

All headers are under `Libraries/include/`. Pre-built libraries live under `Libraries/lib_linux/lib/` (Linux) and `Libraries/lib_win/lib/` (Windows). CMake picks the right folder automatically.

Language: **C++20**.

## Build

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run from the project root (shaders are loaded relative to the working directory):

```bash
./build/opengl_quickstart_example
```

Or use the helper script (builds, then runs the example under GDB for a crash backtrace):

```bash
./run.sh
```

### Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

SDL3.dll and SDL3_image.dll are automatically copied next to the `.exe` by a post-build step, so no manual DLL placement is needed.

### Release build

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

The `DEBUG` macro (and all `DebugLog.h` output) is enabled only in **Debug** builds; Release compiles it out completely.

### GPU selection (hybrid-graphics laptops)

On laptops with an integrated and a dedicated GPU, `initialize()` (`src/core/Window.cpp`) asks for the **dedicated** one:

- **Windows:** exports `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` from the exe. Windows Graphics Settings can still override this per app.
- **Linux:** before `SDL_Init`, sets `__NV_PRIME_RENDER_OFFLOAD=1` + `__GLX_VENDOR_LIBRARY_NAME=nvidia` (NVIDIA proprietary driver) or `DRI_PRIME=1` (Mesa). This is skipped on single-GPU machines and whenever you already set one of these variables yourself. `DRI_PRIME=0 ./build/opengl_quickstart_example` forces the integrated GPU.
- **macOS:** nothing needed. OpenGL apps already run on the dedicated GPU there.

If the dedicated GPU can't create a context, startup retries once on the default GPU. The chosen renderer is printed at startup (`OpenGL renderer: ...`).

## Controls

| Input | Action |
|---|---|
| **O** | Toggle mouse-look (hides cursor, camera follows mouse) |
| **P** | Reset camera to forward (-Z) |
| **W / S** | Move camera forward / back (Z axis) |
| **A / D** | Move camera left / right (X axis) |
| **Q / E** | Move camera up / down (Y axis) |
| **F12** | Save the current frame to `screenshot.png` |

### Debug env vars (example app)

| Variable | Effect |
|---|---|
| `QS_CAMERA=x,y,z[,yaw,pitch]` | Place the camera at startup (`applyCameraOverride`) |
| `QS_SHOT=<path>` | Render for `QS_SHOT_AFTER` seconds (default 2), save one PNG, exit (`saveScreenshot`); the widgets demo honours it too |

### Window options

`initialize(w, h, WindowOptions{...})`:

| Field | Default | Effect |
|---|---|---|
| `title`, `resizable`, `vsync`, `msaa` | | as named |
| `appId` | `nullptr` | SDL app id, set before `SDL_Init`: on X11 it is the `WM_CLASS` class (match it with an i3 `for_window [class="..."]` rule), on Wayland the `app_id` |
| `borderless` | `false` | no title bar / decorations |
| `alwaysOnTop` | `false` | keep above other windows (the WM may ignore it) |
| `transparent` | `false` | ARGB window: pixels cleared with alpha 0 show the desktop (needs a compositor such as picom) |
| `preferDedicatedGpu` | `true` | `false` skips the GPU selection above and stays on the default GPU, so a small always-running app doesn't wake the dedicated one |

## Text charsets

`Text` is drawn with GlyphGL in UTF-8 mode. `create()` takes a charset:

```cpp
Text text;
text.create(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", 18.0f, Text::Charset::Latin);
```

- `Charset::Ascii` (default): printable ASCII.
- `Charset::Latin`: ASCII + Latin-1 Supplement + Latin Extended-A + `✓ ✗ → ← • … – — “ ” ‘ ’`. About 3x the glyphs, so a bigger atlas.

The font must have the glyphs as simple (non-composite) outlines, GlyphGL skips composite ones. GlyphGL's rasteriser puts an edge's whole coverage in one pixel, so a stroke thinner than a pixel cancels out at small sizes (Atkinson Hyperlegible at 11 px drew `l` as a dot and `p` as an `o`): `create()` redraws an atlas of 24 px or less from the same rasteriser at 4x, each pixel the mean of its 4x4, with `Libraries/` untouched. `Libraries/include/glyphgl/glyph_atlas.h` carries one local patch (marked `opengl-quickstart patch`): the atlas packer no longer shifts glyphs onto a shared baseline, which hung short glyphs into the next row and cut them (lost descenders, bits of other glyphs). Keep it if GlyphGL is ever updated. `MesloLGSNerdFontMono-Regular.ttf` has all of the Latin set. A codepoint that isn't in the charset is drawn and measured as `?`; control characters and malformed UTF-8 never crash, they draw nothing or `?`.

Measuring for layout (all UTF-8 aware, byte offsets, `scale` like `draw`):

- `width(s, scale)`: advance of the string.
- `ascent(scale)` / `descent(scale)`: how far glyphs reach above / below the baseline; `lineHeight()` is the line pitch.
- `indexAt(s, x, scale)`: byte offset of the caret boundary nearest x, never inside a UTF-8 sequence. Use it to turn a click into a caret.

`draw(s, x, y, color, scale, opacity)` fades the whole string with `opacity` (default 1). For that `Text` builds GlyphGL with its own default fragment shader plus one `opacity` uniform, passed in as a custom effect, so `Libraries/` stays untouched.

`util/Utf8.h` has the codepoint helpers (`utf8::next/prev/snap/count/valid/sanitize`).

## UI widgets

`src/ui/` is a small 2D layer. The pieces work on their own, so a custom canvas can use `Painter2D` + `Text` + `TextEdit` + `wrap` without any widgets.

| Header | GL | What |
|---|---|---|
| `ui/Painter2D.h` | yes | batched rects, rounded rects, outlines, lines, circles, images and text in window pixels (top-left origin), in call order; stacks for clip, transform, alpha, dither and an app's own fragment shader; shaders `shaders/ui2d.*` |
| `ui/TextEdit.h` | no | UTF-8 string + caret + selection: insert, backspace/delete (Ctrl = word), arrows, home/end, up/down over wrapped lines, select all/word, cut/paste text in and out |
| `ui/Wrap.h` | no | `wrap(text, maxWidth, measure)` -> line ranges (word wrap, long words broken, `\n` kept); `lineOfOffset`, `indexInLine(s)` |
| `ui/Context.h` | yes | immediate-mode widgets, input, `Theme`, a pannable zoomable canvas, the redraw rule |
| `ui/Anim.h` | no | timers, tweens, springs and rolling numbers by id, eases, and what the loop needs to know to sleep |
| `ui/CanvasView.h` | no | the camera of a pannable, zoomable view: world to screen and back, zoom at a point, fit |
| `ui/Geometry.h` | no | `Rect`, `Transform2D` (offset + uniform scale) |
| `ui/Fonts.h` | yes | `Text` atlases made on first use, by face and pixel size |
| `ui/Color.h` | no | `rgb(0x283a35)`, `oklch(L, C, hue)` to sRGB |

```cpp
ui::Painter2D painter;  painter.create();
Text font;              font.create(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", 18, Text::Charset::Latin);
ui::Context ui;         ui.init(painter, font);
SDL_StartTextInput(state.window);

while (running)
{
    SDL_Event e;
    // event-driven: sleeps until input or a caret flip
    if (SDL_WaitEventTimeout(&e, ui.waitTimeoutMs()))
        do { ui.handleEvent(e); } while (SDL_PollEvent(&e));  // true = a text field wants that key
    if (!ui.needsRedraw()) continue;

    ui.beginFrame(state.width, state.height);          // drawable pixels
    if (ui.button({10, 10, 120, 32}, "Save")) save();
    ui.textField({10, 60, 400, 32}, "title", title);
    ui.endFrame();
    SDL_GL_SwapWindow(state.window);
}
```

See `example/widgets.cpp` for a full loop.

Widgets, all taking a pixel `Rect`: `button`, `checkbox`, `progressBar`, `textField` (single line, or `.multiline` with `.wrap`; caret, selection, mouse click / drag / double-click word / triple-click line, Ctrl+A/C/X/V, scrolls to the caret; `.selectOnFocus` selects it all when it's focused some way other than a click, for a rename), `tabBar` (returns `clicked` / `closed` index), `label`, `beginScroll` / `endScroll`. Widgets are identified by their label or id string; `"Text##id"` shows `Text`, `pushId()` scopes ids in loops. `Theme` holds colors, padding, radius and font scale.

Mouse helpers for custom widgets, the same ones the widgets use: `hovered(rect)`, `pressed`, `released`, `doubleClicked`, `mouseDown`, `dragDelta()`, `wheel()` / `consumeWheel()`, `pushClip/popClip` (clips hit-testing and drawing together). `clickArea(rect, id, &held)` is an invisible button for a widget the app draws itself: it takes the press like `button` (so a canvas under it doesn't pan), is true on the click, and `held` says it's being pressed.

Scopes on the context, each a push and a pop:

- `pushTransform(offset, scale)`: drawing and hit-testing together, so rects and `mousePos()` are in the pushed units (widgets in a zoomed view still hover and click).
- `pushDisabled()`: widgets draw but don't react, to the mouse or to keys (what's under a modal).
- `pushFont(text)`: labels, widgets and the text metrics use another face.

Notes: mouse coordinates are converted to drawable pixels, so high-DPI works. `ui::Context` ignores everything it doesn't handle, the app keeps its own key handling (check `wantsKeyboard()` first). `Theme` also holds `focusWidth`, and two accessibility settings: `motionScale` (1 full, 0 lands every animation at once; handed to `anim` each frame) and `reduceFlashing` (for the app; the module draws no flashes).

Demos: `./build/opengl_quickstart_widgets` (`QS_SHOT=<png>` saves a frame and exits, `QS_TAB=0|1|2` picks the tab) and `./build/opengl_quickstart_motion` (below). Tests for the non-GL parts: `tests/run_tests.sh` or `ctest` in the build dir (`ui_tests`: Utf8, Wrap, TextEdit, Anim, CanvasView, Color, Transform2D).

### Motion: `ui::Anim` and the redraw rule

`ui.anim` is ticked at `beginFrame` with `SDL_GetTicks()`. Nothing runs between frames: `needsRedraw()` and `waitTimeoutMs()` keep frames coming every `anim.frameMs` (33, so 30 fps) while something in it moves, wake the loop for a timer that's due, and let it sleep after. The loop above doesn't change.

```cpp
ui.anim.start("open", 400, [&] { opened = true; });            // a timer, 0 to 1, done runs once
float k = ui::ease(ui::Ease::Out, ui.anim.progress("open"));    // 1 once it ended (or never ran)
float x = ui.anim.tween({ "card", i }, picked == i ? 20.0f : 0.0f, 200);  // eases whenever the target changes
float y = ui.anim.spring("marker", targetY, 220, 16);           // overshoots, settles
float n = ui.anim.roll("score", score);                         // a mechanical counter: a unit every 40 ms, hard stops
float m = ui.anim.roll("gold", gold, { .stepMs = 70, .baseMs = 140, .maxMs = 900, .ease = ui::Ease::Back, .stepped = false });
ui.anim.after(1800, [&] { hint = false; });                     // waits without drawing frames
```

- Ids are strings, or a string and an index (`{ "card", i }`); timers, tweens, springs and rolls each have their own.
- A tween, spring or roll starts on its first target, so nothing animates in when it first shows.
- A timer started from another's `done` begins where that one ended, so chains don't drift with the frames.
- `start()` is for things that move (frames come while it runs); `after()` is for waiting (no frames until it's due).
- `roll(..., &ticks)` says how many whole units it passed since the last call (a tick sound per unit).
- `ui.minFrameMs` caps every redraw, input included (33 is 30 fps on a software renderer); 0 by default.

### Canvas: a pannable, zoomable view

```cpp
const ui::CanvasInput in = ui.beginCanvas(view, "chart");     // drag pans, the wheel zooms at the pointer
for (const Card& c : cards) drawCard(c);                      // world units; ui.hovered(c.rect) works here
ui.endCanvas();
if (in.clicked) select(cardAt(in.world));                     // a press that moved less than 3 px
ui.canvas("chart").fit(worldRect, view, 8);                   // or zoomAt(point, factor, view), min/maxZoom
ui.flyCanvas("chart", cardCentre, 500);                       // glides there (InOut), a drag cuts it short
```

Widgets inside take their presses first; a press nobody took is the canvas's. Its origin sits on a whole pixel, so 1 px lines stay crisp at zoom 1, and the wheel keeps the point drawn under the pointer there. For sharp text under a zoom, make the font at the size it's shown and draw it at `1 / zoom` (`ui::Fonts` keeps one atlas per size).

### Painter2D: transforms, alpha, dither, images, an app's own shader

```cpp
p.pushTransform({ 40, 20 }, 1.5f);   // positions, sizes, radii, line widths, text and clips
p.pushAlpha(0.5f);                   // multiplies (shapes, images and text alike)
p.pushDither(0.5f);                  // only the pixels of a 4x4 Bayer pattern under 0.5: a checker
p.rect(0, 0, 100, 60, ink);
p.popDither(); p.popAlpha(); p.popTransform();
p.image(capture.texture, dx, y, w, h, glm::vec4(1), { 0, 1 - y / H, 1, 1 - (y + h) / H });  // a band of a frame
```

Text goes into the same batch as the shapes (sampling the font's atlas), so its color's alpha counts, it follows the transform, and a row of shapes and labels is one draw call. `Text::draw` itself is unchanged (rgb only).

An app's own fragment shader draws Painter2D's quads with whatever look it wants:

```cpp
int foil = p.addProgram("shaders/foil.frag");       // compiled with ui2d.vert
p.bindTexture(foil, "uLut", lutTexture);            // bound on units 1, 2, ... whenever it draws
glUseProgram(p.glProgram(foil)); glUniform2f(...);  // its uniforms: before its first quad in a frame
p.pushProgram(foil);
p.params({ strength, seed, 0, 0 });                 // per quad: the shader's vParam0 (and vParam1)
p.roundedRect(x, y, w, h, 2, glm::vec4(1));         // any shape, image or text
p.popProgram();
```

The shader starts with `#version 330 core` and `#include <ui2d.glsl>` (angle brackets: the stock shader folder), which declares what each quad carries: `vColor`, `vLocal` (pixels from its centre, along its own axes), `vShape` (half size, radius, border), `vUvRect`, `vMode` (0 shape, 1 image, 2 glyph), `vParam0`, `vParam1`, `vPixel` (the pixel in the target, top-left origin); and the helpers `ui2dCoverage()` (the shape's antialiased edge, or a glyph's ink), `ui2dColor()` (what Painter2D's own shader draws), `ui2dUv()` and `ui2dBayer(pixel, cell)` (the 4x4 ordered-dither threshold). See `example/shaders/sheen.frag`.

The motion demo, `./build/opengl_quickstart_motion`, shows all of it: a board of cards on a canvas, a reveal drawn with its own shader and grown by a tween, a marker on a spring, two rolling counters in digit slots (clip + a second font), a modal's dithered checker over a disabled canvas, a fading line (alpha), and a band of the frame blitted back shifted (`ScreenCapture` + `image` with a source rect). It counts the frames it draws, so you can see them stop when nothing moves. `QS_SHOT=<png>` saves a frame `QS_SHOT_MS` (300) in, with a card picked and the counters rolling; `QS_DIM=1` puts the board behind the modal; `QS_SLIP=1` catches the band blit.

## Repository layout

```
opengl-quickstart/
├── CMakeLists.txt        # opengl_quickstart library + example (example only when top-level)
├── src/                  # the package: include root, so headers are "render/Mesh.h", "util/DebugLog.h", ...
│   ├── glad.c            # GLAD loader: do not edit unless regenerating
│   ├── core/
│   │   └── Window.h/.cpp # SDLState, WindowOptions, initialize/cleanup, dedicated-GPU selection
│   ├── render/
│   │   ├── Mesh.h/.cpp           # VAO+VBO+EBO geometry, instanced draw, normal generation, updateVertices
│   │   ├── VAO/VBO/EBO.h/.cpp    # buffer object wrappers (Vertex lives in VBO.h)
│   │   ├── shaderClass.h/.cpp    # compile/link GLSL; expands `#include "file"` (relative) and `<file>` (stock shaders)
│   │   ├── texture.h/.cpp        # 2-D texture loading via SDL_image
│   │   ├── camera.h/.cpp         # FPS-style camera: perspective or ortho proj*view, shader upload
│   │   ├── Framebuffer.h/.cpp    # offscreen render target + ScreenCapture (copy of the drawn frame)
│   │   ├── Light.h               # CPU mirror of GLSL Light struct (std140)
│   │   ├── animation.h           # sprite-sheet grid animator (UV offset per frame)
│   │   ├── point.h/.cpp          # GL_POINTS helper (separate pipeline)
│   │   └── RoundedCorner2D.h/.cpp # round a triangle-mesh corner in 2D
│   ├── ui/
│   │   ├── Painter2D.h/.cpp      # batched 2D shapes + images + text; clip, transform, alpha, dither, own shaders
│   │   ├── TextEdit.h/.cpp       # caret/selection/edit model (no GL)
│   │   ├── Wrap.h/.cpp           # word wrap over a measure function (no GL)
│   │   ├── Context.h/.cpp        # immediate-mode widgets, input, Theme, canvas, redraw rule
│   │   ├── Anim.h/.cpp           # timers, tweens, springs, rolling numbers (no GL)
│   │   ├── CanvasView.h/.cpp     # pan/zoom camera math (no GL)
│   │   ├── Geometry.h            # Rect, Transform2D (no GL)
│   │   ├── Fonts.h/.cpp          # Text atlases by face and size
│   │   └── Color.h               # rgb(hex), oklch (no GL)
│   └── util/
│       ├── Utf8.h                # UTF-8 decode / step / validate helpers
│       ├── DebugLog.h            # zero-cost debug logging macros (see below)
│       ├── DebugHooks.h/.cpp     # saveScreenshot, applyCameraOverride
│       ├── Timer.h               # interval flag used by FPS counter and Animation
│       ├── StdAliases.h          # stdMat<T>, stdCube<T>, arrVec<T,N>, ...
│       └── mathUtils.h/.cpp      # dsin / dcos (degrees)
│
├── shaders/              # stock shaders, found at runtime through QS_SHADER_DIR
│   ├── default.vert/.frag  # main mesh pipeline (instancing, lighting, mask alpha)
│   ├── point.vert/.frag    # GL_POINTS pipeline
│   ├── ui2d.vert/.frag     # Painter2D's quads
│   └── ui2d.glsl           # what ui2d.vert hands a fragment shader, and helpers (#include <ui2d.glsl>)
│
├── fonts/                # bundled .ttf fonts for Text, found through QS_FONT_DIR (licenses in fonts/README.md)
│
├── example/              # app-level template: copy these into a new project and grow them
│   ├── main.cpp          # init, main loop, cleanup
│   ├── widgets.cpp       # UI widgets demo (opengl_quickstart_widgets), event-driven loop
│   ├── motion.cpp        # motion demo (opengl_quickstart_motion): Anim, canvas, own shader (shaders/sheen.frag)
│   ├── GameState.h       # scene objects, FPS
│   ├── Resources.h/.cpp  # central GPU asset store (shaders, meshes, textures, lights, UBO)
│   ├── gameObject.h/.cpp # logical entity: indexes a Mesh in Resources + own transform
│   └── Textures/         # sample textures
│
├── tests/                # RoundedCorner2D tests (test.cpp) and ui_tests.cpp (Utf8, Wrap, TextEdit, Anim, CanvasView, Color, Transform2D); run_tests.sh builds and runs both
├── Libraries/
│   ├── include/          # GLAD, GLM, SDL3, SDL3_image, stb, GlyphGL headers
│   ├── lib_linux/lib/    # Linux shared objects
│   └── lib_win/lib/      # Windows import libs + DLLs
└── run.sh                # build + run the example under GDB (Linux only)
```

## Use as a package

Add the repo as a submodule and link the library. Everything (headers, SDL, GLM, GL, the `DEBUG` define on Debug builds) comes through the target.

```bash
git submodule add git@github.com:GatoMonocromatico/opengl-quickstart.git external/opengl-quickstart
```

```cmake
add_subdirectory(external/opengl-quickstart)

add_executable(my_app src/main.cpp ...)
target_link_libraries(my_app PRIVATE opengl_quickstart)
quickstart_setup_executable(my_app)   # copies SDL DLLs (Windows) / sets rpath (Linux)
```

```cpp
#include "core/Window.h"
#include "render/Mesh.h"

SDLState state = initialize(1280, 720, { .title = "My App", .resizable = false, .vsync = false });
Shader shader(QS_SHADER_DIR "default.vert", "assets/shaders/my.frag");
```

- `QS_SHADER_DIR` is the absolute path (trailing slash) to `shaders/`. Use the stock shaders from there, or ship your own and load them relative to the working dir.
- `QS_FONT_DIR` is the same for `fonts/`: `text.create(QS_FONT_DIR "VT323-Regular.ttf", 32.0f)`.
- `example/` is not part of the library. Your app owns its `main.cpp`, `Resources`, `GameState` and `GameObject`.
- Keep the library static: the Windows dedicated-GPU exports in `core/Window.cpp` only work from inside the executable.

## Adding your own geometry

1. **Define vertices and indices** — `Vertex` is declared in `VBO.h` (position, texcoord, mask texcoord, normal, instance offset, luminance).
2. **Populate `Resources::load()`** in `example/Resources.cpp` — upload meshes, load textures, set up lights, create the UBO.
3. **Add a `GameObject`** to `GameState::scenarioObjects` pointing at your mesh index.
4. **Call `Draw`** in the main loop (see `gs.scenarioObjects[0].Draw(...)` in `example/main.cpp`).

### Shader uniforms expected by `default.vert` / `default.frag`

| Uniform | Type | Purpose |
|---|---|---|
| `camMatrix` | `mat4` | Combined projection × view (from `Camera::updateMatrix`) |
| `model` | `mat4` | Per-object model transform |
| `texCoordOffset` | `vec2` | Sprite-sheet animation UV offset |
| `tex0` | `sampler2D` | Color texture (unit 0) |
| `mask0` | `sampler2D` | Alpha mask texture (unit 1) |
| `numScenarioLights` | `int` | Active lights in the `ScenarioLights` UBO block |

Lights are streamed each frame via `glBufferSubData` into a UBO bound to GLSL `layout(std140) uniform ScenarioLights`.

---

## DebugLog.h

Zero-overhead debug logging controlled by the `DEBUG` preprocessor macro. CMake defines `DEBUG` in **Debug** builds only; in Release every macro compiles away to `((void)0)` with no runtime cost.

Include it anywhere you need logging — it is already pulled into every project header.

### Macros

#### `DBG(value)` — print one value

```cpp
DBG(42);
DBG("Hello world");
DBG(someFloatVariable);
```

Prints:  `[filename.cpp] <value>`

Two-argument form adds a label:

```cpp
DBG("vertexCount", mesh.vertices.size());
```

Prints: `[filename.cpp] vertexCount=42`

---

#### `MDBG(...)` — print multiple fields on one line

Separates each argument with ` | ` after a single file prefix.

```cpp
MDBG("phase", "init done", "width", state.width, "height", state.height);
```

Prints: `[main.cpp] phase | init done | width | 1600 | height | 900`

Combine with `DBG_N` to get `name=value` pairs:

```cpp
MDBG(DBG_N("x", pos.x), DBG_N("y", pos.y), DBG_N("z", pos.z));
```

Prints: `[camera.cpp] x=1.500000 | y=0.000000 | z=-3.200000`

---

#### `DBG_N(name, value)` — named field for use inside `MDBG`

```cpp
MDBG(DBG_N("dt", deltaTime), DBG_N("fps", gs.fps));
```

---

#### `DBG_F6(value)` — float formatted to 6 decimal places

Wraps any numeric value so it prints as `fixed` with six decimal places without affecting the stream flags of surrounding output.

```cpp
MDBG(DBG_N("angle", DBG_F6(yaw)), DBG_N("pitch", DBG_F6(pitch)));
```

Prints: `[camera.cpp] angle=−90.000000 | pitch=0.000000`

---

#### `DBG_IF(cond, ...)` / `MDBG_IF(cond, ...)` — runtime-gated output

Same signatures as `DBG` and `MDBG` but only emit when `cond` is true.

Everything in the package gates on one global switch, `flux::verbose` (default `true`). Flip it once at startup to silence or re-enable all of it:

```cpp
flux::verbose = false;

DBG_IF(flux::verbose, "vertexCount", verts.size());
MDBG_IF(flux::verbose, DBG_N("min", aabb.min), DBG_N("max", aabb.max));
```

The condition is evaluated at runtime only in Debug builds. In Release both macros expand to `((void)0)` — the condition is never evaluated and the call compiles out entirely.

---

### Release behaviour

In Release, all six macros expand to `((void)0)`:

```cpp
#define DBG(...)         ((void)0)
#define MDBG(...)        ((void)0)
#define DBG_N(n, v)      ((void)0)
#define DBG_F6(x)        ((void)0)
#define DBG_IF(c, ...)   ((void)0)
#define MDBG_IF(c, ...)  ((void)0)
```

The compiler eliminates them entirely — no branches, no string literals, no I/O.

---

## License

MIT — see `LICENSE`.
