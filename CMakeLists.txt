cmake_minimum_required(VERSION 3.20)

project(fluxograme LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(LOCAL_LIBRARIES_DIR "${PROJECT_ROOT}/Libraries")
set(LOCAL_INCLUDE_DIR "${LOCAL_LIBRARIES_DIR}/include")

if(WIN32)
    set(LOCAL_LIB_DIR "${LOCAL_LIBRARIES_DIR}/lib_win/lib")
elseif(UNIX)
    set(LOCAL_LIB_DIR "${LOCAL_LIBRARIES_DIR}/lib_linux/lib")
else()
    message(FATAL_ERROR "Unsupported platform for local library layout.")
endif()

if(NOT EXISTS "${LOCAL_INCLUDE_DIR}" OR NOT EXISTS "${LOCAL_LIB_DIR}")
    message(FATAL_ERROR "Expected local SDK folders at Libraries/include and platform-specific lib directory.")
endif()

add_executable(fluxograme
    main.cpp
    camera.cpp
    EBO.cpp
    gameObject.cpp
    Mesh.cpp
    point.cpp
    Resources.cpp
    shaderClass.cpp
    texture.cpp
    VAO.cpp
    VBO.cpp
    glad.c
)

target_include_directories(fluxograme
    PRIVATE
        "${PROJECT_ROOT}"
        "${LOCAL_INCLUDE_DIR}"
)

if(WIN32)
    set(SDL3_LIBRARY "${LOCAL_LIB_DIR}/SDL3.lib")
    set(SDL3_IMAGE_LIBRARY "${LOCAL_LIB_DIR}/SDL3_image.lib")
else()
    find_library(SDL3_LIBRARY NAMES SDL3 PATHS "${LOCAL_LIB_DIR}" NO_DEFAULT_PATH)
    find_library(SDL3_IMAGE_LIBRARY NAMES SDL3_image PATHS "${LOCAL_LIB_DIR}" NO_DEFAULT_PATH)
endif()

if(NOT SDL3_LIBRARY)
    message(FATAL_ERROR "Could not find SDL3 library in ${LOCAL_LIB_DIR}.")
endif()
if(NOT SDL3_IMAGE_LIBRARY)
    message(FATAL_ERROR "Could not find SDL3_image library in ${LOCAL_LIB_DIR}.")
endif()

target_link_libraries(fluxograme PRIVATE "${SDL3_LIBRARY}" "${SDL3_IMAGE_LIBRARY}")

if(WIN32)
    target_link_libraries(fluxograme PRIVATE opengl32)
    add_custom_command(TARGET fluxograme POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LOCAL_LIB_DIR}/SDL3.dll"
            "$<TARGET_FILE_DIR:fluxograme>/SDL3.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LOCAL_LIB_DIR}/SDL3_image.dll"
            "$<TARGET_FILE_DIR:fluxograme>/SDL3_image.dll"
    )
else()
    target_link_directories(fluxograme PRIVATE "${LOCAL_LIB_DIR}")
    target_link_libraries(fluxograme PRIVATE GL dl m)

    # Let the executable resolve SDL shared objects from the local project libs.
    set_target_properties(fluxograme PROPERTIES
        BUILD_RPATH "${LOCAL_LIB_DIR}"
        INSTALL_RPATH "$ORIGIN/../Libraries/lib_linux/lib"
    )
endif()
