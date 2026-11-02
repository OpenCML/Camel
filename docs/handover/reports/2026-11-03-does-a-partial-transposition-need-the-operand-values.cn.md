# 部分转置需不需要操作数的值

**读数树**：`lane-tloop @ 843d38025`（`origin/develop 00c59b0c6` + `lane-portgate`）。Release 构建。

**问题**：一条转置规则（`mul_t`）的右式要操作数的**值**（`ct * c` 里的 `c`），而部分转置的图
按设计**不携带**参与参数的值 —— 这个值是从闭包捕获通道取，还是由规则另想办法？本文记一条拒绝链、
一次实验、以及实验给出的那个"不成立"。

## 一、A 族那五行原文与三层拒绝坐标

用例 `test/cases/compile/transforms/transpose_many_parameters.cml`，第 23 行 `println(transpose(lin)(1.0))`，
`lin` 是捕获了 `3.0`/`2.0` 的 `mt`，`mt` 体内调 `autodiff::mul_tangent`。

```
TRANSPOSE-PROBE function=autodiff::mul_tangent<float> needs=3
TRANSPOSE-NEEDS function=autodiff::mul_tangent<float> param=0
TRANSPOSE function=autodiff::mul_tangent<float> skip=cotangent-needs-a-value uri=:op/mul_d position=1 reason=the rule's right side reads a value the caller does not carry
TRANSPOSE function=mt skip=call node=4 callee=autodiff::mul_tangent<float>
TRANSPOSE function=__g_58__ skip=call node=4 callee=mt
error[RuntimeDiag::RuntimeError]: … transforms: transpose has no transpose for this function.
   --> transpose_many_parameters.cml:23:13
```

**三层坐标**

| 层 | 位置 | 事实 |
|---|---|---|
| 规则的应用条件 | `modules/transforms/rule_expand.cpp:252` | `valueOf(argument)` 交回 `kInvalidNodeRef` ⇒ `why = "the rule's right side reads a value the caller does not carry"` |
| 规则本身 | `stdlib/rulesets/transpose.cml:54` | `mul_t: x * c => (ct) => (mul_cotangent(c, ct), mul_cotangent_c(x, ct))` —— 两条右式都读**操作数的值** |
| 拒绝的发起处 | `modules/transforms/transpose.cpp:1685` | 见 `answer == kInvalidNodeRef` ⇒ 打印 `skip=cotangent-needs-a-value` 并 `return false` |
| 抛出点 | `modules/transforms/transpose_operator.cpp:136` | `built.graph == nullptr` ⇒ `transpose has no transpose for this function` |

`transpose.cpp:1666` 的那句注释是本题的争点：*"A value parameter of a partial transposition takes no
cotangent, and computing one would need the value of a participating parameter -- the one thing this
graph does not have."*

## 二、两族的分界

- **A 族（5 条）**：`transpose has no transpose for this function` —— 分支、`bool` 条件、多参数、未读参数。
  缺的是**规则的应用条件**：规则右式要操作数的值。
- **B 族（6 条）**：`jvp has no forward transform for this function` —— 全部 `loop_escape`。
  缺的是 **`jvp` 的循环提升**（`jvp.cpp:1178` 的 `skip=loop`，`complete` 在 `:1144`/`:1166` 置假）。
  这一族归 `splitlift` 车道，本线不碰。

两族不是同一件事，**不能一起修**。

## 三、我方默认，与实验判据

**默认（作者口径下的合理读法）**：规则右式要的那个操作数**值**，在部分转置里不是"没有"，而是"没去取" ——
记成一次 need，由调用点把它捕获进来，复用已建好的捕获通道（`wantedValues` → `calleeNeeds` →
`transposeSignature(…, &capturesOf)` → `capturePortOf`），而不是让 `transpose.cpp:1666` 直接拒绝。

**实验（已做）**：给捕获通道加两个探针（`capture function=… param=… port=…` 与 `novalue function=… slot=…
capturePorts=… wanted=…`），跑 `transpose_many_parameters`。读数：

```
TRANSPOSE-PROBE function=autodiff::mul_tangent<float> needs=3
TRANSPOSE-DBG capture function=autodiff::mul_tangent<float> param=3 port=0
TRANSPOSE-NEEDS function=autodiff::mul_tangent<float> param=0
TRANSPOSE-DBG novalue function=autodiff::mul_tangent<float> slot=0 node=0 capturePorts=1 wanted=1
TRANSPOSE function=autodiff::mul_tangent<float> skip=cotangent-needs-a-value uri=:op/mul_d position=1 reason=the rule's right side reads a value the caller does not carry
```

**读出来的三件事**

1. **捕获通道本身是通的**：探针 set 里有 `param=3` 的条目，成员真的为它建了端口（`port=0`）。
   所以"参与参数的值进不了成员"这个说法**不成立** —— 它能进。
2. **但两个 set 不是同一个 set**：探针报 `needs=3`，真正落到端口上的只有 `param=3` 一项；真正
   在这次构建里被问的是 `param=0`，而它**不在**探针报上来的集合里（`capturePorts=1 wanted=1`）。
   记录用的是 `wantedValues`（`transpose.cpp:742`），消费方按 `funcType()->normTypeAt(at)`（`:384`）读 ——
   **端口下标与参数下标在"值参数站在参与参数前面"时不是同一个数**，`transpose.cpp:719` 的那段注释
   说的正是这同一个坑。
3. **问在构建途中，端口已经定死**：`param=0` 是在这个成员**已经按探针的集合建好端口之后**才被问到的，
   所以同一次构建里无论如何都补不进去 —— 补它需要**第二次构建**（探针 → 端口 → 再建一遍，直到不动点）。

## 四、我方默认不成立，要裁的那一句

**默认在一趟之内不成立，但不是"走不到那个位置"，而是"问到得太晚，且探针报的集合与构建要的集合不是同一个"。**

要裁的是这一句：

> **`buildTransposeMember` 允许迭代到不动点吗** —— 即：一次失败的构建（带着 `wantedValues`）是否可以用
> 扩充后的 need 集合**再建一遍**，直到不再出现新的 need 为止（上限若干次）？还是说，设计要求
> **探针必须一次报准**（那就要先修"端口下标 vs 参数下标"这一处，让探针的答案与构建的问法一致）？

两种做法都落在 `modules/transforms/transpose.cpp` 内，**不碰 `rulesets/transpose.cml`**；但前者改的是
构建的**结构**（一次构建变一轮迭代），后者改的是**下标的含义**（一处小修）。这是我不能自己定的那一步。