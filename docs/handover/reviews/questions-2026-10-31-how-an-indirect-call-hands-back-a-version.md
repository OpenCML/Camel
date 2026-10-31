# 一问：一次穿过函数值的调用写下的捕获，版本怎么交回来？

> 状态：等作者裁决。**② 已按三种已知情况开工，第四种（本页这一问）在 liveness 函数里留成有名字的分支，不猜。**

## 一句话

**一次穿过函数值的调用**可能写任意捕获变量，**它写了哪些在类型上不知道**，所以那个写之后的读**拿不到版本数据边**；今天这件事由一条 ctrl 边承担。裁决说"不在同步块里门不需要存在、写是否执行由后面的读决定"——**可在这条形状里，"后面的读"没有任何数据边可拿。**

## 三个 file:line：为什么版本边今天不存在

**一、`builder_expr.cpp:654-689`（`valueVersionOf`）只能给"直接调用"出版本。** 它取 `modifierOf(variable)`，再看 `callValues_` —— 而那是"**builder 知道被调者捕获写掩码**"时记下的表（`captureWriteMaskOf`，`builder_expr.cpp:577-585`；注释明说掩码是运行时 `GCFuncRef::reserved` 读版本的地方）。**本例的被调者是一个参数 `f`**（图里 `N09 [label="apply", type="FUNC", value_type="void"]`），**没有静态掩码**，`callValues_` 里没有 `counter`，于是 `valueVersionOf` 返回 `nullptr`。

**二、那条 ctrl 边自己就是"还没拿到的数据边的替身"。**`builder_expr.cpp:685-704`（`orderAfterLastWriter`）的原文：

> a control input, and it has to stay one **until the read gets the data edge it is owed**: for a call that reads a captured `var` this is the edge that makes the write the read uses execute (design principles 3.5, F3), and an order edge would leave the read with the value from before it.

**代码自己写着这是替身**；而本例里它等的那条数据边**拿不到**，因为被调者是值。

**三、登记那半是齐的。**`builder_operators.cpp:4306-4311`（`recordWrite(use.variable, targetNode, /*throughCallee=*/true)`）与 `builder_expr.cpp:569-574`（"A write of a captured variable is what callers of this graph have to see"）——**所以缺的不是"调用算不算写者"，是"间接调用的版本交不出来"**。

## 现状读数（`test/cases/effects/callback_write_ignored.cml`）

| 项 | 现在 | 应当 |
|---|---|---|
| `stdout` | **`0`** | `3` |
| 诊断 | **`EffectNotExecuted`**：`nothing depends on it and it is not in a sync block`（`callback_write_ignored.cml:16:33`） | 消失 |
| 图 | `N09 -> N0A [CTRL]`（调用 → `:var/get`）在，**体内那句写被删掉** | 体内写活着，读拿到它写的值 |

`EffectNotExecuted` 这条诊断**不是错的**——它把"这次写被丢掉了"说成了一句人话；它出现在这里，正说明边还没接上。

## 两条出路

**(A) 让这类调用也把版本交回来。** 运行时有 `GCFuncRef::reserved` 位掩码，"这次调用可能写哪些捕获"**运行时知道**、**类型上不知道**——所以 **(A) 不一定要新语言面**：可以是一个**动态版本口**，运行时按掩码把版本交出来，类型上只说"可能有一个版本"。代价：调用点多一个输出、导出面与两个 interchange 版本都要表示它。

**(B) 承认"读在一次间接调用之后"由一条"等这次调用"的边承担。** 代价：**"门不需要存在"要有一处例外**，或者这类边**换个名字留在原地**——等于承认**有一种"等一次调用"永远不是数据边**。

## 为什么这不能我自己定

3.1 只给了两样凭据（数据使用、同步链）。**这条形状里两样都还没有着落**：版本边要 (A) 才存在，链要同步块才存在。选 (B) 等于给 3.1 加第三种凭据；选 (A) 等于给调用点加一个可变输出。**两者都要作者一句话。**

## 现状怎么不挡路（已做）

- **② 的 liveness 函数今天就能存在**：按已定的三种情况写（数据使用者 ∨ 同步块语句 ∨ 块外"写者登记表 + 后面有读"），**第四种留成有名字的分支 `PendingIndirectCallVersion`，返回"未知"**，调用点保持今天的现状。这样 ② 的各遍可以逐块接手，不必等本页。
- **① 的块外那一半**：改动已存成补丁、工作树已回 `ab113941c`，等本页裁决后接着做。