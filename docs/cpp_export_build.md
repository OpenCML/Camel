# 从 std::cpp 生成代码的编译与运行

`std::cpp` 把一个 Camel 程序转译成一个 C++20 源文件。生成的程序**不依赖 Camel 运行时**：它只包含随 Camel 安装的一组头文件（`$CAMEL_HOME/include/camel/standalone/`，只用 C++ 标准库），用任何 C++20 编译器一条命令就能编成可执行文件，运行时不需要 `libcamel`、不加载 `.cmo`。设计见 [docs/design/cpp-standalone/README.cn.md](design/cpp-standalone/README.cn.md)。

程序用到某个还没有独立译法的模块算子时（例如 `fs`、`re` 的算子），生成的文件另外包含 `camel/standalone/bridge.h`，要链接 `libcamel`；这种情形文件开头的注释会写明是哪些算子、怎样编译。

---

## 1. 一步完成：camel-cpp

```bash
camel-cpp test/cases/linear/nvm/fib.cml            # 写出 fib.cpp，编出 fib（Windows 上 fib.exe）
camel-cpp prog.cml -o build/prog                   # 指定可执行文件
camel-cpp prog.cml -o prog std::opt::inline             # std::cpp 之前先跑别的遍
```

`camel-cpp` 调用同目录下的 `camel <文件> [遍…] std::cpp` 得到 C++，写到可执行文件旁边的 `<名字>.cpp`，再用 `$CXX`（没有时用 `c++`，Windows 上用 `clang++`）编译。头文件取自安装目录（`$CAMEL_HOME`，没有设时取 `camel-cpp` 所在的安装）。需要运行时的程序会自动加上链接参数。

---

## 2. 分两步

```bash
camel prog.cml std::cpp > prog.cpp
c++ -std=c++20 -O2 -ffp-contract=off -I"$CAMEL_HOME/include" prog.cpp -o prog
./prog
```

要链接运行时的程序（文件开头的注释会说明）：

```bash
c++ -std=c++20 -O2 -ffp-contract=off -I"$CAMEL_HOME/include" prog.cpp -o prog \
    -L"$CAMEL_HOME/libs" -lcamel -Wl,-rpath,"$CAMEL_HOME/libs"
```

它运行时通过 `CAMEL_HOME` 找到模块（`.cmo`），与 `camel` 相同。

Windows 上链接构建产物中附带的导入库，并在运行前将 DLL 目录加入 `PATH`：

```powershell
clang++ -std=c++20 -O2 -ffp-contract=off -I"$env:CAMEL_HOME/include" prog.cpp "$env:CAMEL_HOME/libs/libcamel.lib" -o prog.exe
$env:PATH = "$env:CAMEL_HOME/libs;$env:PATH"
./prog.exe
```

`camel-cpp` 会自动链接导入库；运行它生成的桥接程序时同样需要上述 DLL 搜索路径。完全独立的程序不需要这一步。

**PowerShell 的重定向**默认写 UTF-16，C++ 编译器读不了；改用：

```powershell
[System.IO.File]::WriteAllText("prog.cpp", (camel prog.cml std::cpp | Out-String), [System.Text.UTF8Encoding]::new($false))
```

或者直接用 `camel-cpp`。

### 放进自己的 CMake 工程

生成的是一个文件、一个包含目录：

```cmake
add_executable(prog prog.cpp)
target_compile_features(prog PRIVATE cxx_std_20)
target_include_directories(prog PRIVATE $ENV{CAMEL_HOME}/include)
```

---

## 3. 生成的程序做什么

- **输出**：与 `camel prog.cml std::fvm` 相同的字节（Windows 上也不写 `\r\n`）。
- **故障**（除零、越界、转换越界等）：与解释器相同的诊断文字，带源码位置与源码摘录，写在标准输出上，退出码 1。`CAMEL_ERROR_FORMAT=short` 时是一行的形式。
- **递归上限**：与调度器的规则相同，默认是转译时的上限（`--max-recursion-depth`，否则 1000）。尾调用（包括互相尾调用的一组函数）写成循环，不使栈增长、不计数。
- **退出码**：`main` 声明了整数返回类型时是它的返回值；`exit(n)` 是 n。
- **`main` 的参数**（设计原则 10.6）：与 `camel` 一样用 `--arg 名字=值` 给出，按声明的类型读（整数、浮点数、复数、`true`/`false`、原样的文字）：`./prog --arg n=3 --arg name=hi`。整数与浮点按声明的宽度读：`i32` 是 int32、`f32` 是 float，装不下的值被拒绝并说明是哪种类型及其范围，与 `camel` 逐字相同。浮点数是一个完整的数字记号（`parseWholeFloat`：没有十六进制浮点、`Infinity` 或空白）。复数按语言里的复数文字读（`1+2i`、`-2i`），每个部分也按这条数字规则读；实数就是实部、虚部为 0 的复数（`1.5` 是 `1.5+0i`）。写错的、多给的、重复的、缺少的参数报告与解释器逐字相同（`EntryArgumentInvalid`、`EntryArgumentMissing`，位置在 `main`），退出码 1。参数类型是 `int`、`i32`、`float`、`f32`、`c64`、`c128`、`bool`、`string` 之外的（张量、结构体从文件读，只有 `camel` 能读）时拒绝转译。转译时已经用 `--arg` 绑定的参数成为程序里的常量。
- **`map`、`filter`** 在生成的程序里按元素顺序依次执行（设计原则 10.6）。

---

## 4. 转译不了的程序

`std::cpp` 遇到还不能转译的构造时给出 `TranslationRefused`，位置是那个构造，说明是什么类型或算子。能转译的范围随 [设计稿](design/cpp-standalone/README.cn.md) 第 12 节的各阶段扩大；当前的数字见那里的 12.1。

其他模式：`std::cpp::module` 写出模块里的具名函数（没有 `main`），`std::cpp::inspect` 在此之上给每个函数加一段列出节点的注释。

---

## 5. 测试

```bash
python3 test/tools/cppexec/run.py prog.cml                 # 转译、编译、运行，与 std::fvm 比较
python3 test/tools/cppexec/run.py test/cases benchmarks    # 整个目录：结果分类、拒绝理由、编译时间
python3 test/tools/cppexec/run.py --expect test/tools/cppexec/expect.txt   # 测试计划用的清单
```
