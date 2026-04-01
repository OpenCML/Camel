# 从 std::cpp 生成代码的编译、链接与执行

## 流程概述

1. **生成 C++**：用 `camel` 将 `.cml` 转成 C++ 源码
2. **编译链接**：使用 Clang/CMake 编译生成代码，并链接 `libcamel`
3. **执行**：运行生成的可执行文件（需要能找到 `libcamel.dll`）

---

## 0. 一键流程（camel-cpp 推荐）

若只需从 `.cml` 直接得到可执行文件，无需手动生成、编译、拷贝 DLL：

```powershell
# 构建 camel-cpp（与 libcamel 一起）
cmake --build build --target camel-cpp --config Release

# 一键：转译 + 编译，产物输出到 tmp/
build\tools\camel-cpp\Release\camel-cpp.exe test\run\linear\fib.cml

# 产物：tmp/fib.cpp、tmp/fib.exe、tmp/libcamel.dll
.\tmp\fib.exe
```

camel-cpp 会自动：解析并编译 `.cml` → 应用 `std::cpp` pass → 写入 `tmp/<stem>.cpp` → 调用 clang++ 编译 → 将 exe 与 libcamel.dll 拷贝到 `tmp/`。详见 [CLI 文档](cli.cn.md#相关工具related-tools)。

---

## 1. 生成 C++ 源码

```powershell
# 示例：生成 fib.cml 的 C++
camel -L "D:/Projects/camel-1/build/modules/time/Release" test/run/linear/fib.cml std::cpp > gen.cpp
```

- 用 `-L` 指定 `.cmo` 模块所在路径
- 输出重定向到文件（如 `gen.cpp`）
- **编码**：生成文件需为 UTF-8，否则 Clang 可能报错。PowerShell 默认重定向为 UTF-16，可用以下方式获得 UTF-8：
  ```powershell
  [System.IO.File]::WriteAllText("gen.cpp", (camel ... std::cpp | Out-String), [System.Text.UTF8Encoding]::new($false))
  ```
  或用 cmd：`cmd /c "camel ... std::cpp > gen.cpp"`

---

## 2. 编译与链接

生成代码依赖 Camel 头文件和 `libcamel` 运行时，需要一起参与编译和链接。

### 方式 A：在主工程中作为可选目标（推荐）

```powershell
# 配置时指定生成文件路径
cmake -S . -B build -DCPP_GEN_SOURCE=D:/Projects/camel-1/gen.cpp

# 编译 cpp_gen 目标
cmake --build build --target cpp_gen --config Release
```

生成的可执行文件：`build/cpp_gen/Release/cpp_gen.exe`（或对应配置目录）

### 方式 B：用脚本单独编译（示例）

```powershell
# 假设 gen.cpp 在项目根目录
$CAMEL_ROOT = "D:\Projects\camel-1"
$BUILD = "$CAMEL_ROOT\build"
$GEN_CPP = "$CAMEL_ROOT\gen.cpp"

clang++ -std=c++23 -O2 `
  -I"$CAMEL_ROOT/include" `
  -I"$CAMEL_ROOT/src" `
  -I"$CAMEL_ROOT" `
  -DANTLR4CPP_STATIC -DPLATFORM_WINDOWS `
  "$GEN_CPP" `
  -o "$BUILD/cpp_gen.exe" `
  -L"$BUILD/Release" -lcamel `
  -lfmt -lnlohmann_json -lTaskflow
```

依赖路径（fmt、nlohmann_json、Taskflow）需根据你的 Conan 安装目录调整。

---

## 3. 运行

生成的可执行文件依赖 `libcamel.dll`，需保证能正确找到 DLL。

### Windows

```powershell
# 将 libcamel.dll 所在目录加入 PATH 后运行
$env:PATH = "D:\Projects\camel-1\build\Release;$env:PATH"
.\build\cpp_gen\Release\cpp_gen.exe
```

或用完整路径：

```powershell
# 在 build/Release 目录下执行
cd D:\Projects\camel-1\build\Release
..\cpp_gen\Release\cpp_gen.exe
```

### Linux / macOS

若配置了 RPATH，可直接运行；否则需设置 `LD_LIBRARY_PATH`（Linux）或 `DYLD_LIBRARY_PATH`（macOS）。

---

## 4. 一键流程示例

**推荐：使用 camel-cpp**（见上文「0. 一键流程」）

```powershell
build\tools\camel-cpp\Release\camel-cpp.exe test\run\linear\fib.cml
.\tmp\fib.exe
```

**手动流程**（需分步生成与编译时）：

```powershell
cd D:\Projects\camel-1

# 1. 生成 C++
build\Release\camel.exe -L "build/modules/time/Release" test\run\linear\fib.cml std::cpp > gen.cpp

# 2. 配置并编译（需在 CMakeLists 中添加 cpp_gen 目标）
cmake -S . -B build -DCPP_GEN_SOURCE=$PWD/gen.cpp
cmake --build build --target cpp_gen --config Release

# 3. 运行
$env:PATH = "build\Release;$env:PATH"
build\cpp_gen\Release\cpp_gen.exe
```

---

## 5. 生命周期与模块加载

生成的 `main` 会自动调用：

- **`initialize()`**：基础初始化（context + 库搜索路径，通过 `CAMEL_HOME` 定位 SDK）
- **`importModules({...})`**：导入依赖的 `.cmo` 模块（如 `time`）
- **`finalize()`**：清理全局单例，释放 .cmo 句柄等

算子访问通过 `bridge()` 获取全局 bridge，未初始化时由 libcamel 抛出明确错误。

模块搜索路径与 camel-cli 一致，支持环境变量：
- `CAMEL_HOME`：安装根（用于推导 `bin/`、`libs/`、`stdlib/`）
- `CAMEL_PACKAGES`：附加模块根路径（Windows 下用 `;` 分隔）
- `CAMEL_STD_LIB`：覆盖 stdlib 路径
- `CAMEL_MODULE_PATH`：额外 `.cmo` 路径（如 `build/modules/time/Release`）

---

## 6. 常见问题

- **找不到 libcamel.dll**：
  - 生成 exe 通过 `setupLibrarySearchPathForApp` 搜索，优先：exe 所在目录、`CAMEL_HOME`、`CAMEL_HOME/libs`、`CAMEL_HOME/bin`。
  - `CAMEL_HOME` 指向 SDK 根目录（如 `out/latest`），其下应有 `bin/`、`libs/` 等子目录。
  - 若 exe 部署到其他位置，设置环境变量 `CAMEL_HOME` 指向 SDK 根目录即可。
  - 或将 `libcamel.dll` 与 exe 放在同一目录。
- **`python.cmo` 已找到但 failed to load DLL**：
  - 这是 Python 运行时依赖缺失，不是模块搜索失败。
  - `collect-out` 从 `modules/python/sdks/python3xx/` 收集 `python3xx.dll`、`python3.dll`、`vcruntime140*.dll` 到 `out/latest/libs/`；若 sdks 中缺少解释器 DLL，再回退到当前 venv/conda/ PATH 上的 Python。
  - 也可激活目标 venv（`VIRTUAL_ENV`/`CONDA_PREFIX`），运行时会把其常见 DLL 目录加入搜索路径。
- **Python SDK 准备（用于编译 python.cmo/bridge）**：
  - 手动同步 SDK 到 `modules/python/sdks/`（gitignored）：
    - `node scripts/sync-python-sdks.js <python-archive-root>`
  - 构建优先从 `modules/python/sdks/` 查找；若不存在则回退当前激活 venv。
  - 若两者都缺失，仅跳过 `python.cmo` 编译，不影响其它模块。
- **链接错误**：检查 Conan 依赖和 CMake 配置是否与主工程一致
- **模块未找到**：`-L` 指向的目录需包含对应 `.cmo` 文件（如 `time.cmo`）
- **UTF-16 BOM 错误**：PowerShell 重定向默认 UTF-16，Clang 会报错，请用文档中的 UTF-8 写入方式
- **undeclared identifier**：若子函数在调用者之后定义，需在生成器中增加前向声明或调整函数发射顺序
