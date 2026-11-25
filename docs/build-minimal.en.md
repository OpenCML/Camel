# Building this source tree and running one program

[中文简体](build-minimal.cn.md) | English

This page does one thing: everything needed to get a working `camel` from a fresh checkout and run a
program with it. The full development setup is [setup.en.md](setup.en.md), the build options are
[build-options.en.md](build-options.en.md).

## 1. What you need

| Tool | Version | Why |
|---|---|---|
| CMake | ≥ 3.25 | the multi-config generator (`Ninja Multi-Config`) |
| Ninja | any recent | CMake's build backend |
| Clang | ≥ 19 | the sources use C++23; the project's own host builds with 23.1.2 |
| Conan | 2.x | fetches fmt / GTest / nlohmann_json / Taskflow / pybind11 / cpp-httplib |
| Python | 3.x | Conan is a pip package and needs it |
| Node.js | ≥ 18 | `scripts/` drives the build |

The dependencies are downloaded from Conan Center the first time; after that the build needs no
network.

## 2. Build, in two commands

```bash
conan profile detect --force
node scripts/build.js
```

The first writes `~/.conan2/profiles/default`. It has to say three things: `compiler=clang`,
`compiler.cppstd=23`, and

```ini
[conf]
tools.cmake.cmaketoolchain:generator=Ninja Multi-Config
```

The second is the same as `npm run build`, and does four things in order: `conan install .
--output-folder=build --build=missing`; `node scripts/diagnostics-catalog.mjs --generate` (the
diagnostic numbers are generated, and the header in `build/` comes from that step);
`cmake .. -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake`;
`cmake --build . --config Release`. What it builds is then installed under `out/latest/`:

```
out/latest/bin      camel, camel-ls, camel-format, camel-db, camel-cpp, camel-codegen, camel-pr
out/latest/libs     libcamel.so (libcamel.dll on Windows)
out/latest/stdlib   the standard library (.cml) and each module's .cmo
out/latest/include  libcamel's headers
```

About a minute on 32 cores. Do not edit `out/` by hand: every build rebuilds it.

## 3. Run a program

```bash
out/latest/bin/camel hello.cml
# Hello, <your user name>!
```

`camel` finds `libs/` and `stdlib/` from its own location, so no environment variable is needed
first. Move the binary somewhere else and you do need `CAMEL_HOME=<install root>` with
`$CAMEL_HOME/bin` on `PATH`.

The graph of the same program is one word away:

```bash
out/latest/bin/camel hello.cml std::gir std::nop
# digraph GraphIR { ... N00 [label="process:user_name", type="OPER", ...] ... }
```

Every word after the file name is a pass (`std::gir` prints the graph, `std::fvm` executes on the
bytecode VM, `std::opt safe_fp=true` carries a parameter); with no pass it is `std::nvm`.

## 4. Build only part of the modules

A source package only needs the language and the graph half, not the GPU back end and the deployment
exporter. Write a `.camel-opts.json` in the root of the tree:

```json
{ "CAMEL_PACKAGE_MINIMAL": "ON" }
```

and run `node scripts/build.js` again (delete `build/` first to configure from scratch). What the trim
leaves out is written in one place and this page does not repeat it: `CAMEL_MINIMAL_DROPS` in the
top-level `CMakeLists.txt` names the modules, with the reason each one goes or stays beside it, and
the two directions of Python support are the options above. The configure then prints the set it
builds (`CAMEL_PACKAGE_MINIMAL: building ...`), the install carries exactly those modules' `.cmo`,
and the size difference is a reading to take off that build rather than a number written here.

When a module is missing, a program that imports it says so, and names the module:

```
error[SemanticDiag::NativeTypeNotProvided]: The type 'Device' is declared without a right side,
so a C++ module has to provide it, and the module 'tiles' registered no type of that name.
```

The link dependencies between modules are not a list in the top-level file either: a module names the
sibling it links beside its own sources (`LINK_MODULES` in `modules/<name>/CMakeLists.txt`), and
`CMakeLists.txt` reads those pairs back and checks them against the selected set. So
`-DCAMEL_MODULES=tiles` without `tensor` fails during configure and names both modules instead of
failing at a link several minutes later. Both switches default off (`CAMEL_PACKAGE_MINIMAL=OFF`,
`CAMEL_ENABLE_PYTHON_BINDINGS=ON`), so a default build is unchanged.