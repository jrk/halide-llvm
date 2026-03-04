# Plan: Extend halide-llvm to build LLVM for WebAssembly (wasm32)

## Goal

Add a new build variant that cross-compiles LLVM static libraries to wasm32
using Emscripten, packaged as a Python wheel (consistent with existing native
builds). This replaces the inline LLVM build in jrk/Halide's wasm-build branch.

## Background

The existing repo builds LLVM as **native toolchains** (executables + libs)
for 8 host platforms, distributed as Python wheels via a private PyPI. The
Halide wasm-build branch needs LLVM **compiled to wasm32** — the same LLVM
libraries, but as `.a` static libraries targeting WebAssembly, so libHalide
can be linked into a wasm module that runs the Halide compiler under Node.js
or in a browser.

Key difference: this is a **cross-compilation of LLVM itself** via Emscripten,
not building LLVM to emit WebAssembly code (that's already done — WebAssembly
is one of the LLVM_TARGETS_TO_BUILD in the native builds).

## Design Decisions

1. **Distribution**: Python wheel (user confirmed). Platform tag will be
   `emscripten_<major>_<minor>_<patch>_wasm32` (scikit-build-core supports
   this via `wheel.platname` override).

2. **LLVM targets**: All backends (user confirmed). Matches native builds:
   `AArch64;ARM;Hexagon;NVPTX;PowerPC;RISCV;WebAssembly;X86`.

3. **RTTI/EH**: Keep ON (matching `initial-cache.cmake`). Halide uses both.
   The proven wasm test build used defaults (OFF) but a proper build for
   Halide should enable them.

4. **Assertions**: Keep OFF for wasm (unlike native builds which have ON).
   Assertions add significant size to the wasm binary.

5. **Native tablegen tools**: Install system LLVM packages in CI (`llvm-20`).
   This is the simplest approach and matches what the Halide wasm-build uses.

6. **CMAKE_TOOLCHAIN_FILE handling**: The wasm toolchain file will `include()`
   both the Emscripten toolchain file AND `initial-cache.cmake`, chaining them.
   This follows the existing pattern where platform toolchains include
   `initial-cache.cmake`.

## Changes

### 1. New file: `toolchains/wasm32-emscripten.cmake`

```cmake
# LLVM toolchain for wasm32 via Emscripten (cross-compiled from native host).
#
# Prerequisites:
#   - Emscripten SDK activated (EMSDK env var set)
#   - Native LLVM providing llvm-tblgen and clang-tblgen
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
```

### 2. Modify `CMakeLists.txt`

Small changes to handle the wasm cross-compilation case:

a. The post-install symlink conversion code should be skipped when
   cross-compiling (no `bin/` executables exist). Guard it:

```cmake
# Only convert symlinks for native builds (wasm builds have no executables)
if (NOT CMAKE_CROSSCOMPILING)
    install(CODE "set(CONVERT_SYMLINK_SCRIPT ...)")
    install(CODE [[...]])
endif ()
```

b. Override `cmake.build-type` from `pyproject.toml`: The wasm toolchain sets
   `CMAKE_BUILD_TYPE=MinSizeRel`, which conflicts with the `Release` default
   in `pyproject.toml`. This works because the toolchain's CACHE set takes
   priority over scikit-build-core's setting (scikit-build-core also uses
   cache sets, and first-write wins for CMake cache).

### 3. Modify `pyproject.toml`

No changes needed for the base config. The `cmake.build-type = "Release"`
default is overridden by the toolchain's `CMAKE_BUILD_TYPE` cache entry.
The `wheel.py-api = "py3"` and `wheel.install-dir` settings work fine for
wasm (static libs still install to `halide_llvm/data/`).

### 4. New CI workflow: `.github/workflows/build-wasm-wheel.yml`

Separate workflow (not added to the existing matrix) because the wasm build
has fundamentally different prerequisites (Emscripten SDK, native LLVM tools)
and a longer build time (~40-60 min).

```yaml
name: Build wasm wheel

on:
  workflow_dispatch:
    inputs:
      llvm_ref:
        description: "LLVM git ref"
        required: true
      emsdk_version:
        description: "Emscripten SDK version"
        required: false
        default: "4.0.3"

jobs:
  build:
    name: Build / wasm32-emscripten
    runs-on: ubuntu-latest
    env:
      HALIDE_LLVM_REF: ${{ inputs.llvm_ref }}
      GITHUB_TOKEN: ${{ github.token }}
      CMAKE_GENERATOR: Ninja
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.12" }

      # Install native LLVM tools for tablegen
      - name: Install native LLVM 20
        run: |
          wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
          echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-20 main" | \
            sudo tee /etc/apt/sources.list.d/llvm.list
          sudo apt-get update
          sudo apt-get install -y llvm-20-tools clang-20

      # Install Emscripten SDK
      - name: Set up Emscripten
        uses: mymindstorm/setup-emsdk@v14
        with:
          version: ${{ inputs.emsdk_version || '4.0.3' }}

      # Build wheel
      - name: Build wheel
        run: >
          pip wheel . -w dist/ -v
          --config-settings=cmake.define.CMAKE_TOOLCHAIN_FILE=toolchains/wasm32-emscripten.cmake
          --config-settings=cmake.define.LLVM_TABLEGEN=/usr/lib/llvm-20/bin/llvm-tblgen
          --config-settings=cmake.define.CLANG_TABLEGEN=/usr/lib/llvm-20/bin/clang-tblgen

      # Override wheel platform tag (scikit-build-core may not detect Emscripten)
      - name: Retag wheel
        run: |
          pip install wheel
          EMSDK_VER="${{ inputs.emsdk_version || '4.0.3' }}"
          EMSDK_TAG="emscripten_${EMSDK_VER//./_}_wasm32"
          wheel tags --platform-tag "$EMSDK_TAG" --remove dist/*.whl

      - name: Upload wheel
        uses: actions/upload-artifact@v4
        with:
          name: wheel-wasm32-emscripten
          path: dist/*.whl

  upload:
    name: Upload to PyPI
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
        with:
          name: wheel-wasm32-emscripten
          path: dist/
      - name: Upload to PyPI
        env:
          TWINE_USERNAME: upload
          TWINE_PASSWORD: ${{ secrets.PYPI_UPLOAD_PASSWORD }}
          TWINE_REPOSITORY_URL: https://pypi.halide-lang.org/
        run: |
          pip install "twine>=6" "packaging>=24.2"
          twine upload dist/*.whl
```

### 5. Update `local-build.sh`

Add `wasm32-emscripten` as a supported platform. Since it requires Emscripten
to be activated in the current shell, it's a direct local build (no Docker):

```bash
run_wasm_build() {
    local dist_dir="dist/wasm32-emscripten"
    mkdir -p "$dist_dir"

    # Validate Emscripten
    command -v emcc &>/dev/null || {
        echo "error: emcc not found. Activate Emscripten SDK first: source emsdk_env.sh" >&2
        exit 1
    }

    # Find native tablegen tools
    local tblgen_llvm="" tblgen_clang=""
    for suffix in -20 -21 ""; do
        if command -v "llvm-tblgen${suffix}" &>/dev/null; then
            tblgen_llvm="$(command -v "llvm-tblgen${suffix}")"
            break
        fi
    done
    for suffix in -20 -21 ""; do
        if command -v "clang-tblgen${suffix}" &>/dev/null; then
            tblgen_clang="$(command -v "clang-tblgen${suffix}")"
            break
        fi
    done
    [[ -n "$tblgen_llvm" ]] || { echo "error: llvm-tblgen not found" >&2; exit 1; }
    [[ -n "$tblgen_clang" ]] || { echo "error: clang-tblgen not found" >&2; exit 1; }

    echo "Building halide-llvm (wasm32-emscripten)"
    echo "  HALIDE_LLVM_REF: $HALIDE_LLVM_REF"
    echo "  llvm-tblgen: $tblgen_llvm"
    echo "  clang-tblgen: $tblgen_clang"
    echo "  Output: $dist_dir/"

    local config_settings=(
        "--config-settings=cmake.define.CMAKE_TOOLCHAIN_FILE=toolchains/wasm32-emscripten.cmake"
        "--config-settings=cmake.define.LLVM_TABLEGEN=$tblgen_llvm"
        "--config-settings=cmake.define.CLANG_TABLEGEN=$tblgen_clang"
    )

    pip wheel . -w "$dist_dir/" -v "${config_settings[@]}"
}
```

Add to the platform dispatch at the bottom of the script.

### 6. No changes needed to `_version_provider.py` or `src/halide_llvm/__init__.py`

The version provider downloads LLVM source regardless of target platform.
The Python helper module provides path accessors that work for any install layout.

## How the Halide wasm-build will consume this

Instead of running `wasm/build.sh` Stage 1 (which builds LLVM inline), the
Halide wasm build will:

```bash
# Install the wasm LLVM wheel
pip install halide-llvm --platform emscripten_4_0_3_wasm32 --no-deps \
    --index-url https://pypi.halide-lang.org/simple/

# Get the install prefix
LLVM_WASM_PREFIX=$(python -c "import halide_llvm; print(halide_llvm.get_root_dir())")

# Use it in the Halide build
emcmake cmake -S . -B build-wasm \
    -DLLVM_DIR="${LLVM_WASM_PREFIX}/lib/cmake/llvm" \
    -DClang_DIR="${LLVM_WASM_PREFIX}/lib/cmake/clang" \
    -DLLD_DIR="${LLVM_WASM_PREFIX}/lib/cmake/lld" \
    ...
```

## Risk/Open Questions

1. **Emscripten toolchain chaining**: Including `Emscripten.cmake` from within
   our toolchain file is a well-established pattern but might have edge cases.
   If it doesn't work, fallback is to use `emcmake` wrapper and pass our LLVM
   settings via individual `--config-settings=cmake.define.X=Y`.

2. **scikit-build-core + cross-compilation**: scikit-build-core may not fully
   support cross-compilation scenarios. If `pip wheel` fails, fallback is a
   direct CMake build + manual wheel packaging (using `wheel pack`).

3. **Wheel size**: With all LLVM backends, the wasm static libs will be large
   (500MB+). This might be too big for a wheel. If so, we can strip debug info
   or reduce targets.

4. **RTTI/EH in wasm**: The native builds have RTTI=ON and EH=ON. Emscripten
   supports C++ exceptions via `-fexceptions` but it adds overhead. If LLVM's
   EH support causes issues in the wasm build, we may need to disable it
   (like the arm-32-linux toolchain does).

5. **Emscripten version pinning**: The wheel platform tag encodes the Emscripten
   version. Consumers must use a compatible Emscripten version. The CI workflow
   takes the version as an input parameter.
