# OpenGL dev environment

A small, **plug-and-play C++ base** for learning and extending OpenGL: SDL3 window and input, GLAD for OpenGL 3.3 Core, GLM for math, and lightly abstracted wrappers (VAO, VBO, EBO, shaders, textures, mesh, camera). The code is commented so you can follow the pipeline step by step and replace pieces as you grow out of them.

> **Note:** `glad.c` / `Libraries/include/glad/` are generated/third-party loader code; the rest of the sources are the project’s own starting point.

## Stack

| Piece | Role |
|--------|------|
| **OpenGL 3.3 Core** | Graphics API (context requested in `main.cpp`) |
| **GLAD** | Loads OpenGL function pointers after context creation |
| **SDL3** | Window, events, OpenGL context, timing |
| **SDL3_image** | Loading images for `Texture` (e.g. PNG) |
| **GLM** | Vectors, matrices, `lookAt`, `perspective` |

Language: **C++20** (see `OpenGL-dev-env.vcxproj`).

## Repository layout

```
OpenGL-dev-env/
├── main.cpp              # Entry: init, main loop, cleanup
├── GameState.h           # SDL bundle + simple game state
├── Resources.h/.cpp      # Scene assets (meshes, lights, shaders, UBO, …)
├── Mesh.* / VAO.* / VBO.* / EBO.*
├── shaderClass.*         # Load & compile GLSL, link program
├── texture.*             # 2D textures via SDL_image
├── camera.* / gameObject.* / point.*
├── default.vert / default.frag   # Main mesh pipeline
├── point.vert / point.frag     # GL_POINTS helper pipeline
├── glad.c                # GLAD (do not edit unless regenerating)
└── Libraries/
    ├── include/          # GLM, SDL3, glad, …
    └── lib/              # .lib files for MSVC
```

Shaders are loaded from paths **relative to the process working directory**, so run the executable from the folder that contains `default.vert` / `default.frag` (or adjust paths in code).

## Prerequisites

- **Windows** (project is set up for **Visual Studio** / MSVC).
- **Visual Studio 2022** (or newer) with the **Desktop development with C++** workload.
- **GPU drivers** that support OpenGL 3.3 Core.

Third-party binaries and headers are expected under `Libraries\include` and `Libraries\lib`, with **SDL3** and **SDL3_image** DLLs available next to the built `.exe` at runtime (the project lists `SDL3.dll` and `SDL3_image.dll` as content—copy them if they are not already beside the output).

## Build (Visual Studio)

1. Clone the repository.
2. Open **`OpenGL-dev-env.slnx`** (or **`OpenGL-dev-env.vcxproj`**).
3. Select a configuration (**Debug** recommended first) and platform (**x64**).
4. **Important for forks and other machines:** the `.vcxproj` may contain **absolute** `IncludePath` / `LibraryPath` entries pointing at the original author’s machine. Replace them with paths relative to the project, for example:
   - Include: `$(ProjectDir)Libraries\include`
   - Library: `$(ProjectDir)Libraries\lib`
5. Ensure **Release** configurations link the same libraries as Debug if you hit unresolved externals (`SDL3.lib`, `SDL3_image.lib`, `opengl32.lib`, and `glm.lib` as in the Debug settings).
6. Build **Build → Build Solution**.

Output executable location follows the default MSVC layout (e.g. `OpenGL-dev-env\x64\Debug\`).

## Run

- Place **`SDL3.dll`** and **`SDL3_image.dll`** next to the `.exe` (or on the PATH) if Windows reports missing DLLs.
- Run with the **working directory** set to the directory that contains the shader files (project root is typical during development).

### Default controls (from `main.cpp`)

| Input | Action |
|--------|--------|
| **O** | Toggle mouse-look (relative mouse mode) |
| **P** | Reset camera orientation |
| **Arrow keys** | Move camera on XZ |
| **Left Ctrl / Left Shift** | Move camera down / up |

The sample **`Resources::load()`** is mostly commented out; uncomment and supply your own meshes/textures when you want geometry on screen.

## Learning path

1. **`initialize()`** in `main.cpp` — context, GLAD, global GL state (`DEPTH_TEST`, `BLEND`, …).
2. **`Shader`** — how vertex/fragment sources become a program.
3. **`Mesh`** — VAO layout matches `default.vert` attribute locations.
4. **`Camera::updateMatrix`** — `proj * view` uploaded as `camMatrix`.
5. **`GameObject::Draw`** — textures, uniforms, `glDrawElements`.

## Contributing

Pull requests are welcome. If you change the build, prefer **relative** `$(ProjectDir)` paths in the `.vcxproj` so clones build without editing machine-specific paths.

## License

This project is released under the [MIT License](LICENSE).
