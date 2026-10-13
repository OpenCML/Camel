#!/usr/bin/env node
// Precondition of the tests that assert that std::jit compiled a function to native code
// (`skip_unless = ["node", "test/tools/have-native-jit.mjs"]`).
//
// The JIT has one code generator, for x86-64 (cmake/Options.cmake: ENABLE_FASTVM_JIT is on only
// when CMAKE_SYSTEM_PROCESSOR is x86-64). On another processor (arm64 macOS) std::jit is the
// bytecode interpreter of std::fvm: programs run and give the same results, so every test of
// results under std::jit still runs there; what cannot hold is a line of the log that says a
// graph was compiled. Exit 0 where the JIT generates code, 1 with the reason elsewhere.

if (process.arch === 'x64') {
    process.exit(0)
}
console.log(`std::jit has no native code generator for ${process.arch} (x86-64 only): it runs the bytecode interpreter`)
process.exit(1)
