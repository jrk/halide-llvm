// Smoke test for wasm32 LLVM libraries.
//
// Verifies that LLVM libraries cross-compiled to WebAssembly are functional:
//   1. Create an LLVM IR module and verify it
//   2. Emit LLVM bitcode to memory
//   3. Check all expected targets are registered
//   4. Emit actual target assembly for each backend (X86, ARM, AArch64,
//      Hexagon, NVPTX, WebAssembly)
//
// Run with: node smoke_test_llvm_wasm.js

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/TargetRegistry.h"

#include "llvm/ADT/SmallVector.h"

#include <cstdio>
#include <memory>
#include <string>

/// Create a simple module with: i32 @add(i32 %a, i32 %b) { ret a + b }
static std::unique_ptr<llvm::Module> create_test_module(llvm::LLVMContext &ctx,
                                                        const char *triple) {
    auto mod = std::make_unique<llvm::Module>("test", ctx);
    mod->setTargetTriple(triple);

    auto *i32Ty = llvm::Type::getInt32Ty(ctx);
    auto *funcTy = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty}, false);
    auto *func = llvm::Function::Create(
        funcTy, llvm::Function::ExternalLinkage, "add", mod.get());

    auto *bb = llvm::BasicBlock::Create(ctx, "entry", func);
    llvm::IRBuilder<> builder(bb);
    auto *sum = builder.CreateAdd(func->getArg(0), func->getArg(1), "sum");
    builder.CreateRet(sum);

    return mod;
}

// ---- Test 1: Create and verify a module ------------------------------------

static int test_create_module() {
    printf("Test 1: Create LLVM module and verify... ");

    llvm::LLVMContext ctx;
    auto mod = create_test_module(ctx, "x86_64-unknown-linux-gnu");

    std::string err;
    llvm::raw_string_ostream errStream(err);
    if (llvm::verifyModule(*mod, &errStream)) {
        printf("FAIL (verification: %s)\n", err.c_str());
        return 1;
    }

    printf("PASS\n");
    return 0;
}

// ---- Test 2: Emit bitcode --------------------------------------------------

static int test_emit_bitcode() {
    printf("Test 2: Emit LLVM bitcode to memory... ");

    llvm::LLVMContext ctx;
    auto mod = create_test_module(ctx, "x86_64-unknown-linux-gnu");

    std::string buf;
    llvm::raw_string_ostream os(buf);
    llvm::WriteBitcodeToFile(*mod, os);
    os.flush();

    if (buf.size() < 4 || buf[0] != 'B' || buf[1] != 'C') {
        printf("FAIL (invalid bitcode, %zu bytes)\n", buf.size());
        return 1;
    }

    printf("PASS (%zu bytes)\n", buf.size());
    return 0;
}

// ---- Test 3: Check target registry -----------------------------------------

struct TargetInfo {
    const char *arch;    // lookupTarget key
    const char *name;    // human-readable name
};

static const TargetInfo all_targets[] = {
    {"x86_64",      "X86"},
    {"arm",         "ARM"},
    {"aarch64",     "AArch64"},
    {"hexagon",     "Hexagon"},
    {"nvptx64",     "NVPTX"},
    {"ppc64le",     "PowerPC"},
    {"riscv64",     "RISCV"},
    {"wasm32",      "WebAssembly"},
};

static int test_target_registry() {
    printf("Test 3: Check LLVM target registry...\n");

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();

    int failures = 0;
    for (const auto &t : all_targets) {
        std::string error;
        const llvm::Target *tgt =
            llvm::TargetRegistry::lookupTarget(t.arch, error);
        if (!tgt) {
            printf("  %-12s: FAIL (not found: %s)\n", t.name, error.c_str());
            ++failures;
        } else {
            printf("  %-12s: found\n", t.name);
        }
    }

    if (failures) {
        printf("  FAIL (%d target(s) missing)\n", failures);
        return failures;
    }
    printf("  PASS (all %zu targets found)\n",
           sizeof(all_targets) / sizeof(all_targets[0]));
    return 0;
}

// ---- Test 4: Emit target assembly for each backend -------------------------

struct CodegenTarget {
    const char *triple;
    const char *cpu;
    const char *name;
};

static const CodegenTarget codegen_targets[] = {
    {"x86_64-unknown-linux-gnu",      "generic",    "X86"},
    {"armv7-none-linux-gnueabihf",    "generic",    "ARM"},
    {"aarch64-unknown-linux-gnu",     "generic",    "AArch64"},
    {"hexagon-unknown-linux-musl",    "hexagonv60", "Hexagon"},
    {"nvptx64-nvidia-cuda",           "sm_50",      "NVPTX"},
    {"powerpc64le-unknown-linux-gnu", "generic",    "PowerPC"},
    {"riscv64-unknown-linux-gnu",     "generic",    "RISCV"},
    {"wasm32-unknown-unknown",        "generic",    "WebAssembly"},
};

static int test_emit_target_code() {
    printf("Test 4: Emit target assembly for each backend...\n");

    // Already initialized in test 3, but safe to call again
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();

    int failures = 0;

    for (const auto &ct : codegen_targets) {
        std::string error;
        const llvm::Target *target =
            llvm::TargetRegistry::lookupTarget(ct.triple, error);
        if (!target) {
            printf("  %-12s: FAIL (target lookup: %s)\n", ct.name,
                   error.c_str());
            ++failures;
            continue;
        }

        llvm::TargetOptions opt;
        std::unique_ptr<llvm::TargetMachine> tm(target->createTargetMachine(
            ct.triple, ct.cpu, "", opt, llvm::Reloc::PIC_));
        if (!tm) {
            printf("  %-12s: FAIL (could not create TargetMachine)\n",
                   ct.name);
            ++failures;
            continue;
        }

        llvm::LLVMContext ctx;
        auto mod = create_test_module(ctx, ct.triple);
        mod->setDataLayout(tm->createDataLayout());

        // Emit assembly to a buffer (addPassesToEmitFile needs raw_pwrite_stream)
        llvm::SmallVector<char, 4096> asmVec;
        llvm::raw_svector_ostream asmStream(asmVec);
        llvm::legacy::PassManager pm;
        if (tm->addPassesToEmitFile(pm, asmStream, nullptr,
                                    llvm::CodeGenFileType::AssemblyFile)) {
            printf("  %-12s: FAIL (addPassesToEmitFile rejected)\n", ct.name);
            ++failures;
            continue;
        }
        pm.run(*mod);

        std::string asmBuf(asmVec.begin(), asmVec.end());

        if (asmBuf.empty()) {
            printf("  %-12s: FAIL (empty assembly output)\n", ct.name);
            ++failures;
            continue;
        }

        printf("  %-12s: PASS (%zu bytes)\n", ct.name, asmBuf.size());

        // Dump the full assembly so we can manually verify it looks correct
        printf("  --- %s assembly (%s, cpu=%s) ---\n",
               ct.name, ct.triple, ct.cpu);
        printf("%s", asmBuf.c_str());
        printf("  --- end %s ---\n\n", ct.name);
    }

    if (failures) {
        printf("  FAIL (%d backend(s) failed)\n", failures);
        return failures;
    }
    printf("  PASS (all %zu backends emitted assembly)\n",
           sizeof(codegen_targets) / sizeof(codegen_targets[0]));
    return 0;
}

// ---- Main ------------------------------------------------------------------

int main() {
    printf("=== LLVM wasm32 smoke test ===\n\n");

    int total = 4;
    int failures = 0;
    failures += test_create_module();
    failures += test_emit_bitcode();
    failures += test_target_registry();
    failures += test_emit_target_code();

    printf("\n%s (%d/%d tests passed)\n",
           failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           total - (failures > 0 ? 1 : 0), total);

    return failures ? 1 : 0;
}
