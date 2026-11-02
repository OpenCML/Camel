# `:op/ne_*` 不是 `==` 的第二份实现，是 `!=` 的兜底

## 要作者看的一句

作者 2026-11-02 的裁定（`a1b782c19`，提交信息原文）是 "the ne_ rows are a second implementation of == and go"。**但删掉它们之后全量掉了 508 条**，而 `modules/graph/module.cpp:110` 的注释与代码说清了为什么：**`!=` 的排除法（`a != b` 即 `!(a == b)`）只覆盖"有 `==` 可取"的场合；对 handler 那类没有 `==` 可取的重载，编译器要的是 `__neq__` 组本身。** 按本轮页里写死的判据——"掉了就说明有地方直接调，那就不是候选"——**默认动作是保留**，而"裁定与读数冲突"这件事需要作者裁。

## ① 读数：删 9 条规则之后

```
src/builtin/modules/globals.cpp 里 ${':op/ne_i', ':op/ne_l', ':op/ne_f', ':op/ne_d', ':op/ne_b',
                                   ':op/ne_s', ':op/ne_bytes', ':op/ne_c', ':op/ne_z'} 共 9 条、63 行
删后全量：total: 8390, pass: 7866, fail: 508, skip: 10, known failures: 6

[FAIL] array_whole.nvm       194.15 ms
[FAIL] array_whole.fvm       223.62 ms
[FAIL] array_whole.jit       195.33 ms
[FAIL] array_whole.prl       161.38 ms
```

已 `git checkout -- src/builtin/modules/globals.cpp` 撤回，**没有留下半成品**。

**口径更正**：`11 条 / 22 行` 是旧口径（另一棵树、另一天），**今天这棵树上就是 9 条 / 63 行**，以此为准。

## ② 原文（这条注释本身就是答案）

`modules/graph/module.cpp:110`：

```
// `==` of the handle types is declared in stdlib/graph.cml (design principles 4.3): two
// handles are equal when they name the same thing of the same Program. `!=` cannot be
// declared -- an overload of `==` gives it, and `operator !=` is OperatorNotOverloadable
// -- so the module keeps the `__neq__` group the compiler asks for when it does not
// negate a Camel overload. Both names are default imported: an importer of `graph`
// finds them among a type's operators without naming them.
std::vector<std::pair<std::string, resolver_ptr_t>> ne;
for (HandleKind kind : kHandleKinds) {
    if (kind == HandleKind::Program || kind == HandleKind::Txn) { continue; }
    Type *type = HandleType::get(kind);
    ne.emplace_back("graph:ne", StaticFuncTypeResolver::create({}, {{type, false}, {type, false}}, Type::Bool()));
}
exportEntity("__neq__", OperatorGroup::create("__neq__", ne));
```

另一处按名构造的消费者：`modules/onnx/lowering.cpp:1376` — `add(":op/ne_" + t, scalarCompare("Equal", true));`。
内核对：`src/builtin/executors/builtin.cpp` 里 `{"op/ne_i", __builtin__ne__}` … 共 9 条。

## ③ 问句与三条路

> **`:op/ne_*` 保留，还是给"没有 `==` 可取的重载"另造一条兜底路径？**

- **A 保留兜底（默认建议）**：`__neq__` 组留着，用户写的 `!=` 仍由排除法走；代价是"`==` 与 `!=` 两处实现"这件事继续存在，但它服务的是**两种不同的场合**；
- **B 另造一条路径**：让没有 `==` 的重载也有 `!=` 落点（例如把 `__neq__` 变成编译器在无处取反时的**内建降级**，而不是一个可注册的组）——**这是机制，不是删行**，改动面在编译器与模块两处；
- **C 其它**：作者另有裁法。

**默认（作者未裁时按此办）**：**A 保留**，不动手。

## ④ 一句比这条缺陷更通用的

**用户写的 `!=` 与编译器内部的 `__neq__` 组同名不同义**——用户那一侧确实是 `!(a == b)`（作者裁定成立），编译器那一侧是"没有 `==` 可取时的兜底组"。**这是今夜第五处"同名不同义"。**

## ⑤ 凭据

本轮读数在 `lane-gs6`（`f1881400c` + 上述 9 条删除试验，已撤回）上量的；构建凭据：

```
"footprint": "261102_021835_c58f3f8ba_dirty", "builtAt": "2026-11-02T02:18:48.179Z"
```

`等点名`。