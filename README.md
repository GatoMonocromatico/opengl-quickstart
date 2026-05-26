# fluxograme

A **plug-and-play C++20 / OpenGL 3.3 Core quickstart** that builds and runs on **Linux and Windows** right after `git clone` — no system-wide library installs required. All third-party headers and pre-built binaries live inside `Libraries/`.

## Stack

| Piece | Role |
|---|---|
| **OpenGL 3.3 Core** | Graphics API (context created in `main.cpp`) |
| **GLAD** | Loads OpenGL function pointers — bundled as `glad.c` + `Libraries/include/glad/` |
| **SDL3** | Window, events, OpenGL context, timing |
| **SDL3_image** | PNG/JPG loading for `Texture` |
| **GLM** | Vectors, matrices, `lookAt`, `perspective` |

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
./build/fluxograme
```

Or use the helper script (builds then runs under GDB for a crash backtrace):

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

## Controls

| Input | Action |
|---|---|
| **O** | Toggle mouse-look (hides cursor, camera follows mouse) |
| **P** | Reset camera to forward (-Z) |
| **W / S** | Move camera forward / back (Z axis) |
| **A / D** | Move camera left / right (X axis) |
| **Q / E** | Move camera up / down (Y axis) |

## Repository layout

```
fluxograme/
├── main.cpp              # Entry: init, main loop, cleanup
├── GameState.h           # SDLState (window+context bundle) + GameState (scene objects, FPS)
├── Resources.h/.cpp      # Central GPU asset store (shaders, meshes, textures, lights, UBO)
│
├── Mesh.h/.cpp           # VAO+VBO+EBO geometry, instanced draw, normal generation
├── VAO.h/.cpp            # Vertex Array Object wrapper
├── VBO.h/.cpp            # Vertex Buffer Object wrapper
├── EBO.h/.cpp            # Element Buffer Object wrapper
├── shaderClass.h/.cpp    # Compile/link GLSL stages into a program object
├── texture.h/.cpp        # 2-D texture loading via SDL_image
│
├── camera.h/.cpp         # FPS-style camera: yaw/pitch, proj*view matrix, shader upload
├── gameObject.h/.cpp     # Logical entity: indexes a Mesh in Resources + own transform
├── Light.h               # CPU mirror of GLSL Light struct (std140)
├── animation.h           # Sprite-sheet grid animator (UV offset per frame)
├── Timer.h               # Interval flag used by FPS counter and Animation
├── point.h/.cpp          # GL_POINTS helper (separate pipeline)
├── RoundedCorner2D.h/.cpp # Mesh utility: round a triangle-mesh corner in 2D
│
├── DebugLog.h            # Zero-cost debug logging macros (see section below)
│
├── default.vert/.frag    # Main mesh pipeline (instancing, lighting, mask alpha)
├── point.vert/.frag      # GL_POINTS pipeline
│
├── glad.c                # GLAD loader — do not edit unless regenerating
├── Libraries/
│   ├── include/          # GLAD, GLM, SDL3, SDL3_image headers
│   ├── lib_linux/lib/    # Linux shared objects
│   └── lib_win/lib/      # Windows import libs + DLLs
│
├── Textures/             # Sample textures (block.png, no-mask.png)
└── run.sh                # Build + run under GDB (Linux only)
```

## Adding your own geometry

1. **Define vertices and indices** — `Vertex` is declared in `VBO.h` (position, texcoord, mask texcoord, normal, instance offset, luminance).
2. **Populate `Resources::load()`** in `Resources.cpp` — upload meshes, load textures, set up lights, create the UBO.
3. **Add a `GameObject`** to `GameState::scenarioObjects` pointing at your mesh index.
4. **Call `Draw`** in the main loop (see `gs.scenarioObjects[0].Draw(...)` in `main.cpp`).

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

Same signatures as `DBG` and `MDBG` but only emit when `cond` is true. Use this to flip logging on or off for a specific function or block without touching the global `DEBUG` flag.

```cpp
void buildMesh(bool verbose = false)
{
    DBG_IF(verbose, "enter buildMesh");
    DBG_IF(verbose, "vertexCount", verts.size());

    MDBG_IF(verbose, DBG_N("min", aabb.min), DBG_N("max", aabb.max));
}
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
