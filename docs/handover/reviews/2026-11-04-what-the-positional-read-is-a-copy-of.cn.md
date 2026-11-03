# 证据页：那 18 条按位置取值读的是"切线链自己必须打的那通电话"的一份副本

**本页是证据页，不是发问的那一页。** 要裁的只有一句，问句、默认建议、代价与判据在
`2026-11-04-the-identity-already-exists-on-the-cut-side.cn.md`（`lane-p7page`）。本页与那一页曾经是同一句
（本页只删了一个括号），按"一页一问"合并后，本页只留证据：那 18 条读数怎么来的、换过去的是原值而实参是切线、
位置约定什么时候成立。

## 要裁的那一句（本页不重复，指回 the-identity 页）

要裁的句子原样写在 `docs/handover/questions/2026-11-04-the-identity-already-exists-on-the-cut-side.cn.md`
的 `## 要裁的那一句` 一节：

> **把 `split` 已经知道的身份（`Residual` 的 `parameter`）交给 `transpose`，算不算 2026-11-03
> 14 时 55 分否掉的那个"把位置换成字段"？**

本页不重复这一问，也不另给一个默认。**两页若有不一致，以 `the-identity` 页为准。**

## 默认建议（施工方当轮开工的那个）

与问句同页的默认一致，只写细到可施工：**按"不算乙"推进**，并在那一页给的两条写法里**取甲**——
`lin` 交出残差时把"这一格是第几个参数"一起交出去（复用既存的 `Residual::parameter`，不新建映射表、不新建字段），
`transpose` 读身份而不是数第 `at` 格。三步，全在 `split` ↔ `transpose` 的接口面内：

1. `modules/transforms/split.cpp#buildLinearClosure`：交出 `lin` 的第 `at` 个 `with` 输入时，同时交出与残差同序的
   参数序号表（`modules/transforms/split.h#Residual` 的 `parameter` 原样）。
2. 那份序号表进闭包签名，`transpose` 侧从签名读它，不再猜。
3. `modules/transforms/transpose.cpp#buildTransposeMember`：把"数第 `at` 格"改成"按参数身份取"；**查不到身份就报错，
   不许回落按位置**——没有这一步，读数三那条可判条件仍然没有执行点。

取甲不取乙，是因为乙要 `transpose` 反过来向切点索取，动的是取值方向；甲只是在既有接口上多传一份既有序号表。
若负责人选乙，回退只涉及第 2、3 步。

## 代价

**采纳（按甲改）**：动 `split`/`transpose` 之间的接口面——闭包签名多一份与残差同序的序号表，会碰 `10200dc26`
那条"闭包端口要声明"的方向；改到的文件是 `modules/transforms/split.h`、`modules/transforms/split.cpp`、
`modules/transforms/transpose.cpp` 三个，图结构不变、只变查找方式。风险面在 `lin` 闭包的消费者上：
`#buildTransposeMember` 是今天唯一读 `closurePorts` 的地方——**这一条是推断，没有逐调用点量过**，
动手前先 `git grep closurePorts` 数一遍再定改法。

**不采纳（维持按位置）**：代价不是零，是**读数三那条可判条件永远不被检查**，整组读数就停在"今天碰巧对"上：
`grad_array_no_int` 之所以答对，是因为 `index_into` 的转置成员只读形状（`zeros_like`）、两个 `float[]` 同型同形状；
换一个真要读数组内容的律，立刻是静默错数，而且**不会以红出现**——`transpose_hand_closed_lambda` 的 `__g_317__`
已经是这种静默的一例（余切落在闭包成员上、没人读；值也取到 `x`，问的是 `dx`）。**两处缺陷互相遮盖，红永远不会出现。**

## 判据 / 哨兵

三条可跑的读数，改前改后都该量：

1. **绿必须留绿**：`test/cases/compile/transforms/transpose_hand_closed_lambda.cml`，计划项
   `transpose_hand_closed_lambda.same_answer_as_the_cut`（`test/plans/feat/compile/transforms.plan.toml`）。
   它手写闭包，`split` 不参与；它变红说明改法动到了不该动的一层。
2. **唯一的正面哨兵**：`test/cases/compile/transforms/grad_array_no_int.cml`（`both(xs) = xs[1] * xs[0]`）
   今天答 `6.0` 与 `[2.0, 3.0]`。按身份取值必须给出同一对数；若改法把那一格递成切线（`dxs`）或递成空，
   这里立刻红——**本组改动唯一会以红暴露的读数就是它。**
3. **拒绝不许被抹平**：`test/cases/compile/transforms/vjp_loop_rounds_walk_backwards.cml` 的
   `split_of_jvp_of_g_backward` 今天 `skip=call-value-computed-by-the-body param=0`（读数三的反例），
   那一半 `residuals=1 / invariants=0`——`invariants=0` 正是"参数值没全进残差表"的形状。
   改法若让它变成"不报错、给一个数"，就是把第 3 步的报错改成了回落，**立刻回滚**。

**判据（一句话）**：改完之后，同一个 case 上"第 `at` 格"必须来自 `Residual::parameter`，而不是 `closurePorts`
的下标；对 `invariants=0` 的图，取值点必须**报错**而不是照旧取第 `at` 格。今天它不报错——这就是"没有判据"本身。

## 读数一：谁建的（三个问题，各一句）

1. **谁建的**：它比 `split` 早——`modules/transforms/rule_expand.cpp`（`GCNodeKind::Func` 那一支）在展开
   `stdlib/rulesets/jvp.cml#jvp_idx_arr`（`index_into(xs, i) => (dxs: T[]) => index_into(dxs, i)`）时就把它建进了
   抬升图 `jvp_of_g`；`split` 之后由 `modules/transforms/split.cpp#makeCopy` 的 `GCNodeKind::Func` 分支原样复制进后一半
   （切线半的 `modules/transforms/split.cpp#Half::retargets` 第一句就是 `if (tangent) return false;`，所以后一半的
   `Func` 只能是复制，不是重定向新建）。
2. **那一格在切点之前是谁的值**：源函数**第 `at` 个参数自己的值**——`modules/transforms/split.cpp#residualsOf`
   把参数的值端口排在残差表最前、按端口序，并写下 `Residual::parameter`（`modules/transforms/split.h#Residual`）；前一半把它当自己的普通端口带出来
   （`modules/transforms/split.cpp#buildSplitGraph` 的 `residualMembers` 回落到 `forward.portOf[portIndex]`），
   再作为 `lin` 闭包的第 `at` 个 `with` 输入交出去（`modules/transforms/split.cpp#buildLinearClosure`）。
   **是参数端口，不是某次计算的结果。**
3. **为什么它落在切点之后**：被 `makeCopy` 复制过去的（后一半的体只经 `copyHalf` → `makeCopy`），**但它是一次调用、
   不是函数值**：调用点第 0 个实参就是后一半自己的切线端口（读数 `arg=port0(p=0,part)`），切线在切点之前不存在，
   所以这一次调用**必须**在后一半里。

## 读数一的凭据（本轮自己量的，9 形状 × 2 = 父任务那张表的 18）

在 88 上按单 case 顺序跑（**低并行**：一次一个进程，没有并发；没走 `camel-remote.ps1`，用 develop@`c69a110fa`
上一棵已构建、带 `POSPROBE` 插桩的树跑 `test/cases/compile/transforms/*.cml` 各一次、`std::nvm`）。
96 个 case 上 `POSPROBE-HIT` 共 **46** 条，按 `from=` 只有两种来源：

```
from=call-capture   9   ← 父任务那 18 条（计划把每个 case 跑两遍，9 × 2）
from=-             37   ← 律展开自己问的（ask=rule:sum_t|node:8|pos:0, target=oper:tensor:add）
```

`call-capture` 的 9 条，全部同一个形状（`i=0`、`valueSlot=0`、`slotName=closure<at>`）：

| 函数（后一半） | 调用点的 callee | `at` | `params` | `closure` |
| --- | --- | --- | --- | --- |
| `split_of_jvp_of_g_backward`（×5，两个 case，`closure` 2 与 5） | `rulesets.jvp::index_into<float>` | 0 | 1 | 2 / 5 |
| `split_of_jvp_of_read_at_backward` | `rulesets.jvp::index_into<float>` | 1 | 2 | 2 |
| `split_of_jvp_of_mixed_backward` | `rulesets.jvp::index_into<float>` | 0 | 2 | 5 |
| `split_of_jvp_of_both_backward` | `rulesets.jvp::index_into<float>` | 0 | 1 | 5 |
| `split_of_jvp_of___g_325___backward` | `autodiff::div_tangent<float>` | 0 | 1 | 3 |

`POSPROBE-CALL` 把"是谁在问"钉死到调用点（原样三条）：

```
POSPROBE-CALL fn=split_of_jvp_of_g_backward    callee=rulesets.jvp::index_into<float> node=4 idx=0 argIndex=0 arg=port0(p=0,part)
POSPROBE-CALL fn=split_of_jvp_of_read_at_backward callee=rulesets.jvp::index_into<float> node=3 idx=0 argIndex=0 arg=port0(p=1,part)
POSPROBE-CALL fn=split_of_jvp_of___g_325___backward callee=autodiff::div_tangent<float> node=6 idx=3 argIndex=3 arg=port0(p=0,part)
```

`idx` 是 `modules/transforms/transpose.cpp#buildTransposeMember`（call 分支那句
`for (const size_t idx : state.calleeNeeds[callee])`）里被转置成员索要的那一格，`arg=port0(p=…,part)` 说明调用点交给它的
实参**是后一半自己的参与端口（切线）**，不是值。

## 读数一（续）：A 排除；机制是 B，性质是 C

- **A（`split` 新造、可挪到前一半）排除，理由就是一句：它带 `tangentArgs`，调用点第 0 个实参是后一半自己的
  切线端口，而切线在切点之前不存在。** 后一半里的 `Func` 没有一个是函数值：9 条读数的 callee 全是规则内核
  （`rulesets.jvp::index_into<float>`、`autodiff::div_tangent<float>`），由 `modules/transforms/rule_expand.cpp`
  的 `GCNodeKind::Func` 分支带着 `tangentArgs` 建出，并由 `modules/transforms/split.cpp#makeCopy` 带着
  `source->callTangentArgsOf(ref)` 复制；后一半的 `Func` 只可能是复制，因为
  `modules/transforms/split.cpp#Half::retargets` 开头就是 `if (tangent) return false;`。
  **函数值只有前一半才有**：`lin` 是 `modules/transforms/split.cpp#buildLinearClosure` 在前一半里造的
  `FunctionClosure` FILL。所以"函数值在切点两边都不变、可以放到前一半"这句话对它不适用——它一次调用，实参是切线。
- **B 是"怎么来的"**：确实是 `makeCopy` 复制的；要改的话看它 `GCNodeKind::Func` 分支里那张把实参改写到后一半端口的表
  （`modules/transforms/split.cpp#Half::halfInputsOf` → `portOf[i]`）。
- **C 是"该不该在"**：后一半的体就是切线链（`REINTERP jvp … rules=jvp_idx_arr,jvp_mul,jvp_add`），它必须打这通电话。
  → **判决：C**（B 只是它进来的那条路）。

## 读数二：它换过去的是"原值"，不是"实参的值"

被转置的成员要的是 `index_into` 的**参数 0 的值**（`zeros_like` 要形状）；调用点的实参 0 是 `dxs`（切线）；
按位置取值把闭包第 0 格——`xs` 的**原值**——递了过去。`test/cases/compile/transforms/grad_array_no_int.cml`
（`both(xs) = xs[1] * xs[0]`）今天答 `6.0` / `[2.0, 3.0]` **正靠这一手**：它换成的是同一个参数的原值，不是实参的值。
**这是"这段代码为什么有存在的必要"的唯一实证**——不是"余切需要它"，而是"转置图重建一次调用时，要交出一个真的原值"。
它今天站得住，靠的是**参数身份**，而代码在读取处只查了类型相等。

## 读数三：位置约定成立的条件，是可判的一句

`split_of_jvp_of_g_backward` 的 loop case（`test/cases/compile/transforms/vjp_loop_rounds_walk_backwards.cml`）
**0 命中**：它在走动之前就拒了（`skip=call-value-computed-by-the-body param=0`），且那一半
`residuals=1 / invariants=0`——它不读任何参数的值，唯一的残差是**算出来的值**，不是参数端口。

**可判的句子**：位置约定（"slot `at` 就是参数 `at` 的值"）成立的充要条件是
**参数 0 … `at` 的值全部进了残差表**（`modules/transforms/split.cpp#residualsOf` 把参数值端口排在最前、按端口序，
所以它们恰好占住 `0 … 前缀长度-1`）；一旦某个参数的值没进残差表——比如 `invariants=0` 的图，或
`sum(x*s)` / `sum(s*x)` 那种同一值出现两次、副本被排到后面的图——`at` 就落在别人的格上。
代码在读取处只比对类型，而两个 `float[]` 的残差型别相同，**这条条件今天没有检查**。

## 没查 / 查不到的

- **`Func` 的 `kind == 10` 是推断，不是读数。** `POSPROBE-CALL` 不打 kind；这一条是从路径判定的：callee 写在节点上、
  `modules/transforms/transpose.cpp#buildTransposeMember` 走 `calleeIdOf(ref)` 且 `isCall == false`。
  它与父读数的 kind 10 一致，但**一致不等于量到**，记在这里以免下一个人把它当读数用。
- **这组数不可复现，原因是插桩树被复用。** 半路那棵插桩树被别的车道重新检出并重编
  （`modules/transforms/transpose.cpp` 里的 `POSPROBE` 已不在源码，二进制 11 时 36 分重建），所以上面 46 条的日志是
  11 时 33 分–11 时 36 分那次抓到的，重跑不复现——不是探针行为变了。**纪律：插桩树要独占，别在别人的工作树上插桩**；
  插桩期间那棵树不能被 `camel-remote.ps1` 碰到（它会 `checkout -f` 再叠补丁，插桩就没了）。

## 取证位置

- 调用点/捕获端口：`modules/transforms/transpose.cpp#buildTransposeMember`
- 后一半的体：`modules/transforms/split.cpp#copyHalf`、`modules/transforms/split.cpp#makeCopy`
- 切线半不重定向：`modules/transforms/split.cpp#Half::retargets`
- 残差的编号与顺序：`modules/transforms/split.cpp#residualsOf`
- 前一半的端口与 `lin`：`modules/transforms/split.cpp#buildSplitGraph`、`modules/transforms/split.cpp#buildLinearClosure`
- 闭包端口：`modules/transforms/split.cpp#addResidualCaptures`
- 规则内核调用的制造者：`modules/transforms/rule_expand.cpp`（`GCNodeKind::Func` 分支）
- 规则：`stdlib/rulesets/jvp.cml#jvp_idx_arr`
- 靠这一手才成立的用例：`test/cases/compile/transforms/grad_array_no_int.cml`