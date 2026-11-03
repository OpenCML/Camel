# 那 18 条按位置取值读的是"切线链自己必须打的那通电话"的一份副本

## 三个问题，各一句

1. **谁建的**：它比 `split` 早——`modules/transforms/rule_expand.cpp`（`GCNodeKind::Func` 那一支）在展开
   `stdlib/rulesets/jvp.cml#jvp_idx_arr`（`index_into(xs, i) => (dxs: T[]) => index_into(dxs, i)`）时就把它建进了
   抬升图 `jvp_of_g`；`split` 之后由 `modules/transforms/split.cpp#makeCopy` 的 `GCNodeKind::Func` 分支原样复制进后一半
   （切线半的 `modules/transforms/split.cpp#Half::retargets` 第一句就是 `if (tangent) return false;`，所以后一半的
   `Func` 只能是复制，不是重定向新建）。
2. **那一格在切点之前是谁的值**：源函数**第 `at` 个参数自己的值**——`modules/transforms/split.cpp#residualsOf`
   把参数的值端口排在残差表最前、按端口序，并写下 `Residual::parameter`；前一半把它当自己的普通端口带出来
   （`modules/transforms/split.cpp#buildSplitGraph` 的 `residualMembers` 回落到 `forward.portOf[portIndex]`），
   再作为 `lin` 闭包的第 `at` 个 `with` 输入交出去（`modules/transforms/split.cpp#buildLinearClosure`）。
   **是参数端口，不是某次计算的结果。**
3. **为什么它落在切点之后**：被 `makeCopy` 复制过去的（后一半的体只经 `copyHalf` → `makeCopy`），**但它是一次调用、
   不是函数值**：调用点第 0 个实参就是后一半自己的切线端口（读数 `arg=port0(p=0,part)`），切线在切点之前不存在，
   所以这一次调用**必须**在后一半里。

## 读数（本轮自己量的，9 形状 × 2 = 父任务那张表的 18）

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

## 结论：A 排除；机制是 B，性质是 C

- **A（`split` 新造、可挪到前一半）排除。** 后一半里的 `Func` 没有一个是函数值：9 条读数的 callee 全是规则内核
  （`rulesets.jvp::index_into<float>`、`autodiff::div_tangent<float>`），由 `modules/transforms/rule_expand.cpp`
  的 `GCNodeKind::Func` 分支带着 `tangentArgs` 建出，并由 `modules/transforms/split.cpp#makeCopy` 带着
  `source->callTangentArgsOf(ref)` 复制。**函数值只有前一半才有**：`lin` 是
  `modules/transforms/split.cpp#buildLinearClosure` 在前一半里造的 `FunctionClosure` FILL。
  所以"函数值在切点两边都不变、可以放到前一半"这句话对它不适用——它一次调用，实参是切线。
- **B 是"怎么来的"**：确实是 `makeCopy` 复制的；要改的话看它 `GCNodeKind::Func` 分支里那张把实参改写到后一半端口的表
  （`modules/transforms/split.cpp#Half::halfInputsOf` → `portOf[i]`）。
- **C 是"该不该在"**：后一半的体就是切线链（`REINTERP jvp … rules=jvp_idx_arr,jvp_mul,jvp_add`），它必须打这通电话。
  → **判决：C**（B 只是它进来的那条路）。

## 最要紧的一层：它换进来的不是实参自己的值

被转置的成员要的是 `index_into` 的**参数 0 的值**（`zeros_like` 要形状）；调用点的实参 0 是 `dxs`（切线）；
按位置取值把闭包第 0 格——`xs` 的**原值**——递了过去。`test/cases/compile/transforms/grad_array_no_int.cml`
（`both(xs) = xs[1] * xs[0]`）今天答 `6.0` / `[2.0, 3.0]` 正是靠这一手：它换成的是同一个参数的原值，不是实参的值。
这就是 14:55 要删的那个"私下约定"的全部内容——它今天站得住，靠的是**参数身份**，而代码在读取处只查了类型相等。

## 顺带量到的一处反例（身份在哪些图上不成立）

`split_of_jvp_of_g_backward` 的 loop case（`vjp_loop_rounds_walk_backwards.cml`）**0 命中**：它在走动之前就拒了
（`skip=call-value-computed-by-the-body param=0 node=23`），且那一半 `residuals=1 / invariants=0`——它不读任何参数的值，
唯一的残差是算出来的值，不是参数端口。所以"slot `at` 就是参数 `at` 的值"只在**参数前缀全进残差**的图上成立，
与 `modules/transforms/split.cpp#residualsOf` 里 `sum(x*s)` / `sum(s*x)` 那段注释同义。

## 没查 / 查不到的

- `Func` 的 `kind == 10` 我没有从探针行直接读到（`POSPROBE-CALL` 不打 kind），是从路径判定的：callee 写在节点上、
  `modules/transforms/transpose.cpp#buildTransposeMember` 走 `calleeIdOf(ref)` 且 `isCall == false`。与父读数的 kind 10 一致。
- 半路那棵插桩树被别的车道重新检出并重编（`modules/transforms/transpose.cpp` 里的 `POSPROBE` 已不在，二进制 11:36 重建），
  所以上面 46 条的日志是 11:33–11:36 那次抓到的；重跑不复现，不是探针行为变了。

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