# 交接：`task-205`（C2 —— 遍即函数第 3 步 + 第 5 步诊断）

来源：`lane-editview-a` 在 2026-11-28 01:2x 摸清了落点、但一格余量写不完"6 处一次写全"的一刀，按"枝上不留半成品"停手。这份清单接手即可开工，**不必重摸**。

## 已核定的结论（不必再讨论）

- `std::cxx` 不是模块，是 `std.cml:319` 里的 `namespace cxx`；`cse` 的声明在 `std.cml:402`
- 模块 `std` 今天**没有 C++ 侧**（`src/stdlib/std/` 顶层无 `.cpp`、无 `camel_module_create`）
- 已裁：**给 `std` 造 C++ 侧**（方案 B）；声明留在 `std.cml`，不搬家、不动解析路径
- `programOfGraph` 不必搬：本体就是 `ctx.programOfRuntimeGraph(*g)`，新执行器直接调
- `std::cxx::cse` **只有一处登记**：`src/stdlib/std/cxx/opt/generic/generic.cpp:2195` 的 `commonSubexpressionKernel`（`PassKernel`）；
  `:1943`/`:2073` 是传给 `rewriteReachableGraphs` 的**归因名字符串**，不是登记

## 要写的 6 处（一次写全，否则留下编译不过的半成品）

```
① CMakeLists.txt:284-287  模块集 `CAMEL_ALL_MODULES` 里加 `std`（否则 :316-318 的 add_subdirectory 不会被调）
② src/stdlib/std/CMakeLists.txt  新文件，照 src/stdlib/rewrite/CMakeLists.txt:1-15
   `include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)` + `add_camel_module(NAME std SOURCES module.cpp LINK_MODULES …)`
③ src/stdlib/std/module.cpp  新文件：`class StdModule final : public BuiltinModule`（`BuiltinModule("std", ctx)`）
   + `class StdExecutor final : public Executor` + `kernels()`（照 rewrite/module.cpp:4703-4730）
   + `extern "C" Module *camel_module_create(Context *ctx)`（照 rewrite/module.cpp:4957）
④ 访问声明：照 rewrite/module.cpp:4941 的成对形状；cse 读图 ⇒ 声明读 `graph`；算子记录的 `readsTags` 在同一处登记带上
⑤ 算子本体 `slot_t cseKernel(ArgsView &with, ArgsView &norm, Context &ctx)`：
   `auto *f = norm.get<::Function *>(0);`
   `auto &p = *ctx.programOfRuntimeGraph(*f->graph());`
   `const auto *out = CommonSubexpressionPass(ctx.shared_from_this()).apply(&p, os);`
   `return makeOk(toSlot(functionOver(norm, 0, out)), TupleType::fallible(norm.type(0)));`
⑥ 命令行侧同一笔：`applyRegisteredPass` 的 cse 分支分派到同一算子，**并删掉** generic.cpp:2195 的 `PassKernel`（不留双轨）
```

## 必须先过的一条读数（这刀真正的风险）

`std` 今天是纯 CML 模块，加 `add_camel_module(NAME std)` 以后必须证明：

- 模块被创建**一次**、Executor 注册**一次**（`camel_module_create` 对 std 只有一处；运行期不重复注册）
- 载入后 `std` 现有函数与类型照常（`smoke` 10/10、`feat/parse` 129/129）
- 若 `BuiltinModule("std")` 与"纯 CML 模块 std"**冲突**（同名两来源），**立刻停下贴冲突原文**，不许靠先后顺序绕

## 判据

- 程序里调 `std::cxx::cse` 与命令行 `camel p.cml std::cxx::cse` 的 `jgir` **逐字节相同**（`std::nvm` 至少一次）
- 算子记录含 `readsTags` 的证据（原文）
- 快层逐名新增红 0
- `numstat` + 新 tip

## 已知会挡路的一条

`Unresolved reference: 'std::cxx::cse'` 属 C1 ②（把算子声明成值），不属本刀。交回写法：**"入口已绿，验收红在 C1 ② 的未解析引用"**，并把已绿的读数贴全。