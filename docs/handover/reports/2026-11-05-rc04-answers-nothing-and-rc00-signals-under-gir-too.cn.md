# 报告：`rc04_seed72` 修完之后答什么，以及 `rc00_seed27` 在 `std::gir` 下也吃信号

> 线：`lane-guard3`（分支 `lane-guard3`，工作树 `guard3`），tip `20a2eaacb`。
> 主机：闸门主机（`internal/tools/camel-gate.local.json` 的 `sshHost`），远端树 `/home/zjwei/camel-work/guard3`，`-Jobs 8`，`load (min 1m) 47.5`（本次探针）与 `22.2`（基线探针），构建时刻 `2026-11-04T12:50:01-05:00` 与 `2026-11-04T12:31:32-05:00`。
> 相关：`src/runtime/draft_inline.cpp#resolvePlaceForks`（`bf12366ba`）、`src/runtime/graph_edges.cpp#findCycle`、`include/camel/runtime/cycle.h`、`test/cases/opt/adb67e081/rc04_seed72.cml`、`test/plans/feat/linear/jit_framed.plan.toml`。

## 1. 这个程序的正确答案是「什么都不打印」

`rc04_seed72.cml` 里没有任何可观测输出：`main` 只建了两个值——`n62` 是 `true`（两个 `??` 都取到非空的分支），`n75` 是 `k14(len([1.0]))` 得到的闭包——然后 `return 0`；闭包 `p17` 从未被调用，所以 `v16 /= -2` 也不会跑，程序里没有一条打印。

| 管道 | exit | stdout | stderr |
| --- | --- | --- | --- |
| `std::nvm`（不带任何 pass，基准） | 0 | 空 | 空 |
| `std::opt::inline std::nvm` | 0 | 空 | 空 |
| `std::opt::inline std::fvm` | 0 | 空 | 空 |
| `std::opt::inline std::gir` | 0 | 14432 字符的 GIR | 空，无任何违规行 |

所以「什么都不打印」是这个程序本来的答案，不是该打印而没打印：无 pass 的基准与两个后端给的是同一个答案。`std::opt::inline std::gir` 这一行另外说明图本身通过了密封——`std::gir` 提交的是内联后的图并把它画出来，seal 没有报违规。

## 2. 为什么这不是「把环藏起来了」

环的判据没有被我改动。`src/runtime/graph_edges.cpp#findCycle` 的原文：

> The judgement the draft's check makes, on the frozen graph (camel/runtime/cycle.h): every
> edge kind takes part, the sync and order ones included. Each of them makes one node run after
> another -- a sync edge is a real ordering (docs/design/effects/edges.cn.md, section 1) -- so a
> cycle of them is a graph no back end can run, and it is refused here rather than handed on.

它不是空话：`src/runtime/graph_edges.cpp` 里的 `FrozenCycleEdges::inputsOf` 有 `case CycleEdgeKind::Order:` 分支，读 `graph_.orderPredsOf(ref)`；而 `include/camel/runtime/cycle.h` 把五类写成同一个枚举，并在注释里说清了两处判据的关系：

> The kinds of edge that make one node run after another. `Order` is last: `findInputCycle`
> walks kinds by increasing value up to kCycleEdgeKindCount, so a kind added here is walked by
> both the draft's check and the freeze's without touching either.

`enum class CycleEdgeKind : uint8_t { Norm = 0, With, Ctrl, Sync, Order };`

内联器那条 `resolvePlaceForks` 的守卫是**手写的第二份走法**，不是这个模板，于是它漏了第五类。改动只动内联器自己的前置条件（「这个位置等不等这条语句」），没有动密封：图仍然要过同一个五类环检查，`std::gir` 与三个后端都不报违规，就是这条检查在小程序上仍然会说话的证据。

代价的方向是对称的另一半：守卫说「等」时发生的只是「这一次移动不做」，而移动本身是优化，不是语义。`rc04_seed72` 里另有一条 forked 语句（37）仍然被移到 24 之后，于是 21 只剩一个后继、叉消失——拒绝没有让 pass 失败，反而让它跑完并给出与基准相同的答案。

## 3. `rc00_seed27` 在 `std::opt std::gir` 下也被信号杀掉（基线，未带本次三笔）

在同一台主机、同一棵树名上，把 `src/runtime/draft_inline.cpp` 退回基线（`6c28291ad`，树上只有两个未跟踪探针文件）后测得：

| 命令 | 读数 |
| --- | --- |
| `camel rc00_seed27.cml std::nvm` | exit 0，无输出 |
| `camel rc00_seed27.cml std::opt std::gir` | 进程被信号杀掉（计划汇总 `killed by a signal: 1`，`exit=null`） |

`test/cases/opt/adb67e081/README.md` 只记了 `std::opt std::nvm` 的 SIGSEGV，并按此把它排除在所有 plan 之外。上表说明「换一个 pass 就能避开」这条推断不成立：`std::opt std::gir` 一样吃信号。它仍然不在任何 plan 里（`feat/opt` 的汇总因此不受影响），处置留给拥有调度器的那条线：先定位是不是同一个崩溃点，再决定要不要为它加一个 plan 条目。