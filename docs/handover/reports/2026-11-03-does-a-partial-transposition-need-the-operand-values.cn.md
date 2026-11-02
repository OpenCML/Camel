# 部分转置需不需要操作数的值

**读数树**：`lane-tloop @ 843d38025`（`origin/develop 00c59b0c6` + `lane-portgate`）。Release 构建。
**第五节是后续车道在同一分支 `719e81272` 上的更正与读数，它作废本文第三节第 2 点与第四节。**

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
2. **~~两个 set 不是同一个 set：下标不同义~~ —— 这个说法作废，见第五节。** 记录端与消费端用的是
   同一个数：`normPorts()` 的位置 `i` 就是参数的位置（`div_tangent`、`index_into` 都印证），
   `transpose.cpp:742` 记 `i`、消费方按 `normTypeAt(at)` 读，两者在本题里一直是同一个数。
   真正的差别不在下标，而在**探针停在哪里**：见第五节。
3. **问在构建途中，端口已经定死**：`param=0` 是在这个成员**已经按探针的集合建好端口之后**才被问到的，
   所以同一次构建里补不进去 —— 这一点仍然成立，原因见第五节。

## 四、我方默认不成立（已被第五节取代）

**~~默认在一趟之内不成立~~**：一趟之内成立，但成立的办法不是"迭代到不动点"，也不是"改下标"。见第五节。

> 原裁决问句（**已裁：允许"探针用占位值应答自己的请求"**，理由是探针的 draft 整张丢掉、不重建、
> 不存在第二轮构建，所以它不是"迭代到不动点"，而是"让一次走查走完"）：`buildTransposeMember`
> 允许迭代到不动点吗？还是探针必须一次报准？

## 五、实际机理：探针停在第一个答不上的请求上（2026-11-03，lane-tloop）

**记录端的下标不是病灶。** 第 1 步把 `:742` 从记 `i`（`normPorts()` 位置）改成记 `at`（参数位置）并
在 `i >= paramCount` 时不记，**`feat/compile/transforms` 42/60、`feat/transforms/split` 14/18 一条没动**
（`fb2ec50df`）。两处是同一个数，改的是措辞而不是行为。

**真机理：** 探针（`buildTransposeMember` 开头那次 scratch 构建）**停在第一个它答不上的请求上** ——
那个请求返回 invalid，规则右手（`rule_expand.cpp:252`）直接失败，整条走查结束。所以探针永远只报
**第一个**要的值。`mul_tangent` 的体 `dx*w + x*dw` 有两条 `mul_d`，每个位置各问一个操作数，真正要的是
**四个**；探针只报第一个（`needs=3`），实建里 `param=3` 被捕获端口答上后继续走，下一个 `param=0` 才断。

**一趟内报全的办法（已试、已量化）：** 探针在自己的 scratch draft 里**用一个同类型的端口占位应答**
自己的请求，走查就能走完；再把探针的 map 并回外层，让"调用点把参数转交给 callee"这一级也拿到 callee
的 need 集合。读数（`transpose_many_parameters`，本地树实测）：

```
TRANSPOSE-PROBE function=autodiff::mul_tangent<float> needs=3,0,1,2     ← 四个，一趟报全
TRANSPOSE function=autodiff::mul_tangent<float> graph=92
              type=(float, float, float, float, float) => (float, float, float, float)   ← 正是 plan 钉的形状
TRANSPOSE-PROBE function=mt needs=3,0,1,2                              ← 上一级也拿到了
TRANSPOSE function=mt graph=89 type=(float, float, float, float, float) => (float, float, float, float)
TRANSPOSE-PROBE function=__g_58__ needs=1,0
TRANSPOSE function=__g_58__ graph=59 type=(float, float, float) => (float, float)
```

**但它不能这样上树，原因不是 need 集合，而是类型侧：** 顶层 `transpose(lin)(1.0)` 按
`transpose_operator.cpp` 的 `transposeSignature(function)`（不带 captures）解析成 `(float) => (…)`，
而成员因为捕获端口变成 3 或 5 个 norm port ⇒ **调用点带 1 个实参、成员要 3 个**：

```
():?:?: [Error]: Pass 'std::nvm' failed with an internal error:
Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args,
target graph 'transpose_of___g_58__' has 3 norm ports.
```

同一根因在 `grad_array_no_int.fvm` 上更硬：`SIGABRT`（`fastvm.cpp:262`，
`FastVM indirect call norm-arity mismatch for graph
'transpose_of_split_of_jvp_of_both_backward': expected 2, got 1.`）。**这条改动因此已整体撤回**，
树停在 `719e81272`，`killed by a signal` 回到 0。

**要裁的那一句（新）——已裁，2026-11-03 17:10：** 作者原话：类型本来就由 `F` 静态定出，**捕获不属于类型**；
毛病在成员把已知值做成了 `with` 端口、要求调用方传，可这些已知值是成员**捕获**的，应当进函数值的闭包，
和 lambda 捕获变量走同一套表示（捕获端口 + 闭包元组）。函数类型不暴露闭包内容，所以 `transpose(f)` 的类型
仍然是 `(ȳ) => (每个参数一项的余切)`；**也不需要限制成员的捕获**：成员要捕获多少已知值都行，都在闭包里。

⇒ (a) 重建类型：作者明说**不必**，且类型不该暴露闭包 ⇒ 不做。
⇒ (b) 禁止体读参与参数的值：作者明说**不需要限制成员的捕获** ⇒ 不做。
⇒ 走第三条：**成员的闭包 = `f` 的闭包，同序同类型整份透传**（`split.cpp:606-617` 的 `addResidualCaptures`：
只 `appendClosureNode`、**没有** `appendWithPort`；填充走 `GCFillKind::FunctionClosure`，`split.cpp:628-641`），
`withPorts` 归零；`f` 的闭包里没有的值 ⇒ **保持拒绝**。这一层归 `b2f` 车道（同一处代码），本车道不碰。

## 六、本车道的读数（`lane-tloop @ 719e81272`，Release，远端闸门）

| 计划 | 之前 | 之后 |
|---|---|---|
| `feat/compile/transforms` | 42 / 60 | **46 / 59**（`many_parameters` 绿、删掉 1 条不可建的读数） |
| `feat/transforms/split` | 14 / 18 | **18 / 18** |
| 全量 fast | 8526 总数 / 8483 通过 / 23 失败 | **8525 总数 / 8491 通过 / 14 失败 / 跳过 10 / known 10 / `killed` 0** |

A 族 5 条**全绿或按裁决明确拒绝**：`transpose_walks_each_arm_of_a_branch`、
`transpose_no_tangent.bool_condition`、`transpose_unread_parameters` 有数；`transpose_many_parameters`
是**钉子住的拒绝**（理由见下）；`…is_linear_in_the_cotangent` 随它删掉。另外 4 条
`split_branch_author_vjp`（nvm/fvm/jit/prl）随之全绿 —— 它要的正是"臂里那条 `mul_tangent`
部分转置能被建出来"。

**没有采用的两条路，及理由**：① 下标统一（`:742` 记 `i` → 记 `at`）单独立一次提交（`fb2ec50df`），
读数一条没动，因为它本来就同义；② 探针占位 + need 集合上送，读数完全正确但会打开第五节那个
arity 缺口（`SIGABRT`），所以整条撤回 —— 裁决是第三条（闭包透传，归 `b2f`），不是"改类型"也不是
"禁止捕获"。

## 七、`many_parameters` 两条的处置（`50ff68746`）

**`unread_parameters` 的四个数从哪来 —— 核过了，不是"把参数改成闭包变量"**：`add_tangent` 的律 `add_t`
**不读任何操作数的值**，所以成员一个值都不用捕获，签名就是 `(float) => (float, float, float, float)`，
顶层调用是**纯调用**、无捕获端口。读数（原样）：

```
TRANSPOSE function=autodiff::add_tangent<float> graph=74 type=(float) => (float, float, float, float) nodes=5 arms=0 rhs=add_t
TRANSPOSE function=at                 graph=58 type=(float) => (float, float, float, float) nodes=5 arms=0 rhs=-
stdout: (0.0, 0.0, 1.0, 1.0) / (0.0, 0.0, 2.0, 2.0)
```

唯一站不住的是**写法**：原来转置的是 `let lin = (x,w,dx,dw) => at(x,w,dx,dw)` 这个包装 lambda，
容易读成"把参数变成捕获变量才有这四个数"。改成 `transpose(at)(1.0)` —— 同一问法、同一四个数、
同一个五节点成员（实测 `graph=58` 那次），包装删掉。

**`many_parameters` —— 明确拒绝，理由**：`mul_tangent` 的律要读操作数（`mul_cotangent(w, ct)`、
`mul_cotangent_c(dx, ct)`），而它读的四个值里有两个是**切线自身的值**；转置对一个切线没有值可给
（切线的余切正是它要答的东西）。被转置的 `lin` 闭包是空的（`3.0`/`2.0` 是体内的字面量，`dx`/`dw`
是它自己的参数，不是捕获），所以在"闭包里没有 ⇒ 拒绝"这条裁决下无处可取。读数（原样）：

```
TRANSPOSE function=autodiff::mul_tangent<float> skip=cotangent-needs-a-value uri=:op/mul_d position=1 \
    reason=the rule's right side reads a value the caller does not carry
TRANSPOSE function=mt skip=call node=4 callee=autodiff::mul_tangent<float>
```

另试过 `vjp` 路线（`vjp(mt)(3,2,1,0)`），`split` 在直线提升图上先拒（`skip=exit`：exit 不是双成员元组填充），
所以"对切线自身求四元余切"在本树里**没有可写的拼法**：那条用例问的是设计已经收回的问题，
`…is_linear_in_the_cotangent` 随之删除；它独有的"一个种子分不出 pullback 与导数表"并进
`transpose_unread_parameters`（那条本来就跑 ct=1 和 ct=2 并钉两个元组）。