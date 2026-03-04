# Smoke test for wasm32 LLVM libraries.
#
# Usage (after building the LLVM wasm wheel):
#   emcmake cmake -S test -B test/build \
#       -DLLVM_DIR=<llvm-install>/lib/cmake/llvm
#   cmake --build test/build
#   node test/build/smoke_test_llvm_wasm.js
cmake_minimum_required(VERSION 3.21)
project(llvm-wasm-smoke-test LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake from: ${LLVM_DIR}")

add_executable(smoke_test_llvm_wasm smoke_test_llvm_wasm.cpp)
target_include_directories(smoke_test_llvm_wasm PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(smoke_test_llvm_wasm PRIVATE ${LLVM_DEFINITIONS})

# Map LLVM components to libraries.
# Each backend needs its CodeGen + AsmParser + Info components.
llvm_map_components_to_libnames(llvm_libs
    Core
    IRReader
    BitWriter
    Support

    # X86
    X86Info
    X86CodeGen
    X86AsmParser

    # ARM
    ARMInfo
    ARMCodeGen
    ARMAsmParser

    # AArch64
    AArch64Info
    AArch64CodeGen
    AArch64AsmParser

    # Hexagon
    HexagonInfo
    HexagonCodeGen
    HexagonAsmParser

    # NVPTX
    NVPTXInfo
    NVPTXCodeGen

    # PowerPC
    PowerPCInfo
    PowerPCCodeGen
    PowerPCAsmParser

    # RISCV
    RISCVInfo
    RISCVCodeGen
    RISCVAsmParser

    # WebAssembly
    WebAssemblyInfo
    WebAssemblyCodeGen
    WebAssemblyAsmParser
)
target_link_libraries(smoke_test_llvm_wasm PRIVATE ${llvm_libs})

# Allow memory growth for large LLVM operations
target_link_options(smoke_test_llvm_wasm PRIVATE
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -sSTACK_SIZE=4194304
)
