# C4：轮数由数据决定的 `while`，两条导数路径与闭式解

**状态：初读，非定稿（preliminary, not final）。** 本页是**一次**运行、一个程序族的读数。结论分两半：
`with_grad` 这一半成立且逐位精确；`vjp` 这一半**没做到**，卡住的位置与原话在 §5。

- **尖端**：车道工作树 `D:\Projects\Camel-wt\misc`，detached HEAD `b25dcda7a2b3`（主机检出
  `/home/zjwei/camel-work/misc`）。本轮没有改任何被跟踪文件；跑时工作树有 4 个**未跟踪**的探针文件，
  所以 `build-info.json` 的 `dirty` 是 4。
- **机器**：主机 `zjwei`（Linux，令牌池 24）。本次运行的
  `[provenance] tip (dirty: 4 files) | binary built 2026-11-28T15:10:33+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 2`。
- **负载**：跑前 `[load] 65.69 59.26 45.59 (1m/5m/15m)`，跑后 `[load] 61.25 58.96 46.06`，池子
  `pool: size=24 inUse=15`。主机很忙；本页的比较都是**浮点数的逐位相等**，与墙钟无关。
- **日期**：2026-11-28。

## §1 程序与闭式解

C4 的主案例是"转置循环"：`while` 的轮数由数据决定，条件读的是状态本身。这里用最小的形状复现那个
性质——循环何时停，取决于只有这些轮次才产出的值：

```camel
// 主案例形状（two_states）：每轮把 s 加进 acc，再把 s 折半；s 落到 1 以下就停。
let (acc, s) = loop acc = 0.0, s = x while s > 1.0 do (acc + s, s / 2.0)
return acc + s
```

取 `x = 12`：轮子是 **12 → 6 → 3 → 1.5**，然后 `0.75 ≤ 1` 停下，共 **4 轮**。所以

```
f(12) = 12 + 6 + 3 + 1.5 + 0.75 = 23.25
```

在这段区间上 `f` 是线性的：`f(x) = x · (1 + 1/2 + 1/4 + 1/8 + 1/16) = x · 31/16`，因此

**闭式解 `f'(12) = 31/16 = 1.9375`**。这四个加数都是 2 的幂，`31/16` 在二进制浮点里**精确**，
所以下面比的是"是否逐位相等"，不是"是否在容差内"。

另有两个形状与一个控制组一起读（同一份驱动程序里生成）：

| 形状 | 循环 | f(12) | f'(12) |
|---|---|---|---|
| `one_state` | `loop s = x while s > 1.0 do s / 2.0`，返回 `s` | 0.75 | 1/16 = 0.0625 |
| `two_states` | 上面那个累加形状（主案例） | 23.25 | 31/16 = 1.9375 |
| `three_states` | 同上再加一个只用于计数的 `k`，循环带三个状态 | 23.25 | 31/16 = 1.9375 |
| `for_control` | `loop acc = 0.0 for s in [1,2,3,4,5] do acc + s * x`，返回 `acc` | 180.0 | 15.0 |

`for_control` 是**控制组**，不是 C4 的形状：它的轮数写在字面量里，不是数据决定的；它存在的用处是——
如果连它都读不出导数，那说明坏的是我的读数手法，而不是循环。

四条路：`plain`（只读值，`std::nvm`）、`with_grad`（引擎的，`autodiff`）、`vjp`（库的，
`stdlib/vjp.cml`，即 `transpose(split(jvp(f)))`）、`transpose`（只读转置那一半，不经 `jvp`/`split`）。
每个格子跑**独立进程**，一条路拒绝不会遮住别的路。

## §2 读数表

| 形状 | `plain` 值 | `with_grad` 导数 | `vjp` 导数 | `transpose` 导数 |
|---|---|---|---|---|
| `one_state` | 0.75 | **0.0625**（= 1/16，逐位） | 拒绝（见 §5） | 拒绝（见 §5） |
| `two_states`（主案例） | 23.25 | **1.9375**（= 31/16，逐位） | 拒绝（见 §5） | 拒绝（见 §5） |
| `three_states` | 23.25 | **1.9375**（= 31/16，逐位） | 拒绝（见 §5） | 拒绝（见 §5） |
| `for_control`（控制组） | 180.0 | **15.0**（逐位） | **15.0**（逐位） | 拒绝 |

`with_grad` 三个 while 形状全部**逐位等于闭式解**（`rel_err=0.000e+00`，不是"小于 1e-9"）。
`vjp` 在三个 while 形状上全部拒绝、在控制组上给出 15.0（逐位）——所以拒绝来自循环的形状，不是读数手法。

## §3 判词原文（驱动程序 stdout，逐字）

```
c4.x=12.0 repo_root=/home/zjwei/camel-work/misc
c4.shape=one_state closed_value=0.75 closed_derivative=0.0625 rounds=four rounds: 12, 6, 3, 1.5, then 0.75 stops it
c4.cell shape=one_state route=plain exit=0 value_raw='0.75' d_raw=None
c4.cell shape=one_state route=vjp exit=1 REFUSED
c4.cell shape=one_state route=vjp said: ():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of_f' has LOOP node 12: state 0 is a 'float' in the condition and a '(float, float)' in the body. The pass gave up without a diagnostic of its own: it built or was given a graph it cannot handle, which is a defect of the compiler rather than of the program. Try the pipeline without this pass or with the passes in another order, and report the program together with the pass list. (name=RuntimeDiag::PassInternalError, code=0x0400000C)
c4.cell shape=one_state route=with_grad exit=0 value_raw='0.75' d_raw='0.0625'
c4.cell shape=one_state route=transpose exit=1 REFUSED
c4.cell shape=one_state route=transpose said: TRANSPOSE function=f skip=state node=7 origin=0 why=the-loop-carries-no-state
c4.cell shape=one_state route=transpose said: (c4_one_state_transpose)/tmp/c4-while-cwcmhx4f/c4_one_state_transpose.cml:13:16: [Error]: An error has occurred: transforms: transpose has no transpose for this function: a loop carries its state out of itself, and that shape has no transpose. The runtime has no diagnostic of its own for this fault: every fault falls back to this code, so the message is all there is. (name=RuntimeDiag::RuntimeError, code=0x04000000)
c4.shape=two_states closed_value=23.25 closed_derivative=1.9375 rounds=four rounds, accumulating each one; f is x * 31/16 on that interval
c4.cell shape=two_states route=plain exit=0 value_raw='23.25' d_raw=None
c4.cell shape=two_states route=vjp exit=1 REFUSED
c4.cell shape=two_states route=vjp said: ():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of_f' has LOOP node 23: state 0 is a 'float' in the condition and a '(float, float)' in the body. The pass gave up without a diagnostic of its own: it built or was given a graph it cannot handle, which is a defect of the compiler rather than of the program. Try the pipeline without this pass or with the passes in another order, and report the program together with the pass list. (name=RuntimeDiag::PassInternalError, code=0x0400000C)
c4.cell shape=two_states route=with_grad exit=0 value_raw='23.25' d_raw='1.9375'
c4.cell shape=two_states route=transpose exit=1 REFUSED
c4.cell shape=two_states route=transpose said: TRANSPOSE function=f skip=state node=13 origin=0 why=the-loop-carries-no-state
c4.cell shape=two_states route=transpose said: (c4_two_states_transpose)/tmp/c4-while-cwcmhx4f/c4_two_states_transpose.cml:13:16: [Error]: An error has occurred: transforms: transpose has no transpose for this function: a loop carries its state out of itself, and that shape has no transpose. The runtime has no diagnostic of its own for this fault: every fault falls back to this code, so the message is all there is. (name=RuntimeDiag::RuntimeError, code=0x04000000)
c4.shape=three_states closed_value=23.25 closed_derivative=1.9375 rounds=the same plus an unused round counter k, so the loop carries three states
c4.cell shape=three_states route=plain exit=0 value_raw='23.25' d_raw=None
c4.cell shape=three_states route=vjp exit=1 REFUSED
c4.cell shape=three_states route=vjp said: ():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of_f' has LOOP node 31: state 0 is a 'float' in the condition and a '(float, float)' in the body. The pass gave up without a diagnostic of its own: it built or was given a graph it cannot handle, which is a defect of the compiler rather than of the program. Try the pipeline without this pass or with the passes in another order, and report the program together with the pass list. (name=RuntimeDiag::PassInternalError, code=0x0400000C)
c4.cell shape=three_states route=with_grad exit=0 value_raw='23.25' d_raw='1.9375'
c4.cell shape=three_states route=transpose exit=1 REFUSED
c4.cell shape=three_states route=transpose said: TRANSPOSE function=f skip=state node=18 origin=0 why=this-version-counts-the-rounds-of-a-loop-by-pulling-its-control
c4.cell shape=three_states route=transpose said: (c4_three_states_transpose)/tmp/c4-while-cwcmhx4f/c4_three_states_transpose.cml:13:16: [Error]: An error has occurred: transforms: transpose has no transpose for this function: a loop carries its state out of itself, and that shape has no transpose. The runtime has no diagnostic of its own for this fault: every fault falls back to this code, so the message is all there is. (name=RuntimeDiag::RuntimeError, code=0x04000000)
c4.shape=for_control closed_value=180.0 closed_derivative=15.0 rounds=five rounds, written as a list: the round count is not data-decided (x does appear, so 15x)
c4.cell shape=for_control route=plain exit=0 value_raw='180.0' d_raw=None
c4.cell shape=for_control route=vjp exit=0 value_raw='180.0' d_raw='15.0'
c4.cell shape=for_control route=with_grad exit=0 value_raw='180.0' d_raw='15.0'
c4.cell shape=for_control route=transpose exit=1 REFUSED
c4.cell shape=for_control route=transpose said: TRANSPOSE function=f skip=law-reads-a-value uri=:op/mul_d position=0 operand-types=float,float reason=the rule's right side reads a value the caller does not carry
c4.cell shape=for_control route=transpose said: TRANSPOSE function=f skip=state node=7 origin=0
c4.cell shape=for_control route=transpose said: (c4_for_control_transpose)/tmp/c4-while-cwcmhx4f/c4_for_control_transpose.cml:13:16: [Error]: An error has occurred: transforms: transpose has no transpose for this function: a loop carries its state out of itself, and that shape has no transpose. The runtime has no diagnostic of its own for this fault: every fault falls back to this code, so the message is all there is. (name=RuntimeDiag::RuntimeError, code=0x04000000)
c4.check shape=one_state route=with_grad d=0.0625 closed=0.0625 rel_err=0.000e+00
c4.check shape=two_states route=with_grad d=1.9375 closed=1.9375 rel_err=0.000e+00
c4.check shape=three_states route=with_grad d=1.9375 closed=1.9375 rel_err=0.000e+00
c4.check shape=for_control route=vjp d=15.0 closed=15.0 rel_err=0.000e+00
c4.check shape=for_control route=with_grad d=15.0 closed=15.0 rel_err=0.000e+00
c4.verdict: cells=16 derivatives_compared=5 mismatched=0 plain_ok=True
```

运行器那一侧：

```
[PLAN] probe.c4-while-two-routes: probe.c4_while_two_routes
[PASS] probe.c4_while_two_routes                               1.68 s
[SUMMARY] total: 1, pass: 1, fail: 0
```

## §4 这一页成立到哪一步

**成立**：`with_grad` 在三个"轮数由数据决定"的 while 形状上给出的导数，与手算闭式解**逐位相等**
（0.0625 / 1.9375 / 1.9375，`rel_err=0.000e+00`），值也对（0.75 / 23.25 / 23.25）。

**没做到**：格 7 要求的那句"经 `vjp` 与 `with_grad` 两条路**数值一致**"——**一半没做到**。`vjp` 在
三个 while 形状上全部拒绝，且拒绝发生在 `jvp` 这一半，转置那一半还没轮到。

## §5 卡在哪（原话 + 读法，不替编译器下结论）

`vjp` 的拒绝原文（三个形状只有节点号不同：12 / 23 / 31）：

```
Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of_f' has LOOP node 12:
state 0 is a 'float' in the condition and a '(float, float)' in the body. The pass gave up
without a diagnostic of its own: it built or was given a graph it cannot handle, which is a
defect of the compiler rather than of the program.
(name=RuntimeDiag::PassInternalError, code=0x0400000C)
```

- **是编译器自称缺陷**，不是我给它定性：`which is a defect of the compiler rather than of the program`
  是这条诊断自己的措辞，`PassInternalError` 是它自己的名字。
- **形状**：`jvp` 之后的循环节点里，条件读的是 `float`（值），循环体产出的是 `(float, float)`
  （值 + 切向量）——两者对不上。也就是说 `jvp` 的循环下降把每个状态配成了对，而条件仍然读裸值。
- **不是"端口不够"**：一个状态、两个状态、三个状态的 while 都以同一句话被拒（只是节点号不同），
  所以不是"端口太少装不下切向量"这一类问题。
- **不是测量手法**：同一驱动程序、同一二进制下，`for_control`（轮数写死的 for）走 `vjp` 得到 15.0，
  逐位等于闭式解；`plain` 四个形状都跑通。
- **`transpose` 单独读也不是同一条墙**：它拒的是"循环把自己的状态带出去"这个形状
  （`why=the-loop-carries-no-state` / `why=this-version-counts-the-rounds-of-a-loop-by-pulling-its-control`），
  且 `rulesets.transpose` 的规则族是**已导入**的（第一次跑时忘了导入，运行时明确报
  `no module named 'rulesets.transpose' was built into this program`；补上导入后才是上面这些状态类拒绝）。
  仓库里 `test/cases/compile/transforms/transpose_loop_condition_reads_state.cml` 就是给这个形状写的
  案例；本页用的程序**不是**它的逐字形状，所以"它拒了我这三个形状"不等于"它拒了那个案例"。

**所以 C4 的欠账位置**：`vjp` 的问题是 `jvp`（前向半步）不能读这个 while，而不是转置（反向半步）。
把 `jvp` 这一处修好后，同一条探针一行不用改；届时判词行里的 `derivatives_compared` 会从 5 变成 8
（三个 while 形状各多一条 `vjp`），`mismatched=0` 仍然要成立。

## §6 怎么复算

探针当时是两个未跟踪文件：`test/plans/probe/c4-while-two-routes.plan.toml`（一个条目，
`tool = "python"`，`expect_stdout_contains = ["mismatched=0 plain_ok=True"]`）与
`test/plans/probe/c4-while-two-routes.py`（驱动：把四个形状的四条路写成程序文本落到临时目录，逐格跑
`out/latest/bin/camel <程序> std::nvm`，从 `c4.value_raw=` / `c4.d_raw=` 两行读回数值，与闭式解比
`rel_err`，最后打判词行）。**交回时已从工作树删除**（`test/plans/probe/` 是二十来条车道共用的目录，
留着会让别人误把它当目录靶子跑），原件归档在 `D:\Temp\misc-c4-and-figure1-probes\`。

页里不缺可复算的东西：§1 的四个程序文本、§3 的每格原话、§5 的闭式解推导都齐了；驱动没有别的花招，
照 §1 的程序跑一次、把 `c4.d_raw` 与闭式解比就得到同一张表。§3 那 39 行取自运行日志
（`test/results/fetched/pass/probe.c4_while_two_routes/probe.c4_while_two_routes/stdout.txt`，
`.gitignore` 覆盖）。

命令（本轮实际用的那一行）：

```powershell
& "D:\Projects\Camel-wt\misc\internal\tools\camel-remote.ps1" `
  -Path "D:\Projects\Camel-wt\misc" `
  -Plan "test/plans/probe/c4-while-two-routes.plan.toml" `
  -FetchFile "pass/probe.c4_while_two_routes/probe.c4_while_two_routes/stdout.txt" `
  -FromThisRun -Jobs 2 -QueueOnTreeLock
```

`-FetchFile` 把条目自己的 stdout 取回 `test/results/fetched/…`（`.gitignore` 覆盖），§3 的 39 行就是
那个文件。

不想用探针时，最小的一步是（主案例形状）：

```camel
import { with_grad } from autodiff

with <x: float>
func f() => float {
    let (acc, s) = loop acc = 0.0, s = x while s > 1.0 do (acc + s, s / 2.0)
    return acc + s
}

func main() sync {
    let (value, gradient) = with_grad(f)<12.0>()
    println('value={} gradient={}'->format<value>->format<gradient>)
}
```

`camel 那个文件.cml std::nvm` 应印 `value=23.25 gradient=1.9375`。

## §7 没回答的

- 没测 `tanh`/残差那类"条件读非线性状态"的形状（backlog 里 CC 主案例最终用的是 `tanh`），本页只测了
  折半累加这一族；`vjp` 的墙是否与"条件读什么"有关，本页不能判定——它在 `jvp` 的**节点类型**上就拒了。
- 没测 `split`/磁带长度（`ReverseRoundCount::Source::TapeLength`）那一路，因为 `vjp` 在 `jvp` 就停了。
- 没测 `std::fvm`/`std::jit` 后端（都是 `std::nvm`）。
- 没测控制组之外"轮数由数据决定"的更大循环（上界更多轮、`s` 折半以外的收缩方式）。