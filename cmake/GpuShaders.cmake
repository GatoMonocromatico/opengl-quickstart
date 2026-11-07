# -----------------------------------------------------------------------------
# GpuShaders.cmake -- build-time shaders for the gpu:: renderer interface
# -----------------------------------------------------------------------------
# qs_gpu_shaders(<target> <dir> <files...>) compiles GLSL 450 shaders (.vert /
# .frag, SDL_GPU's resource layout, see src/gpu/Gpu.h) into <dir>:
#   <name>.spv   SPIR-V (glslangValidator), for the SDL_GPU backend
#   <name>.gl    GLSL 330 for the GL backend (spirv-cross, with the depth range
#                and render-target flip fixups)
#   <name>.refl  its uniform blocks and samplers with their slots
# and makes <target> depend on them. Load one with
# device.shader("<dir>/<name>", stage). Shaders can #include files next to them
# or in the toolkit's shaders/ (with GL_GOOGLE_include_directive and
# quotes: `#include "ui2d_common.glsl"`).
#
# Needs glslangValidator and spirv-cross on the PATH at build time only
# (Ubuntu: apt install glslang-tools spirv-cross; Windows: the Vulkan SDK).

find_program(QS_GLSLANG glslangValidator)
find_program(QS_SPIRV_CROSS spirv-cross)
set(QS_GPU_SHADER_POST "${CMAKE_CURRENT_LIST_DIR}/GpuShaderPost.cmake")
set(QS_GPU_SHADER_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../shaders")

function(qs_gpu_shaders target dir)
    if(NOT QS_GLSLANG OR NOT QS_SPIRV_CROSS)
        message(FATAL_ERROR "gpu shaders need glslangValidator and spirv-cross "
                            "(Ubuntu: sudo apt install glslang-tools spirv-cross)")
    endif()
    set(outputs "")
    foreach(src IN LISTS ARGN)
        get_filename_component(abs "${src}" ABSOLUTE)
        get_filename_component(name "${src}" NAME)
        get_filename_component(srcdir "${abs}" DIRECTORY)
        set(out "${dir}/${name}")
        add_custom_command(
            OUTPUT "${out}.spv" "${out}.gl" "${out}.refl"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}"
            COMMAND "${QS_GLSLANG}" -V --quiet "-I${srcdir}" "-I${QS_GPU_SHADER_INCLUDE}"
                    --depfile "${out}.d" -o "${out}.spv" "${abs}"
            COMMAND "${QS_SPIRV_CROSS}" "${out}.spv" --version 330 --no-420pack-extension
                    --fixup-clipspace --output "${out}.gl.raw"
            COMMAND "${QS_SPIRV_CROSS}" "${out}.spv" --reflect --output "${out}.json"
            COMMAND "${CMAKE_COMMAND}" -DGL_IN=${out}.gl.raw -DGL_OUT=${out}.gl
                    -DJSON=${out}.json -DREFL=${out}.refl -DSHADER=${name}
                    -P "${QS_GPU_SHADER_POST}"
            DEPENDS "${abs}" "${QS_GPU_SHADER_POST}"
            DEPFILE "${out}.d"
            COMMENT "gpu shader ${name}"
            VERBATIM)
        list(APPEND outputs "${out}.spv" "${out}.gl" "${out}.refl")
    endforeach()
    add_custom_target(${target}_gpu_shaders DEPENDS ${outputs})
    add_dependencies(${target} ${target}_gpu_shaders)
endfunction()
