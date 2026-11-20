# 构建这份源码并跑起一个程序

[English](build-minimal.en.md) | 中文简体

这一页只做一件事：让第一次看到这棵树的人，用最少的命令拿到 `camel` 并跑通一个程序。完整的开发
环境说明在 [setup.cn.md](setup.cn.md)，编译选项在 [build-options.cn.md](build-options.cn.md)。

## 1. 需要什么

| 工具 | 版本 | 用途 |
|---|---|---|
| CMake | ≥ 3.25 | 多配置生成器（`Ninja Multi-Config`） |
| Ninja | 任意较新版本 | CMake 的构建后端 |
| Clang | ≥ 19 | 源码用到 C++23；项目自己的主机用 23.1.2 |
| Conan | 2.x | 取 fmt / GTest / nlohmann_json / Taskflow / pybind11 / cpp-httplib |
| Python | 3.x | Conan 是 pip 包，需要它 |
| Node.js | ≥ 18 | `scripts/` 是构建的驱动 |

依赖第一次要从 Conan Center 下载；之后再构建不需要网络。

## 2. 三步构建

```bash
conan profile detect --force
node scripts/build.js
```

第一步生成 `~/.conan2/profiles/default`，要满足三处：`compiler=clang`、`compiler.cppstd=23`，以及

```ini
[conf]
tools.cmake.cmaketoolchain:generator=Ninja Multi-Config
```

第二步等价于 `npm run build`，它按顺序做四件事：`conan install . --output-folder=build --build=missing`；
`node scripts/diagnostics-catalog.mjs --generate`（诊断编号是生成的，先有它才有 `build/` 里那份头文件）；
`cmake .. -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake`；
`cmake --build . --config Release`。产物随后安装到 `out/latest/`：

```
out/latest/bin      camel、camel-ls、camel-format、camel-db、camel-cpp、camel-codegen、camel-pr
out/latest/libs     libcamel.so（Windows 上是 libcamel.dll）
out/latest/stdlib   标准库（.cml）与各模块的 .cmo
out/latest/include  libcamel 的头文件
```

32 核的机器上大约一分钟。**不要手工编辑 `out/`**：每次构建都会重建它。

## 3. 跑一个程序

```bash
out/latest/bin/camel hello.cml
# Hello, <你的用户名>!
```

`camel` 会按自身位置找 `libs/` 与 `stdlib/`，不必先设环境变量；要把 `camel` 挪到别处，就得设
`CAMEL_HOME=<安装根>` 并把 `$CAMEL_HOME/bin` 放进 `PATH`。

同一个程序的图可以直接看：

```bash
out/latest/bin/camel hello.cml std::gir
# digraph GraphIR { ... N00 [label="process:user_name", type="OPER", ...] ... }
```

文件名之后每个词是一个 pass（`std::gir` 打印图，`std::fvm` 是字节码 VM 执行，`std::opt safe_fp=true`
带参数）；不给 pass 时用 `std::nvm`。

## 4. 只构建一部分模块

源码包只需要语言与图那一半，不需要 GPU 后端与部署导出器。在树根写一个 `.camel-opts.json`：

```json
{ "CAMEL_PACKAGE_MINIMAL": "ON" }
```

再 `node scripts/build.js`（先删 `build/` 会从零配置）。它少建 `tiles`、`onnx`、`ckpt` 三个模块，
以及两个方向的 Python 支持（`python`/`pyplot` 的嵌入与 `bindings/python` 的扩展）。结果：`stdlib`
里 23 个 `.cmo` 而不是 28 个，`out/latest` 64 MB 而不是 68 MB，`out/latest/libs` 里只有 `libcamel.so`。
`nn` 不在这三个里：它是这个开关会在其中取舍的四个模块之一（`tiles`、`onnx`、`nn`、`ckpt`），留了
下来，所以案例与 MLP/transformer 训练步仍然有 `sgd` 和 `save_params`；它自己的 `.cmo` 就是多出
来的那 436,960 字节。

留下的 `tensor`、`random`、`autodiff`、`transforms` 是刻意的：它们是图与自动微分的演示面；
`random` 链接 `tensor`，所以裁 `tensor` 会把 `random` 一起带走。少一个模块时，`import` 它的程序
报错并且**指出是哪个模块没提供**：

```
error[SemanticDiag::NativeTypeNotProvided]: The type 'Device' is declared without a right side,
so a C++ module has to provide it, and the module 'tiles' registered no type of that name.
```

模块之间的链接依赖写在 `CMakeLists.txt` 的 `CAMEL_MODULE_LINKS_<name>` 里，配置阶段就校验，所以
`-DCAMEL_MODULES=tiles` 这种「选了 tiles 却没选 tensor」的写法在 configure 就失败，并点名两个模块，
不会拖到几分钟后的链接。默认两个开关都是关（`CAMEL_PACKAGE_MINIMAL=OFF`、
`CAMEL_ENABLE_PYTHON_BINDINGS=ON`），默认构建与以前完全一样。