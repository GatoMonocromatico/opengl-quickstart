# -----------------------------------------------------------------------------
# TextLibraries.cmake -- vendored FreeType + HarfBuzz (+ libpng, zlib) as static libs
# -----------------------------------------------------------------------------
# Sources live in Libraries/src (third-party, unmodified). Everything builds from
# source with the project, so Unicode text keeps the "works right after clone, no
# system installs" promise on Linux and Windows. libpng (and zlib under it) is only
# there for FreeType's PNG strikes: colour emoji fonts (Noto Color Emoji) store
# glyphs as PNGs. FreeType's config (cmake/freetype-config) enables PNG and lists
# only the modules built here.

set(QS_TEXT_SRC "${QS_LIBRARIES_DIR}/src")

# zlib --------------------------------------------------------------------------
file(GLOB QS_ZLIB_SOURCES "${QS_TEXT_SRC}/zlib/*.c")
add_library(qs_zlib STATIC ${QS_ZLIB_SOURCES})
target_include_directories(qs_zlib PUBLIC "${QS_TEXT_SRC}/zlib")
set_target_properties(qs_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
if(MSVC)
    target_compile_definitions(qs_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
else()
    target_compile_options(qs_zlib PRIVATE -w)
    target_compile_definitions(qs_zlib PRIVATE HAVE_UNISTD_H)
endif()

# libpng ------------------------------------------------------------------------
file(GLOB QS_PNG_SOURCES "${QS_TEXT_SRC}/libpng/*.c")
add_library(qs_png STATIC ${QS_PNG_SOURCES})
target_include_directories(qs_png PUBLIC "${QS_TEXT_SRC}/libpng")
target_link_libraries(qs_png PUBLIC qs_zlib)
# no SIMD filter paths: their sources aren't vendored
target_compile_definitions(qs_png PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0 PNG_POWERPC_VSX_OPT=0
    PNG_MIPS_MSA_OPT=0 PNG_MIPS_MMI_OPT=0 PNG_LOONGARCH_LSX_OPT=0 PNG_RISCV_RVV_OPT=0)
set_target_properties(qs_png PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
if(NOT MSVC)
    target_compile_options(qs_png PRIVATE -w)
endif()

# FreeType ----------------------------------------------------------------------
set(QS_FT "${QS_TEXT_SRC}/freetype")
add_library(qs_freetype STATIC
    "${QS_FT}/src/base/ftsystem.c"
    "${QS_FT}/src/base/ftinit.c"
    "${QS_FT}/src/base/ftdebug.c"
    "${QS_FT}/src/base/ftbase.c"
    "${QS_FT}/src/base/ftbbox.c"
    "${QS_FT}/src/base/ftbitmap.c"
    "${QS_FT}/src/base/ftglyph.c"
    "${QS_FT}/src/base/ftmm.c"
    "${QS_FT}/src/base/ftsynth.c"
    "${QS_FT}/src/autofit/autofit.c"
    "${QS_FT}/src/truetype/truetype.c"
    "${QS_FT}/src/cff/cff.c"
    "${QS_FT}/src/psaux/psaux.c"
    "${QS_FT}/src/psnames/psnames.c"
    "${QS_FT}/src/pshinter/pshinter.c"
    "${QS_FT}/src/sfnt/sfnt.c"
    "${QS_FT}/src/smooth/smooth.c"
)
# our config dir first, so its ftoption.h / ftmodule.h win over the vendored ones
target_include_directories(qs_freetype BEFORE PUBLIC "${QS_ROOT}/cmake/freetype-config")
target_include_directories(qs_freetype PUBLIC "${QS_FT}/include")
target_compile_definitions(qs_freetype PRIVATE FT2_BUILD_LIBRARY)
target_link_libraries(qs_freetype PRIVATE qs_png)
set_target_properties(qs_freetype PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
if(MSVC)
    target_compile_definitions(qs_freetype PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(qs_freetype PRIVATE -w)
endif()

# HarfBuzz (single-file amalgamation) -------------------------------------------
add_library(qs_harfbuzz STATIC "${QS_TEXT_SRC}/harfbuzz/src/harfbuzz.cc")
target_include_directories(qs_harfbuzz PUBLIC "${QS_TEXT_SRC}/harfbuzz/src")
target_compile_definitions(qs_harfbuzz PRIVATE HAVE_FREETYPE=1)
target_link_libraries(qs_harfbuzz PUBLIC qs_freetype)
set_target_properties(qs_harfbuzz PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
if(NOT MSVC)
    target_compile_options(qs_harfbuzz PRIVATE -w -fno-exceptions -fno-rtti)
endif()
