# `panic` 的两个入口，今天一边也没有写入者

本页记一个**返工合入前必须先补的洞**。它不是缺陷报告，是一张**合入检查单**。

## 一、读到的两个事实

```
$ git grep -n 'panic' -- 'lib/std/**/*.cml'      # 声明侧：只有一处提及，且不写 panic
$ git grep -n 'mayFault\|panic' -- 'lib/std/builtin/**/*.cpp' 'compile/**'   # 登记侧：零写入者
```

第二条是 `exf` 车道在 `391d7b2ce` 里查出来的，原文：

> 唯一的 `:op/copy` 声明不写 `panic`，且**整棵树没有任何 `panic` 写入者**（`compile/` 与 `CppRegistration` 皆无，返工版也没有），所以今天 `mayFault()` 对任何记录都读出保守的 `true`——对 COPY 与现有 `false` **不同**。

## 二、为什么这是一个洞而不是一个待办

`operator_record.h` 的返工版把 `panic` 定成 `Stated<bool>`：Camel 入口是「有没有写这个关键字」，C++ 入口是**必填的布尔值，不填即登记失败**。

于是：

| 入口 | 今天的状态 | 按新原则应是 |
|---|---|---|
| Camel 侧 `lib/std/prelude.cml` 的 `:op/copy` | **不写 `panic`** | 不写 = **承诺不会故障** ⇒ `false` |
| C++ 侧 `lib/std/builtin/modules/globals.cpp:2105-2113` | **不填** | **必填** ⇒ 合入后会编译失败，或填了才知道填什么 |
| **库里一个 `panic` 写入者都没有** | —— | —— |

而 `mayFault()` 的实现是 `!panic.stated() || panic.value`，对「没人说」取保守的 `true`。所以：

**今天每个算子的 `mayFault()` 都返回 `true`，包括 `:op/copy`。**

## 三、后果：改成读记录会**真改语义**

`kernel/runtime/graph.cpp:79-95` 现在按形状判：CAST 可能故障、其余不会。对 `copy` 它给 `false`，因此 `copy` 是可自由调度的语句。

若把那一处改成读记录的 `mayFault()`，`copy` 会从 `false` 变成 `true` —— **`copy` 不再是可自由调度的语句**。这不是重构，是行为改变。

`exf` 因此**没有改**，只在 `graph.cpp:79-95` 留了注释：两条读数各是什么、今天为什么分家、记录带上 `panic` 后每臂改成 `camel::ops::Registry::instance().find(oper)->mayFault()`。

**判据写在那里之后，下一次有人「顺手把它改成读记录」时会看见分家的理由。**

## 四、合入检查单

`operator_record.h` + `facts.h` 的返工（`6489e1ca6`）合入时，**同一批**必须做完：

1. `lib/std/builtin/modules/globals.cpp:2105-2113` 给 `:op/copy` 的 `CppRegistration` **填上 `mayFault`**（按原则：`copy` 不故障 ⇒ `false`）。不填就是编译失败——这是返工特意设计的，好。
2. `lib/std/prelude.cml` 的 `copy` 声明**不写** `panic`，与上一条一致（不写 = `false`）。
3. 把 `kernel/runtime/graph.cpp:79-95` 每臂改成读 `mayFault()`，**并报出改前改后对每个算子的取值表**——若表里只有 `copy` 变化且方向是 `false`，才说明这一改是「读数来源变了、语义没变」。
4. 跑 `feat/compile`、`feat/runtime`、`feat/opt`、`feat/consistency`，四次 `[SUMMARY]` 的 `fail` 全为 0。

**第 3 条的表是判据。** 没有那张表，「改成读记录没改语义」就只是一句话。

## 五、一句话

**一个「必填」，只有在有人真去填的时候才有约束力。** 今天两侧都空着，而读取方又对空取值取了保守默认——于是「必填」这件事在库代码里一次也没有发生过。返工把 `panic` 收成 `Stated<bool>` 是对的，但它**同时**需要一次真实写入，否则那个布尔值的第一个取值仍来自默认。