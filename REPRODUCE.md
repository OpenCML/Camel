# From the source archive to a running program

This file reproduces, command by command, what a reader of the archive has to type: unpack it, build
it, run one program, run the smallest test case, and build the trimmed subset. Every claim below was
run on the tree this archive was cut from, on Linux/x86-64 with a 32-core EPYC and clang 23.1.2;
the numbers are that machine's, the commands are not.

## 0. What is in the archive

The archive is `git archive` of one commit:

```bash
git archive --format=tar.gz --prefix=camel/ -o camel-source.tar.gz <commit>
```

Because it is `git archive`, it holds exactly the tracked files of that commit: no build output, no
`out/`, no cache, no `.git/`, and no state file of any tool. The tree checks that for itself, and the
check needs neither git nor a network:

```bash
tar xzf camel-source.tar.gz && cd camel
node scripts/source-package.mjs
# source-package: this tree carries no process trace; it is only source
```

If the last line instead lists paths (each with its class and its size), the tree is not only
source; `node scripts/source-package.mjs --table` groups them by class, and `--clean` removes them.

## 1. Toolchain

```bash
cmake --version      # >= 3.25
ninja --version
clang++ --version    # >= 19: the sources are C++23
conan --version      # 2.x
node --version       # >= 18
```

Conan needs a profile it can use for this project. The three settings that matter:

```bash
conan profile detect --force
conan profile show
#   compiler=clang
#   compiler.cppstd=23
#   [conf] tools.cmake.cmaketoolchain:generator=Ninja Multi-Config
```

Edit `~/.conan2/profiles/default` if the detected profile says otherwise (a gcc default, or
`compiler.cppstd=17`).

## 2. Build

```bash
node scripts/build.js
```

Expect the four steps in this order: `conan install . --output-folder=build --build=missing` (needs
network the first time), `node scripts/diagnostics-catalog.mjs --generate`, `cmake .. -G "Ninja
Multi-Config" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake`, then `cmake --build . --config
Release`. It ends with:

```
[DONE] Artifacts collected to out/latest/
[DONE] Release build completed
```

Build time on the reference machine: 56 s (583 compile/link steps for the trimmed subset in §4; the
full one is a few dozen steps more). The install is `out/latest/{bin,libs,stdlib,include}`.

## 3. Run

```bash
node scripts/source-package.mjs --root . >/dev/null   # optional: 0 traces, exit 0

out/latest/bin/camel hello.cml
# Hello, <your user name>!

out/latest/bin/camel hello.cml std::gir | head -5
# digraph GraphIR {
#     subgraph cluster_F01 {
#         label="main";
#         O01 [label="EXIT", type="EXIT"];
#         N00 [label="process:user_name", type="OPER", origin=..., span="3:12-3:21", ...];

out/latest/bin/camel test/cases/smoke/core/fib10_pipeline.cml std::fvm
# Start computing fibonacci(10)...
# Fibonacci(10) = 55 (computed in 0.0000 seconds)

out/latest/bin/camel examples/algorithms/merge_sort.cml
# input:  [38, 27, 43, 3, 9, 82, 10, 27, -5, 0]
# sorted: [-5, 0, 3, 9, 10, 27, 27, 38, 43, 82]
# same as array.sort: true
```

`camel` locates `libs/` and `stdlib/` from its own path, so no environment variable is needed. To
run the whole fast test layer (730 cases, minutes):

```bash
node scripts/test.js --tier fast --jobs 8
```

The 13 cases under `test/plans/gpu` that assert bit-exact GPU transfer counts are recorded as known
reds on a machine without a device; that plan set is report-only and is not one of the 730.

## 4. The trimmed subset

The archive can be built as a subset: `CAMEL_PACKAGE_MINIMAL=ON` builds the full module list minus the
modules `CAMEL_MINIMAL_DROPS` names (top-level `CMakeLists.txt`, with the reason each module is in or
out written beside it) and leaves out the Python support on either side. Neither the list nor its
size is repeated here: the configure prints the set it builds, and the install carries exactly those
modules' `.cmo`.

```bash
printf '%s\n' '{ "CAMEL_PACKAGE_MINIMAL": "ON" }' > .camel-opts.json
rm -rf build out
node scripts/build.js                       # prints "CAMEL_PACKAGE_MINIMAL: building ..."
ls out/latest/stdlib/*.cmo                  # exactly the modules that line named
ls out/latest/libs                          # libcamel.so only
out/latest/bin/camel hello.cml              # Hello, <your user name>!
out/latest/bin/camel test/cases/smoke/core/fib10_pipeline.cml std::fvm
# Fibonacci(10) = 55 (computed in 0.0000 seconds)
```

A program that imports one of the dropped modules reports which module no longer provides its types:

```bash
printf 'import tiles\n\nfunc main(): int sync {\n    return 0\n}\n' > /tmp/tiles.cml
out/latest/bin/camel /tmp/tiles.cml
# error[SemanticDiag::NativeTypeNotProvided]: The type 'Device' is declared without a right side,
# so a C++ module has to provide it, and the module 'tiles' registered no type of that name.
```

Delete `.camel-opts.json` (and `build/`) to get the full build back.

## 5. Where the numbers in the paper come from

`node scripts/test.js test/plans/feat` runs the feature plans; each plan pins the observable
behaviour it is about (printed graph, diagnostics, program output) as data in the plan file, so a
reading a paper quotes can be traced back to a plan and re-run. `docs/build-minimal.en.md` is the
short path to a first build; `docs/cli.md` documents the command line, passes included.