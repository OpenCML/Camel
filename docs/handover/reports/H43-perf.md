# H43（第 7 项）：循环性能

分支：`handover/H43-region-perf`，工作树 `D:\Projects\Camel-wt\h43perf`，从
`handover/H43b-regions-cleanup`（`fc62829f`）切出。本报告只涉及第 7 项（性能目标）。

结论：**目标未达到，但差距的来源已经量化清楚**。一千万轮（10 000 000 轮）两状态 `while`，
每一行是同一台机器上的一批（批内交替跑 3 到 5 次取中位数；`loadavg` 一样本列，最后一行是本次
提交的构建）：

| 改动 | `loadavg` | `std::nvm` | `std::fvm` | `std::jit` | 手写尾递归（`std::fvm`） |
|---|---|---|---|---|---|
| 目标 | — | ≤ 0.70 s | ≤ 0.245 s | ≤ 0.245 s | — |
| 改动前 | 28–137 | 2.337 s | 1.255 s | 1.216 s | 0.491 s |
| 状态保留分量（第 3 节） | 27.8 | 1.630 s | 0.595 s | 0.598 s | 0.507 s |
| 再把 `%` 变成指令（第 4 节） | 50.6 | 1.564 s | 0.426 s | 0.427 s | 0.313 s |
| **最终（本次提交的构建）** | 26.2 | **1.571 s** | **0.436 s** | **0.435 s** | **0.310 s** |
| 距离目标 | — | 2.24 倍 | 1.78 倍 | 1.78 倍 | — |

同一台机器上，尾递归那一列在各批之间只动了 2%（0.507 → 0.310 是取余变成指令带来的，
0.313 → 0.310 是漂移），而循环那三列动了 2.5 倍以上，所以趋势不是负载造成的；批次之间的绝对
值有 3% 到 7% 的抖动（最后一行比上一行多 3% 到 4%，那一批的负载是更重的 50.6），下面的比较
都在批内做。

两个改动把循环的一轮从“造元组 + 算子调用 + 循环事件”压到“组件直传 + 一条取余指令 + 循环
事件”：`std::fvm` 快了 2.88 倍，`std::nvm` 快了 1.49 倍，`std::jit` 快了 2.80 倍。剩下的是
解释器的**每字节码开销**与 **NodeVM 的每节点开销**，不是循环机制本身能再挤出来的部分；
`std::jit` 的差距来自“含循环的图还不编译”（第 2 项）。证据在下面第 2、5、6 节。

## 1. 测法

- 机器：`88zjwei`，32 核 EPYC 7343，共享、长期有其他构建与训练任务在跑（本报告所有数字的
  load average 在 16 到 137 之间，逐条标注）。工作机（Windows）在满载时蓝屏，构建与测量都在
  这台 Linux 机器上做。
- 程序：`docs/design/loops/examples/bench_loop.cml`
  （`let r = loop s = 0, i = 0 while i < n do (s + i % 7, i + 1)`，n = 10 000 000），
  基线是同样工作量的手写尾递归 `docs/design/loops/examples/bench_tail.cml`。
- 时间：整个进程的 CPU 时间（user + sys），由父进程用 `resource.getrusage(RUSAGE_CHILDREN)`
  取差值，进程用 `taskset -c 3` 绑核。墙钟在这个负载下不可信（同一进程可以慢 1.4 倍），
  CPU 时间在 load average 137 时仍稳定在 ±0.01 s。每份 `(程序, 后端)` 交替跑 3 到 5 次，
  取中位数，同时在跑的一批里放一个不变的基线（尾递归）用来核对漂移。
- 环境：`perf` 不可用（`perf_event_paranoid = 4`），没有 valgrind。因此“时间去哪了”不是采样
  得到的，而是**同一份构建里的减法**：把被测程序裁成只差一个因素的几个版本，同批交替跑，
  差值就是那个因素的价格（第 2 节）。这也顺手避免了跨批次漂移：同一批里的尾递归基线变化在
  3% 以内，而下面要对比的数字变化的都是几十个百分点。

## 2. 时间去哪了（改动前）

两状态 `while`、一千万轮，ns/轮（同批交替，5 次中位数）：

| 一轮做什么 | `std::nvm` | `std::fvm` |
|---|---|---|
| 两状态 + `i % 7`（改动前） | 233.7 | 125.5 |
| 其中：体内的 `FILL` 造下一轮的元组 | 114 | 61 |
| 其中：`:op/mod_l` 走算子表调用 | 30 | 30 |
| 其中：循环本身（事件、端口、计数器、条件、加法） | 90 | 34 |

也就是说：**接近一半的时间（两台都是 49%）是“每轮造一个没人读的元组”**；取余两台都是
30 ns（`std::fvm` 占 24%，`std::nvm` 占 13%）；循环机制占 39%（nvm）与 27%（fvm）。
这个 `FILL` 是 `loop s = 0, i = 0 while … do (a, b)` 这种“多值状态”的直接后果：体的结果是
`(next_s, next_i)` 元组，计划里 `FILL` 造出来，下一轮的 `LTST` 再按组件拆开绑给条件的端口。

`std::jit` 与 `std::fvm` 的数字完全一致（1.216 对 1.255），因为含循环的图今天不进 JIT
（第 2 项），`std::jit` 只是用编译后的被调函数跑解释器。

## 3. 改动一：多值状态不再每轮造元组

`ExecPlan` 在 `buildLoopTables` 之后多一步 `planStateComponents`（`src/execute/exec_plan.cpp`）：
**如果能证明这个元组只是转运工具，就不造它**。证明的条件（全部满足才认）：

- 循环不收集（`collectionCount == 0`），状态至少两值（`stateCount >= 2`）；
- 体的结果（穿过 GATE 之后）是 `GCNodeKind::Fill`，其类型是 k 元组，各分量类型恰是
  `loop.valueType->typeAt(j)`；
- `FILL` 的模板输入是 `DATA` 常量，`fillBody->slots()` 恰好把 k 个分量各写一次；
- `FILL` 的每个 with 输入都是**体内**的普通节点（不是端口、不是捕获），且各占一个自己的槽；
- 只有 `LOOP` 节点读这个元组（`readOnlyBy`，扫一遍全图的输入）。

成立时计划记下 `ExecLoop::stateComponents = true`、`stateSlots`（k 个分量槽）与
`ExecPlan::elidedAt[fillTopo] = 1`。之后：

- `loopBegin` 照旧用 `startLoopState` 造出**本循环的值**（那个元组）放进状态槽——它是
  `LOOP` 节点的值，也是收集器要跟踪的东西——再把各分量从元组抄进 `stateSlots`；
- `loopTest` 直接把 `stateSlots` 里的分量绑给条件的端口；
- `loopEnd` 不再 `takeLoopRound`（不再拆元组、不再造元组）；循环停下时把 `stateSlots`
  里的分量写回那个元组（一次，而不是每轮）；
- NodeVM 与 FastVM 都不执行被省掉的 `FILL`（FastVM 干脆不发射它，JIT 也就看不到它）；
  其余消费者（macro、`std::prl`、调试器）照旧执行它，结果一样，只是白花那点时间。

条件不成立时计划什么都不说，所有后端走原来的路。这条“能省就省、否则原样”的写法是刻意的：
一个由端口直传的状态（`do (s, i + 1)`）、一个被别处读到的元组、一个带语句（GATE）的体，
都仍然每轮造元组，行为与改动前逐位相同。

## 4. 改动二：`%` 成为一条指令（`IMOD` / `LMOD`）

`%` 是这个形状的循环里每轮都有的算子，而它是**唯一**还走算子表调用的算术算子：`+`、`-`、`*`、
`/`、比较都在 `getSupportedInlineOperatorsMap()` 里，`%` 不在。一次算子表调用（`FrameArgsView`
两次、`FunctionCallScope`、间接调用）在三个后端里都值 30 ns 左右，比一次 `idiv` 贵得多。

于是按已有惯例补上两条字节码：`IMOD`（Int32）、`LMOD`（Int64），语义与 `camel/utils/int_div.h`
的 `remainder` 一致（`x % -1 == 0`，除数为 0 是 `RuntimeDiag::DivisionByZero`），
在两台解释器（`gotovm.cpp` 的 `DEF_BIN_MOD_LABEL`、`casevm.cpp` 的 `DEF_BIN_MOD_CASE`）与
`bytecode.cpp` 的名字表里各一行。

JIT 那边不硬扛：`IMOD`/`LMOD` 交给 `jitHelperBytecode`（和 `CAST`、`COPY`、`FILL`、`ACCS`
同一条路），并在 `collectRuntimeConstructs` 里登记，使含 `%` 的图按 framed 代码编译——
第一版忘了这一步，图被当成 frameless，helper 拿不到上下文，`spin` 算出 1 而不是 299995；
这个错误是被 `bench_tail.cml` 在 `std::jit` 下的结果抓到的，报告里留一笔。

## 5. 改动后的一轮，与“同一件事编译起来有多快”

同一批交替，5 次中位数，ns/轮（这一批的 `loadavg` 是 16 到 20，比最终那批轻，所以比上表的
最终行小 3% 到 6%；表内各行是同一批，互相可比）：

| 一轮做什么 | `std::nvm` | `std::fvm` |
|---|---|---|
| 一状态、无 `%`（4 条字节码 / 4 个节点） | 91.8 | 33.4 |
| 两状态、无 `%` | 121.8 | 37.9 |
| 两状态、`i % 7`（就是 `bench_loop.cml`） | 152.3 | 40.9 |
| 同样的工作量写成尾递归，解释执行 | 155.7 | 30.4 |
| 同样的工作量写成尾递归，`std::jit` 编译后 | — | 7.8 |

- `std::fvm`：6 条字节码一轮，40.9 ns，即**每条约 6.8 ns**（派发 + 处理器）。取余从 30 ns
  降到 3 ns（`LMOD` 只比 `LADD` 贵一点点），第二个状态分量值 4.5 ns（一条 `LADD` 加一次绑定）。
  循环的两个事件（`LTST`、`LEND`）比普通字节码贵一些（各 k 次端口拷贝），但没有到主导地位。
- `std::nvm`：每节点 23 ns 左右，一轮六七个节点就是一轮的全部。第二个分量值 30 ns（一个
  `LADD` 加一次绑定），`%` 仍然值 30 ns（NodeVM 不走字节码，`LMOD` 帮不到它）。
- `std::jit`：7.8 ns 一轮——**同一件事编译起来比解释快 4 倍**。循环到不了这个数，只因为
  含循环的图今天不编译。

## 6. 剩下的差距与原因

最终那批（`loadavg` 26.2，本次提交的构建）是 157.1 ns/轮（`std::nvm`）、43.6（`std::fvm`）、
43.5（`std::jit`），尾递归 31.0（`std::fvm`）与 8.7（`std::jit`）。按这些数：

- **`std::nvm`：157 ns/轮，目标 70。** NodeVM 是一个节点一个节点执行的参照实现，每节点 23 ns，
  其中相当一部分是“取计划里的槽号、走 `Frame::get/set`、`stepLoops`、观察者检查、switch”。
  要 70 ns/轮就得把每节点压到 10 ns 以下，那不是修剪能得到的，是把执行器换掉——FastVM 就是
  那个执行器（同一程序 43.6 ns/轮）。所以 `std::nvm` 的目标我不认为能用“循环更瘦”达成；
  它是参照实现的固有价格。
- **`std::fvm`：43.6 ns/轮，目标 24.5（= 文档里 0.49 s 的一半）。** 一轮六条字节码，每条约
  7 ns，这是这台机器上解释器的派发价格（同一形状的尾递归是 8 条字节码 31 ns，即 3.9 ns/条，
  它的字节码更“平”，没有两个事件的端口拷贝）。要 24.5 ns，得让一轮少走几次派发：编译循环
  （第 2 项），或者一条**超级指令**把“条件 + `LTST` + 体 + `LEND`”整轮在一个处理器里跑完。
  另外要注意：文档里“手写尾递归 0.49 s”这个基线本身也变了——取余变成指令后，同一个尾递归
  现在是 0.310 s（`std::jit` 0.087 s），按“今天的一半”算该是 0.155 s。本报告仍按文档写死的
  0.245 s 比较。
- **`std::jit`：43.5 ns/轮，目标 24.5。** 原因单一：含循环的图不编译（第 2 项，属于别的条目）。
  同一份工作负载的尾递归编译后是 8.7 ns/轮，所以循环一旦能编译，落到 0.10 到 0.15 s 是合理的
  预期——那时目标才真正可能达成。

## 7. 验证

Release 构建（`camel-remote.ps1`，Linux 主机；我改的 12 个文件在 `-Wall` 下没有新 warning，
日志里其余的警告都在 `optimize.h`、`taskflow.cpp`、`onnx/exporter.cpp` 等未动的地方），计划
（`-Jobs 12`）：

| 计划 | 结果 |
|---|---|
| `test/plans/feat/linear/loops.plan.toml` | 100/100 |
| `test/plans/feat/modules/graph.plan.toml` | 46/46 |
| `smoke` | 10/10 |

`loops.plan.toml` 里的 `loops.edges`（两状态、`i % 7`、一百万轮）、`loops.hof_block_alloc`、
`loops.reduce_view`、`loops.collect_multi_state.{nvm,fvm,jit,prl}` 与
`loops.discrete_loop_state` 都在这三个计划里跑过：省掉 `FILL` 的路径与不省的路径都有覆盖，
四个后端的结果互相比较（`backends_agree`）也没有变化。

手工（Release，四个后端）：`bench_loop.cml` 与 `bench_tail.cml` 在 `std::nvm`、`std::fvm`、
`std::jit`、`std::tf` 下都输出 29999994（n = 1000 万）与 299995（n = 10 万）；
`t_mod1/mod2/mod3/div2` 几个小例（非递归、自尾递归、运行期除数）在三个后端下都一致。

没有做代码生成（`std::cpp`）的改动，所以没有跑 cpp 翻译计划。

调试器（顺带跑过，因为省掉 `FILL` 改变了节点序列）：`test/plans/feat/tools/debugger.plan.toml`
串行 12 项过 11 项，`debugger.loop_frames` 过；`debugger.dap_stepping` 的 6 个断言失败在
“尾调用替换掉的帧”上（期望 `fact [arm at line N] (tail call, 1 frame replaced)`，实际是
`fact`），两个后端一模一样，而它的样例里既没有循环也没有 `%`——这块属于臂的子图形态
（H43b 之前的机制），与本项无关，没动。同一个计划在 `-Jobs 12` 下还会多挂一项
`dap_session`（会话端口冲突，单独跑是通过的），所以调试器相关的结论以串行为准。

## 8. 文档

- `docs/technical/39_loops.md`：第 4 节新增“多值状态保留分量而不是每轮造元组”（证的条件、
  `stateComponents` / `elidedAt`、各后端怎么做、不成立时的回退）；第 5 节 Timings 换成这台
  机器上的实测（改动前/后的分解表与前后对比表），并把“剩下的差距与原因”写清；Next steps
  改成“编译循环、一轮一条超级指令、编译体的 k 个分量作为参数”。
- `docs/technical/07_jit_architecture.md`：`LMOD` 在整数运算表里注明“由 helper 执行”，
  故障表里的除零补上 `IMOD`/`LMOD`。
- `docs/technical/jit/bytecode-to-asm.md`：MIR 对应表补一行 `IMOD`/`LMOD`（不映射到 V\* 指令，
  由 `jitHelperBytecode` 执行，`collectRuntimeConstructs` 因此把含 `%` 的图按 framed 编译）。

## 9. 没做的事

- **第 2 项（含循环的图进 JIT）**：不属于第 7 项，没动。它是 `std::jit` 达标的唯一路径，
  也是 `std::fvm` 追上 0.245 的最现实路径。
- **NodeVM 的执行器改造**：`std::nvm` 的 2.2 倍差距要换执行器，不是第 7 项的范围。
- **常量除数的强度削减**（把 `x % 7` 变成乘加移位）：能再省 2 到 3 ns/轮（取余现在只值 3 ns），
  收益已经很小，且在负数、`INT_MIN`、运行期除数上都要重新证一遍语义，不值得动。
- **每轮 safepoint 的省除**（体不分配的循环不轮询）：能省 1 到 2 ns/轮，同样不值得为它引入
  一条“这个循环永远不会分配”的证明。