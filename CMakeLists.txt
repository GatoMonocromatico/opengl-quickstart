cmake_minimum_required(VERSION 3.20)

project(opengl_quickstart LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(QS_LIBRARIES_DIR "${QS_ROOT}/Libraries")
set(QS_INCLUDE_DIR "${QS_LIBRARIES_DIR}/include")

if(WIN32)
    set(QS_LIB_DIR "${QS_LIBRARIES_DIR}/lib_win/lib")
elseif(UNIX)
    set(QS_LIB_DIR "${QS_LIBRARIES_DIR}/lib_linux/lib")
else()
    message(FATAL_ERROR "Unsupported platform for local library layout.")
endif()

if(NOT EXISTS "${QS_INCLUDE_DIR}" OR NOT EXISTS "${QS_LIB_DIR}")
    message(FATAL_ERROR "Expected local SDK folders at Libraries/include and platform-specific lib directory.")
endif()

# ---------------------------------------------------------------------------
# opengl_quickstart: the engine layer apps link against.
# Must stay STATIC: core/Window.cpp carries the dedicated-GPU exports that only
# work from inside the .exe.
# ---------------------------------------------------------------------------
add_library(opengl_quickstart STATIC
    src/glad.c
    src/core/Window.cpp
    src/render/camera.cpp
    src/render/EBO.cpp
    src/render/Framebuffer.cpp
    src/render/Mesh.cpp
    src/render/Model.cpp
    src/render/ModelData.cpp
    src/render/cgltf_impl.c
    src/render/point.cpp
    src/render/RoundedCorner2D.cpp
    src/render/shaderClass.cpp
    src/render/Text.cpp
    src/render/texture.cpp
    src/render/VAO.cpp
    src/render/VBO.cpp
    src/ui/Context.cpp
    src/ui/Painter2D.cpp
    src/ui/TextEdit.cpp
    src/ui/Wrap.cpp
    src/util/DebugHooks.cpp
    src/util/mathUtils.cpp
)
add_library(opengl_quickstart::opengl_quickstart ALIAS opengl_quickstart)

target_include_directories(opengl_quickstart
    PUBLIC
        "${QS_ROOT}/src"
        "${QS_INCLUDE_DIR}"
)

if(WIN32)
    set(SDL3_LIBRARY "${QS_LIB_DIR}/SDL3.lib")
    set(SDL3_IMAGE_LIBRARY "${QS_LIB_DIR}/SDL3_image.lib")
else()
    find_library(SDL3_LIBRARY NAMES SDL3 PATHS "${QS_LIB_DIR}" NO_DEFAULT_PATH)
    find_library(SDL3_IMAGE_LIBRARY NAMES SDL3_image PATHS "${QS_LIB_DIR}" NO_DEFAULT_PATH)
endif()

if(NOT SDL3_LIBRARY)
    message(FATAL_ERROR "Could not find SDL3 library in ${QS_LIB_DIR}.")
endif()
if(NOT SDL3_IMAGE_LIBRARY)
    message(FATAL_ERROR "Could not find SDL3_image library in ${QS_LIB_DIR}.")
endif()

target_link_libraries(opengl_quickstart PUBLIC "${SDL3_LIBRARY}" "${SDL3_IMAGE_LIBRARY}")

if(WIN32)
    target_link_libraries(opengl_quickstart PUBLIC opengl32)
else()
    target_link_libraries(opengl_quickstart PUBLIC GL dl m)
endif()

target_compile_definitions(opengl_quickstart
    PUBLIC
        # DEBUG enables DebugLog.h macros (DBG / MDBG); only on Debug to keep Release binaries quiet.
        $<$<CONFIG:Debug>:DEBUG>
        # absolute path, with trailing slash, to the stock shaders: Shader(QS_SHADER_DIR "default.vert", ...)
        QS_SHADER_DIR="${QS_ROOT}/shaders/"
        # same for the bundled fonts: text.create(QS_FONT_DIR "VT323-Regular.ttf", 32.0f)
        QS_FONT_DIR="${QS_ROOT}/fonts/"
)

# Call on every executable that links opengl_quickstart, so it finds the SDL
# shared libraries at runtime (DLL copy on Windows, rpath on Linux).
function(quickstart_setup_executable target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QS_LIB_DIR}/SDL3.dll"
                "$<TARGET_FILE_DIR:${target}>/SDL3.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QS_LIB_DIR}/SDL3_image.dll"
                "$<TARGET_FILE_DIR:${target}>/SDL3_image.dll"
        )
    else()
        set_target_properties(${target} PROPERTIES BUILD_RPATH "${QS_LIB_DIR}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Example app: only when this repo is the top-level project, not when it's
# pulled in with add_subdirectory.
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(opengl_quickstart_example
        example/main.cpp
        example/gameObject.cpp
        example/Resources.cpp
    )
    target_include_directories(opengl_quickstart_example PRIVATE "${QS_ROOT}/example")
    target_link_libraries(opengl_quickstart_example PRIVATE opengl_quickstart)
    quickstart_setup_executable(opengl_quickstart_example)

    # UI widgets demo (src/ui): event-driven loop, QS_SHOT=<png> saves a frame
    add_executable(opengl_quickstart_widgets example/widgets.cpp)
    target_link_libraries(opengl_quickstart_widgets PRIVATE opengl_quickstart)
    quickstart_setup_executable(opengl_quickstart_widgets)

    # Tests for the parts that need no GL: Utf8, Wrap, TextEdit
    enable_testing()
    add_executable(ui_tests tests/ui_tests.cpp src/ui/TextEdit.cpp src/ui/Wrap.cpp)
    target_include_directories(ui_tests PRIVATE "${QS_ROOT}/src")
    add_test(NAME ui_tests COMMAND ui_tests)

    # Tests for model loading (STL, glTF, GLB), GL-free
    add_executable(model_tests tests/model_tests.cpp src/render/ModelData.cpp src/render/cgltf_impl.c)
    target_include_directories(model_tests PRIVATE "${QS_ROOT}/src" "${QS_INCLUDE_DIR}")
    add_test(NAME model_tests COMMAND model_tests)
endif()
