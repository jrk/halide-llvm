# LLVM-to-WebAssembly Build — WIP Notes

## Build configuration

Cross-compile LLVM 18.1.3 to wasm32 via Emscripten, with all 8 Halide backend
targets enabled (the default set from `toolchains/initial-cache.cmake`):

```
AArch64;ARM;Hexagon;NVPTX;PowerPC;RISCV;WebAssembly;X86
```

Build command (direct ninja, not via pip wheel, to avoid pip timeout issues):

```bash
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_TOOLCHAIN_FILE=../toolchains/wasm32-emscripten.cmake \
  -DLLVM_TABLEGEN=/usr/bin/llvm-tblgen-18 \
  -DCLANG_TABLEGEN=/usr/lib/llvm-18/bin/clang-tblgen
ninja -j$(nproc)
```

No `LLVM_TARGETS_TO_BUILD` override — uses the full default from
`initial-cache.cmake`. Build produces 3783 ninja targets.

## Smoke test results

Test program: `test/smoke_test_llvm_wasm.cpp`
Run via: `node test/build-allarch/smoke_test_llvm_wasm.js`

```
=== LLVM wasm32 smoke test ===

Test 1: Create LLVM module and verify... PASS
Test 2: Emit LLVM bitcode to memory... PASS (1428 bytes)
Test 3: Check LLVM target registry...
  X86         : found
  ARM         : found
  AArch64     : found
  Hexagon     : found
  NVPTX       : found
  PowerPC     : found
  RISCV       : found
  WebAssembly : found
  PASS (all 8 targets found)
Test 4: Emit target assembly for each backend...
  X86         : PASS (220 bytes)
  ARM         : PASS (650 bytes)
  AArch64     : PASS (205 bytes)
  Hexagon     : PASS (220 bytes)
  NVPTX       : PASS (369 bytes)
  PowerPC     : PASS (260 bytes)
  RISCV       : PASS (250 bytes)
  WebAssembly : PASS (370 bytes)
  PASS (all 8 backends emitted assembly)

ALL TESTS PASSED (4/4 tests passed)
```

## Per-backend assembly verification

Each backend compiles `i32 @add(i32 %a, i32 %b) { ret a + b }` to target
assembly. The generated code was manually inspected for correctness:

### X86 (`x86_64-unknown-linux-gnu`, cpu=generic)

```asm
	.text
	.globl	add
	.p2align	4, 0x90
	.type	add,@function
add:
	.cfi_startproc
	leal	(%rdi,%rsi), %eax
	retq
```

Correct: AT&T syntax, SysV ABI (`%rdi`/`%rsi` for args, `%eax` for return).
`leal` computes the add in one instruction. `retq` is 64-bit return.

### ARM (`armv7-none-linux-gnueabihf`, cpu=generic)

```asm
	.text
	.syntax unified
	.fpu	neon
	.code	32
add:
	.fnstart
	add	r0, r0, r1
	bx	lr
	.fnend
```

Correct: AAPCS calling convention (`r0`/`r1` for args, `r0` for return).
`.code 32` = ARM mode (not Thumb). `.fnstart`/`.fnend` are ARM unwind directives.
EABI attributes include hard-float and NEON.

### AArch64 (`aarch64-unknown-linux-gnu`, cpu=generic)

```asm
	.text
	.globl	add
	.type	add,@function
add:
	.cfi_startproc
	add	w0, w0, w1
	ret
```

Correct: Uses `w0`/`w1` (32-bit sub-registers of `x0`/`x1`) which is right for
i32 arguments per AAPCS64. `ret` returns via the link register.

### Hexagon (`hexagon-unknown-linux-musl`, cpu=hexagonv60)

```asm
	.text
	.globl	add
	.type	add,@function
add:
	.cfi_startproc
	{
		r0 = add(r0,r1)
		jumpr r31
	}
```

Correct: Hexagon VLIW packet syntax with `{` `}`. `r0`/`r1` for args,
`r0` for return. `jumpr r31` returns (r31 = link register). Dual-issued
add + return in a single packet.

### NVPTX (`nvptx64-nvidia-cuda`, cpu=sm_50)

```asm
.version 4.0
.target sm_50
.address_size 64

.visible .func  (.param .b32 func_retval0) add(
	.param .b32 add_param_0,
	.param .b32 add_param_1
)
{
	.reg .b32 	%r<4>;

	ld.param.u32 	%r1, [add_param_0];
	ld.param.u32 	%r2, [add_param_1];
	add.s32 	%r3, %r1, %r2;
	st.param.b32 	[func_retval0+0], %r3;
	ret;
}
```

Correct PTX assembly: `.version 4.0` / `.target sm_50` / `.address_size 64`.
Parameters passed via `.param` space, loaded with `ld.param`, added with
`add.s32`, result stored with `st.param.b32`.

### PowerPC (`powerpc64le-unknown-linux-gnu`, cpu=generic)

```asm
	.text
	.abiversion 2
	.globl	add
	.type	add,@function
add:
	.cfi_startproc
	add 3, 3, 4
	blr
	.long	0
	.quad	0
```

Correct: PPC64 ELFv2 ABI (`.abiversion 2`). Args in `r3`/`r4`, result in `r3`.
`blr` = branch to link register (return). Trailing `.long 0` / `.quad 0` is the
standard PPC64 function padding.

### RISCV (`riscv64-unknown-linux-gnu`, cpu=generic)

```asm
	.text
	.attribute	4, 16
	.attribute	5, "rv64i2p1"
	.globl	add
	.type	add,@function
add:
	.cfi_startproc
	addw	a0, a0, a1
	ret
```

Correct: `addw` (word add) is right for i32 on rv64 — sign-extends the 32-bit
result. `a0`/`a1` are the argument registers per RISC-V calling convention.
`.attribute 5, "rv64i2p1"` confirms RV64 base integer ISA v2.1.

### WebAssembly (`wasm32-unknown-unknown`, cpu=generic)

```asm
	.text
	.functype	add (i32, i32) -> (i32)
	.globl	add
	.type	add,@function
add:
	.functype	add (i32, i32) -> (i32)
	local.get	0
	local.get	1
	i32.add
	end_function
```

Correct wasm text assembly: `.functype` declares the signature, `local.get`
pushes arguments onto the stack, `i32.add` pops two i32 and pushes the sum,
`end_function` terminates the function body. Target features include
`mutable-globals` and `sign-ext`.

## Known issues / notes

- The `pip wheel` build can time out before completing the full 3783-target
  build. Running cmake + ninja directly avoids this. A future improvement would
  be to increase pip's build timeout or use a two-stage build.
- NVPTX has no AsmParser component (there is no `NVPTXAsmParser` library) — this
  is expected since PTX is an output-only format for LLVM.
