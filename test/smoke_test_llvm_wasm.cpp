// Smoke test for wasm32 LLVM libraries.
//
// Verifies that LLVM libraries cross-compiled to WebAssembly are functional
// by creating a minimal LLVM module, adding a function, and emitting bitcode.
// Run with: node smoke_test_llvm_wasm.js

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"

#include <cstdio>
#include <string>

static int test_create_module() {
    printf("Test 1: Create LLVM module and function... ");

    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("smoke_test", ctx);

    // Create: i32 @add(i32 %a, i32 %b) { return a + b; }
    auto *i32Ty = llvm::Type::getInt32Ty(ctx);
    auto *funcTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty}, false);
    auto *func = llvm::Function::Create(
        funcTy, llvm::Function::ExternalLinkage, "add", mod.get());

    auto *bb = llvm::BasicBlock::Create(ctx, "entry", func);
    llvm::IRBuilder<> builder(bb);
    auto *sum = builder.CreateAdd(func->getArg(0), func->getArg(1), "sum");
    builder.CreateRet(sum);

    // Verify the module
    std::string err;
    llvm::raw_string_ostream errStream(err);
    if (llvm::verifyModule(*mod, &errStream)) {
        printf("FAIL (verification: %s)\n", err.c_str());
        return 1;
    }

    printf("PASS\n");
    return 0;
}

static int test_emit_bitcode() {
    printf("Test 2: Emit LLVM bitcode to memory... ");

    llvm::LLVMContext ctx;
    auto mod = std::make_unique<llvm::Module>("bitcode_test", ctx);

    // Create a simple function
    auto *voidTy = llvm::Type::getVoidTy(ctx);
    auto *funcTy = llvm::FunctionType::get(voidTy, false);
    auto *func = llvm::Function::Create(
        funcTy, llvm::Function::ExternalLinkage, "noop", mod.get());
    auto *bb = llvm::BasicBlock::Create(ctx, "entry", func);
    llvm::IRBuilder<> builder(bb);
    builder.CreateRetVoid();

    // Write bitcode to a string buffer
    std::string buf;
    llvm::raw_string_ostream os(buf);
    llvm::WriteBitcodeToFile(*mod, os);
    os.flush();

    if (buf.empty()) {
        printf("FAIL (empty bitcode)\n");
        return 1;
    }

    // Check for bitcode magic number: 'BC' (0x42, 0x43)
    if (buf.size() < 4 || buf[0] != 'B' || buf[1] != 'C') {
        printf("FAIL (invalid bitcode magic)\n");
        return 1;
    }

    printf("PASS (%zu bytes)\n", buf.size());
    return 0;
}

static int test_target_registry() {
    printf("Test 3: Check LLVM target registry... ");

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();

    // Check that at least X86 and WebAssembly targets are registered
    std::string error;
    const llvm::Target *x86 = llvm::TargetRegistry::lookupTarget("x86_64", error);
    if (!x86) {
        printf("FAIL (X86 target not found: %s)\n", error.c_str());
        return 1;
    }

    const llvm::Target *wasm = llvm::TargetRegistry::lookupTarget("wasm32", error);
    if (!wasm) {
        printf("FAIL (WebAssembly target not found: %s)\n", error.c_str());
        return 1;
    }

    printf("PASS (found X86, WebAssembly)\n");
    return 0;
}

int main() {
    printf("=== LLVM wasm32 smoke test ===\n");

    int failures = 0;
    failures += test_create_module();
    failures += test_emit_bitcode();
    failures += test_target_registry();

    printf("\n%s (%d/3 tests passed)\n",
           failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           3 - failures);

    return failures;
}
