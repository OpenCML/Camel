# 链头节点被删时，它的后继拿不到链位

## 我量到的

一个程序 `test/cases/modules/autodiff/nn_grad_check.cml`，管线 `std::macro std::opt::inline growth_budget=512 std::opt::fold std::opt::dce std::nvm`，二进制由 `camel-remote.ps1` 从当前树构建。它在 `std::opt` 下只通过 1 条检查（`cross_entropy`），在 `growth_budget=511` 下通过 6 条；两次的差别只在"整轮循环跑了几轮"，图本身在 `inline`、`fold`、`dce` 各步上逐字节相同。

三处探针（只打印，不改语义）在这一次运行里的总数：

```
eraseNode 298 次
closeChainOver 328 次
setSyncInputs 把一条 sync 输入清成空：11 次，全部来自同一个调用点
```

清空的那 11 次逐条长这样（节选，`kind` 是 `GCNodeKind` 的数值）：

```
PROBE eraseNode      id=142 kind=13 syncIn=0 syncOut=1 ctrlIn=0 ctrlOut=1
PROBE closeChainOver id=142 kind=13 syncIn=0 syncOut=1 ctrlIn=0 ctrlOut=1
PROBE clearSync      user=73 kind=13 old=1 new=0 oldpred=142

PROBE eraseNode      id=73 kind=13 syncIn=0 syncOut=1 ctrlIn=2 ctrlOut=0
PROBE clearSync      user=20 kind=11 old=1 new=0 oldpred=73
```

被删的节点**自己就是链头**（`syncIn=0`，只有一个 sync 后继）。`closeChainOver` 只用被删节点的 `syncInputs` 构造要给后继的那份前驱，所以那份**按定义就是空**，后继的 sync 输入被清成空，它后面那一段链变成没有头的段。

后果在读达性上：`dce` 之前从出口反走，`main` 图 **364/386** 可达；`dce` 之后只剩 **56/321**，6 条 `:io/println` 里有 5 条不可达。这 6 条打印是 `check(...)` 被内联后落进 `main` 的语句，它们通往出口的唯一一条边就是那条 sync 边；链位一被清空，上游整段失去可达性，于是不执行。11 次清空与"丢掉的语句段"数目对得上。

## 试过的两种改法，都不成

| 改法 | 读数 |
|---|---|
| `src/runtime/draft.cpp#closeChainOver` 的 `keepWaiting` 里把来自 sync 的前驱按 sync 交出去（原来一律按 control 交） | 全量 `fail 8`：数目不变、名单换了一条（新增 `runner.backend_matrix`，消失 `native_state_checkpoints`），`nn_grad_check` 三条仍只过 1 条 |
| `kept` 为空时，从被删节点的同块 ctrl 前驱（优先）再 order 前驱接一条链位给后继 | 全量 `fail 10`，**新增 `var_between_statements.opt` 与 `var_between_statements.opt_jit`**，输出 `1 \| 2 \| 1 \| 2 \| 1 \| 1 \| 2 \| 6`；`nn_grad_check` 三条仍只过 1 条 |

第二种改动说明了一件事：**ctrl 前驱和 order 前驱不是"该在它前面执行的语句"**。把它们接管成链位，会把 `var` 语句之间的执行顺序改掉——`var_between_statements` 两条就是这么红的。所以"链头位置由另一条边补出来"这条思路不成立。

与尾调用那条裁定是同一处：**拼接时链尾继承了调用的等待（接值门），但链头没有接到调用点原来的链位上**。删节点只是把"本来就没有的位置"暴露出来；链头是孤立的，不是在删除时才被孤立的。

## 要问的一句

这条 sync 边缺失，应该在哪一侧收口：

- 在**制造端**补：拼接、内联把代码搬进调用者时，链头也要接到调用点原来的链位上（`docs/design/effects/edges.cn.md` 5.7）；或
- 在**执行器侧**接受"删掉链头后后继另起一段"，把段头标成不需要可达（`src/runtime/graph.cpp` 里调度器读链的那些函数）。

两条都是语义决定，我不动。

页尾记一条环境：`runner.backend_matrix` 在这两次全量里都出现过（23 秒上下，计划运行器自己的自测会再起一层并行，`-Jobs 8` 下受负载影响），按已知的环境抖动计，不算红。