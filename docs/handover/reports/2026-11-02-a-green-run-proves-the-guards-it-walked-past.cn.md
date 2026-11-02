# 绿证明了 `GUARD`，没证明 `ASSERT`

**本页只说明一件事，不改构建。**

## 一条分界（原文）

```
include/camel/utils/assert.h:93-101
/// Its report is headed `Camel Invariant Violation` rather than `Debug Assertion`: this check is
/// alive in the build that ships, so its heading must not read like a build-time reminder.
#define GUARD(condition, reading) ... ::cml::fail_invariant(#condition, (reading)); ...

include/camel/utils/assert.h:102-104
#ifdef NDEBUG
#define ASSERT(condition, message) ((void)0)
```

**`GUARD` 在 `#ifdef NDEBUG` 之前定义，注释写明它在出货构建里活着；`ASSERT` 在 `NDEBUG` 下变成 `((void)0)`。**

⇒ **`GUARD` 类不变量：绿 = 被证明。`ASSERT` 类不变量：绿 ≠ 被证明（它在出货构建里根本不存在）。**

## 有多少条在"绿"的这一侧

`GUARD(` 在 `include/` 与 `src/` 下共 **878 行**（按行计）。最密的五个文件：`src/passes/sched/taskflow/taskflow.cpp` 62、`src/compile/gct/builder.cpp` 52、`src/passes/sched/nodevm/nodevm.cpp` 47、`src/passes/sched/fastvm/fastvm.cpp` 41、`src/execute/macro/eval.cpp` 36。

## 我差点写错的一条

我一度推断"`GUARD` 在 release 里被编掉，所以越界读只剩一次裸读、于是 SIGSEGV"。**原文否掉了它。** 这个例子的正确读法因此是：

**毒化构建印 `TupleType: index out of range`、而普通构建 `rc=139`（`grad_array_sum` / `grad_array_product`，`real=0s`）——既然 `typeAt` 的 `GUARD` 活着，那次 SIGSEGV 就不是 `typeAt` 越界，是别处踩空。** 后来查实：它是"数组的余切被当成数的余切来求和"，修复停在 `origin/lane-lit2`（`19f0b05e8`、`0aed45eb6`），没合。

## 推论

**在 `ASSERT` 那一侧，"release 全量绿"没有分辨力**；这一层的不变量只能靠带检查的构建证明。要在哪一档构建里保留 `ASSERT`，是构建层的决定，本页不改宏、不改 `-DNDEBUG`，只把它写下来。

## 顺带三条（同一次审计）

- `transforms.plan.toml` 四条 `known_fail` 的 note 已过期：一条已自愈（`grad_array_one_element`，`661127422` 上 `rc=0`，`REINTERP jvp … tangent=computed source=ruleset rules=jvp_idx_arr`），另外两条的崩溃已由 `lane-lit2` 修复、等合并。
- `expected exit 0, got null` 不是超时：裸跑这两条是 `real=0s`、`rc=139`。**`got null` 至少三因：超时被杀、信号杀死、以及尚未见过的第四种。**
- 因此"6 条 known failures"这个数里，条数稳定而形状已漂——**数与含义分家。**