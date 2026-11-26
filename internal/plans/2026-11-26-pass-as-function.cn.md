# 遍即函数：在实现上归一

## 要达到的效果

设计原则第 1、9 节说遍是函数：`(f: F) => F!` 的函数变换，流水线是函数组合。今天这只对用 Camel 写的遍成立；用 C++ 写的遍只在声明上像函数——编译器认出它的声明后丢掉（不建图、不建算子、不导出），名字在源码里解析不到，只能从命令行流水线上点名，还另有一张别名表。目标是让一个遍不论用什么写，在名字、登记、调用三件事上都与普通函数走同一条路，任何地方都不为"它是遍"开特例。

不做的（负责人 2026-11-26）：命令行最外层的执行循环留在宿主一侧（C++ 驱动程序），不改成在某个后端上求值的 Camel 程序；调度遍（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`、`std::nop`）只在流水线上用，不在程序里当函数调。

## 现状（读数，带位置）

- 编译器把无函数体、形如 `(f: F) => F! when is_function::<F>()` 的声明当遍：`src/compile/src/gir/builder.cpp:859-957`（`isPassDeclaration`、`visitDeclNode` 只记名字、返回空），导出时跳过（`builder_modules.cpp:1353-1357`）。带 `#op` 的例外（`rewrite`、`saturate`、`extract`、`run`）会成为算子，但 `extract` 因此有两个 C++ 入口（`src/stdlib/rewrite/module.cpp:1830-1837` 算子、`1852-1873` 流水线）。
- C++ 遍的接口吃整个程序：`apply(const Program*, std::ostream&)`（`src/kernel/include/camel/execute/pass/base.h:29-30`），只有工具会调（`camel-cli/main.cpp`、`timeit.cpp`、调试器）。
- 名字有两张表：核心的 `passAliases`（`src/kernel/execute/pass.cpp:97-111`，`std::opt` → `std::cxx::opt`）与模块登记的别名（`registerPassAlias`，`pass.cpp:362-398`）。
- 用 Camel 写的遍上流水线经一层转接（`src/kernel/execute/camel_pass.cpp:209-337`），只能作用于程序的根函数。

## 五处，各收回到普通函数的那一套

1. **名字只有模块导出一套。** 遍的声明照常编进模块并导出；命令行解析遍名用与 `import` 相同的解析（模块名加导出名）。别名就是模块在 `.cml` 里多导出一个名字（泛型函数作值有限制，所以别名写成转发：`export func echo::<F>(f: F) => F! when is_function::<F>() { return test_echo(f) }`）。删除 `passAliases` 与 `registerPassAlias` 及其表；`std::opt` 成为 `std.cml` 里一个真正的声明（转发到默认优化流水线）。
2. **登记只有算子表一处。** C++ 写的遍是一个原生算子：在算子表里按"一个算子一条记录"登记（设计原则 9.3），输入一个函数值，输出 `F!`。遍注册表并进算子表；`extract` 只留一个入口。
3. **调用只有一种。** 程序里写 `f->std::cxx::cse`、`f->onnx::export` 编成该算子的节点；运行时算子内核拿到函数值，以它为根取出它可达的图，在其上跑遍，交回新的函数值（图是值：原函数对其他持有者不变）。C++ 遍的接口因此从"整个程序"改成"以一个函数为根"；整个程序就是以 `main` 为根的情形。
4. **流水线就是逐个调用。** 命令行驱动程序仍在宿主一侧：对每个词解析出函数，把它作用于当前的根函数值，与程序里那次调用走同一个入口（原生算子走内核，Camel 函数走参考执行器）。用 Camel 写的遍因此不再需要 `camel_pass.cpp` 这层转接；写在库里的流水线函数（如 `rules::saturate_all`）里可以调 C++ 遍，并照样在命令行上点名。
5. **调度遍留在流水线。** 它们照常声明与导出（名字统一），在程序里调用时给一条说明"调度遍执行程序，只在流水线上使用"的诊断；行为不变。

## 顺序与验收

按 1 → 2 → 3 → 4 → 5 推进，每步一笔可合，验收都是读数：

1. 名字：`import { cse } from std::cxx` 一类导入能解析；`git grep -n "registerPassAlias\|passAliases" -- src` 为 0；`onnx::export`、`tensor::echo` 照常可在命令行使用（原有计划全绿）；源码里写 `std::opt` 能解析。
2. 登记：遍注册表的读者为 0（全部改问算子表）；`extract` 的 C++ 入口只剩一个；`stdlib-declarations --check` 与 `stdlib-reference --verify-ops` 全过。
3. 调用：新用例——同一个 C++ 遍，在程序里对函数值调用与在命令行上跑，打出的规范 jgir 逐字节相同（nvm/fvm/jit 三个后端）；作用于一个非根的函数值（闭包、库函数）也成立；原函数在调用后不变。
4. 流水线：`camel_pass.cpp` 删除；设计原则 9.3 那个例子的同形写法（库里一个流水线函数串起 C++ 遍与 Camel 遍）能编译、能在命令行点名、结果与逐个写在命令行上相同。
5. 调度遍：程序里 `f->std::fvm` 给出那条诊断（一条负例用例钉住），流水线行为不变。

全程：快层与合并闸门照常，`opt/camel_passes`、`opt/cxx_isolation`（Camel 遍与 C++ 遍对照）保持全绿。

## 文档

做完后改：设计原则第 9 节补"命令行驱动程序在宿主一侧，调度遍只在流水线上"；`docs/language/en/09_rules_and_facts.md` 第 9.8 节（未实现的写法）删去"`std::opt` 在源码里解析不到"那条；`docs/design/graph-native-optimizer/README.cn.md:147` 同步。
