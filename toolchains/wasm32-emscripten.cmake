# LLVM toolchain for wasm32 via Emscripten (cross-compiled from native host).
#
# This cross-compiles LLVM itself to WebAssembly static libraries, producing
# .a files that can be linked into a wasm module (e.g., libHalide compiled
# to wasm). It does NOT build LLVM tools or executables.
#
# Prerequisites:
#   - Emscripten SDK activated (EMSDK env var set)
#   - Native LLVM providing llvm-tblgen and clang-tblgen (passed via
#     LLVM_TABLEGEN and CLANG_TABLEGEN cmake defines)
#
# Usage:
#   EMSDK=/path/to/emsdk HALIDE_LLVM_REF=... \
#   pip wheel . -w dist/ -v \
#     --config-settings=cmake.define.CMAKE_TOOLCHAIN_FILE=toolchains/wasm32-emscripten.cmake \
#     --config-settings=cmake.define.LLVM_TABLEGEN=/usr/bin/llvm-tblgen-20 \
#     --config-settings=cmake.define.CLANG_TABLEGEN=/usr/bin/clang-tblgen-20

# --- Cross-compilation: include Emscripten toolchain ---
if (NOT DEFINED ENV{EMSDK})
    message(FATAL_ERROR
        "EMSDK environment variable must be set.\n"
        "Activate the Emscripten SDK first: source emsdk_env.sh")
endif ()

include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

# --- Wasm-specific overrides (set before initial-cache.cmake) ---
# These use CACHE so they take priority over initial-cache.cmake's CACHE sets
# (CMake CACHE only writes on first set; first writer wins).

# No runtimes — we only need LLVM/Clang/LLD static libraries
set(LLVM_ENABLE_RUNTIMES "" CACHE STRING "")

# Disable assertions (significant size impact in wasm)
set(LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")

# No tools or executables (wasm can't run native binaries)
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "")
set(CLANG_BUILD_TOOLS OFF CACHE BOOL "")

# Wasm-incompatible features
set(LLVM_ENABLE_THREADS OFF CACHE BOOL "")
set(LLVM_ENABLE_PIC OFF CACHE BOOL "")
set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
set(LLVM_ENABLE_CRASH_OVERRIDES OFF CACHE BOOL "")
set(LLVM_ENABLE_UNWIND_TABLES OFF CACHE BOOL "")
set(LLVM_ENABLE_LIBPFM OFF CACHE BOOL "")

# Force static libs only
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

# Use MinSizeRel for smaller wasm output
set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/initial-cache.cmake")
