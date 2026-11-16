# `casevm.cpp` 在任何配置下都编不出来：删掉还是连 20 处一起修？

> 一页一问。**默认建议写死在下面，本轮按默认开工，不另等。**

## 这一页要的答复

`src/passes/sched/fastvm/casevm.cpp` 是 `std::fvm` 的第二个解释器体，整份文件在
`#if !ENABLE_FASTVM_COMPUTED_GOTO` 里，而 `cmake/Options.cmake:63` 把
`ENABLE_FASTVM_COMPUTED_GOTO` 默认设为 `ON`，默认构建只编 `gotovm.cpp`——**默认构建里它一个
字节都没编，也一个字节都没跑。**

把选项关掉它也**编不出来**，而且与 2026-11-16 那一笔无关（那一笔只在一个调用点上加了钩子）：

```
$ clang++ … -DENABLE_FASTVM_COMPUTED_GOTO=0 -fsyntax-only casevm.cpp
casevm.cpp:21:10: fatal error: 'camel/utils/opperf.h' file not found
```

那个头文件叫 `src/passes/sched/fastvm/opperf.h`，孪生位置（`fastvm.cpp:17`、`gotovm.cpp:79`）
用的是 `"opperf.h"`。只在 `/tmp` 的副本上改这一行再编，后面还有二十处：

```
casevm.cpp:70:  unknown type name 'Frame'
casevm.cpp:129: use of undeclared identifier 'Type'
casevm.cpp:140: unknown type name 'TypeCode'
casevm.cpp:142: unknown type name 'Object'
…（-ferror-limit 在第二十个错误处停下）
```

**这些都是预存在的**：我这一笔只在 `casevm.cpp` 的 `OpCode::SCHD` 分支里把一个直接调用换成了
`camel::execute::callOperator`（与 `gotovm.cpp` 的孪生分支同一形状），没有动它的 include、也没有
动它用的任何一个类型。文件烂到什么程度与「它挂在哪一行」无关。

## 默认建议（按此开工，不另等）

**本轮什么都不做，但把它记成待办**：等计数这件事（`include/camel/execute/operator_call.h` 那条路
与计划侧的表）全部落地、`std::fvm` 只剩一个解释器体之后，在下面两条里选一条：

- **删掉这一支**（连带删掉 `ENABLE_FASTVM_COMPUTED_GOTO` 这个选项，只留 `gotovm.cpp`）；或
- **连它的二十处一起修**，把它重新变成能编、能跑、能与 `gotovm.cpp` 对照的第二条解释器体。

**不做「只改那一行 include」。** 那会让一个编不过的分支看起来编得过：`-fsyntax-only` 过了第一行，
下一行的 `Frame` 还是未知类型，于是「它编过了」这句话只对第一行成立——比现在更糟，因为现在至少
没有人以为它能编。

## 为什么现在不改

改这一行会把这条**不可达分支的其余部分**（`Frame` 不再存在、`TypeCode`/`Object` 的接口早已搬走）
一起暴露出来，那不是顺手能做的事，需要一次独立判定：要么整个删，要么整套修。它也不在计数这件事的
路上——`std::fvm` 与 `std::jit` 的读数来自 `gotovm.cpp` 与 `native_call.cpp`，那是编过、跑过的。

## 这一页与哪一笔有关

- `5f90e0ce0`（`refactor(exec): call an operator through one hook, and count it there`）：
  这一支的 `OpCode::SCHD` 分支被接上了钩子；文件头与分支上的注释都写明「没有编过、没有跑过」。
- `docs/handover/reports/2026-11-16-what-the-missing-count-was-and-was-not.cn.md`：同一件事在
  报告里的一节。