# 「最后一次使用」成了计划的一列，而交回与终结器是同一件事

- 日期：2026-11-15
- 车道：`lane-count2`（工作树 `D:\Projects\Camel-wt\count2`，基线 `origin/develop` `d7cdb9be4`，含 `0e2eda7be`）
- 前情：`docs/handover/reports/2026-11-15-the-refs-that-come-back.cn.md`（计划侧的表算出来了，没有后端读）、
  `docs/handover/reports/2026-11-15-count-at-zero-has-no-entry.cn.md` §4（工作清单）
- 依据：原则 10.5；作者 2026-11-15 的裁决
  （`docs/handover/reviews/2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md` 文末）
- 状态：**第 2 件的第 1 段落地（那一列 + 加减一作为计划条目）；第 2 段（四个后端执行、删掉 `Ref` 的终结器路径）
  没有做，理由在 §6，它不是「还没排期」而是「两件事必须一起做」。**

## 1. 第 1 步的现状读数：`planStorageReuse` 原来凭什么决定复用

`src/execute/exec_plan.cpp#planStorageReuse`（改前）沿 topo 顺序走一遍，每个变量单元（cell）带一个
`owned` 布尔：`:var/new` 的初值由本图造出来（`Copy` / `Fill` / 图的值槽，`initialValueMadeHere`）时置 1，
`:var/get`（读走了值、读到的值会留着）或普通 `:op/assn` 时清 0。原地更新（`:var/take` + 就地算子）**只在
`owned` 还是 1 时**复用存储；并行后端另加一条 `laterUsersAreOrderedAfter`（晚于更新的使用者必须排在它之后）。
外来单元（`var` 参数、捕获的单元）没有这个证明，交给 `cellValueMayBeKept`：**只要有一个使用者是
`:var/get` 或不是本变量的算子，就认为值可能被留住，于是复制**。

**它没有「最后一次使用的位置」这一列时代替它的是：**
1. `owned` 这个**沿顺序搬的布尔**——「到这一刻为止还没有人读过」，是「第一次使用之前」而不是「最后一次使用之后」；
2. `cellValueMayBeKept` 这个**集合判断**——「使用者里有没有能留住值的」，是「有没有」而不是「在哪」。

两条都是**前瞻的上界**，不是位置。所以计划答不出「这个值在哪条指令之后再没人读」——那正是这一列缺的东西。

## 2. 那一列：`ExecPlan::lastUseAt`

- 位置：`include/camel/runtime/exec_plan.h`，`ExecPlan::lastUseAt`（按 topo 下标），
  `kNoLastUse = 0xFFFFFFFF` 表示「没有读者」，`lastUseOf(ref)` 按节点查。
- 谁算：`src/execute/exec_plan.cpp#planValueUses`，由 `planStorageReuse` 在开头调用——
  作者裁决第 2 条「最后使用由它算，不另写一份」。原来独立的第二次走查 `planRefGiveBacks` 因此**合并掉了**：
  一次 uses 表（`planValueUses`）→ 存储复用决策 → `planRefRows` 摆引用行，顺序就是
  `buildLoopTables` → `planStorageReuse`（内含两者）。
- 判据（可测）：给一个值，它答出「在哪条指令/哪个节点之后再也不被读」。
  `ref_boxes_made_per_call_ten.cml` 的 `burn` 图：位置 1 造出盒子（`state::ref`，算子节点），位置 2 是
  `state::get` 读它，所以 **`value 1` 的 last-use = 2**。怎么算出来的：`executionPredecessorsOf` 给出每个节点
  的 Data 前驱（「谁的值先跑」的唯一答案），一个值经 JOIN/GATE/端口转交也算它的使用；`usesAt[value]` 收集
  全部使用位置，`endOf[value] = max(自己, 所有使用位置, 打包者的 endOf)`（反向 topo 读，打包者是 FILL，
  它比被它打包的值靠后）；`usesAt` 为空则 `kNoLastUse`。`burn` 里 `value 1` 只有一个使用（位置 2），
  所以列答 2；同一张图的 `value 2`（`get` 的结果）没人读，答 `none`。
- 打包那一份也读这一列：`ref_one_cell_two_holders.cml` 的 `main` 里 `value 3`（闭包 FILL）的 last-use 是 12，
  被它捕获的 `value 1` 的 `packed` 行就在位置 12（改前是 `endOf` 这个局部变量，现在是同一列）。

## 3. 计划条目的形状：`ExecPlan::RefEntry`

`refEntryOffsets` + `refEntries`，`refEntriesAt(i)` 给出位置 i 的条目：

| 条目 | 什么时候 | 含义 |
|---|---|---|
| `Retain value x n` | 产出位置 | 这个节点的产出除了自己那一份，还要多拿 n 份（= `refCopiesAt[i]`） |
| `GiveBack value x n` | 使用位置 / 打包者死的位置 / 没有任何读者时它自己的位置 | 这个位置跑完之后回来 n 份 |

`refEntriesBalance()` 是**按行**重算的平衡：`produced + retains == give-backs + kept`，与原来
`refBalanceHolds()`（`produced + copies == giveBacks + kept + packed`）是同一件事的两种读法。
`test::plan_refs` 把它们印出来（`PLAN_LAST_USE`、`PLAN_REF_ENTRIES` 两个新前缀，原来的 `PLAN_REFS` 行一字未动），
`plan_refs.last_use_column` 钉住 `burn` 那一条与 `main` 的 `none`。

## 4. 三个计数盒子用例 × 四后端 × 三次（运行中途读数）

临时计划（未提交，量完即删），旗标与 `ref.boxes_*` 逐字相同（`--gc-stress … --gc-check foreign,verify=both
--gc generational`），`expect_exit = 1` 而程序退 0，每轮都失败、于是每轮都打印自己那一行。读数格式
`created/released/live/disposed/finalized`，**三个用例 × 四个后端 × 三次，全部一致**（第二遍独立重跑同值）：

| 用例 | nvm | fvm | jit | prl |
|---|---|---|---|---|
| `boxes_are_counted` | `1001/0/1001/1001/0` ×3 | `1001/0/1001/1001/0` ×3 | `1001/0/1001/1001/0` ×3 | `1001/1001/0/0/1001` ×3 |
| `boxes_loop_ten` | `11/8/3/3/8` ×3 | `11/11/0/0/11` ×3 | `11/11/0/0/11` ×3 | `11/11/0/0/11` ×3 |
| `boxes_made_per_call_ten` | `11/11/0/0/11` ×3 | `11/9/2/2/9` ×3 | `11/9/2/2/9` ×3 | `11/11/0/0/11` ×3 |

- **`ref.boxes_made_per_call_ten.prl` 三次都是 `11/11/0`，但它是 flaky**：`ref.plan.toml` 自己写着
  `p≈0.3-0.5`（`--jobs 4`），前一份读数里它出过一次 `11/5/6/6/5`。**三次绿不等于状态**，`-`jobs 8` 下三次
  一致只是这一批。这条读数不能当判据用。
- `boxes_are_counted` 的 nvm/fvm/jit 读 `live=1001` 与 prl 读 `live=0` 同源：今天唯一能把 `live` 减一的地方
  是收集器里的 wrapper 终结器（`ForeignResourceObject::finalize` → 延迟队列 → `release()`），所以 `live` 量的是
  「退出时还没有被任何一次收集看见的死 wrapper」，不是「持有者还剩几个」。`created=1001` 四后端恒等。
- 这一批的宿主 1m load 15.2–24.5（每次运行前后各取一次），**高于门槛 15，读数不可信，只作分布不绝对值用**。

## 5. 反向验证：把这一列改成「永远不是最后一次」

临时（未提交）两处改动：`planValueUses` 里不写 `lastUseAt`；`planRefRows` 里打包行遇到 `kNoLastUse`
直接丢掉。结果：

- 每一行 `PLAN_LAST_USE` 都变 `none`（这一列是印出来的那个数）；
- `ref_one_cell_two_holders.cml` 的 `main`：`packed 1 → 0`、`give-backs 14` 不变、
  `produced 11 copies 4` 不变 → **`balanced yes → no`**；
- `plan_refs.closure_holder` 与两条读数条目按预期变红（其余 1 条绿是 `plan_refs.counted_box`，它没有打包行）。

即：**这一列被摆表这件事读了**，翻掉它，表的行数与平衡都变。运行时的 `created/released/live` 不会变——
因为没有任何后端执行这张表（见 §6），这一点必须写清楚，别把计划侧的变读成计数的变。

## 6. 没做的事，以及为什么「执行交回」与「删 `Ref` 终结器」是同一件事

第 4、5 步（四个后端照条目执行、删掉终结器）没有做。原因不是工作量大，是**块的生命期只有一条**：

- `ForeignResourceControlBlock::release()`（`src/core/rtdata/foreign.cpp`）在计数减到 0 时
  `dispose()` 并 **`delete this`**；
- 而 `Ref` 的**每个** wrapper（`ForeignResourceObject`）都用**裸指针** `control_` 指着这块；
- 所以「归零即释放」一旦真的执行，wrapper 还活着就指着已释放内存；它的终结器
  （`ForeignResourceObject::finalize`）照样把 `control_` 塞进延迟释放队列 → use-after-free。

`docs/handover/reports/2026-11-15-count-at-zero-has-no-entry.cn.md` §4 第 4 条写的是「终结器在块已被计数释放后
必须成为空操作」——**这条要求把两件事绑在一起**。作者随后的答复把它分成了甲与丙（见 §6a）。

## 6a. 甲是过渡，丙是终点（作者答复 2026-11-15）

**甲（已在本次提交里落地）**：计数只决定**资源**何时释放，wrapper（句柄这个值本身）暂时仍由回收器释放。原则
10.5 末句允许这一过渡——「核实之前程序的值仍留在分代堆上，不影响正确性」；可观察的是资源何时释放，那一半由
计数管。实现上：`ForeignResourceControlBlock` 现在有两个数——`refCount_`（**wrapper** 的个数，回收器那边）与
`holderCount_`（**持有者**的个数，计划里的加减一）。`releaseCountNow` 让 `holderCount_` 减到 0 时只
`dispose()` 资源、把它从登记表里摘掉、`released +1 / live -1`，**不 `delete this`**；块的**内存**仍由最后
一个 wrapper 的终结器（`release()`）释放，那时 `disposed_` 已经是真，`dispose()` 是空操作。所以终结器只管
wrapper 的内存，资源与计数它一概不碰。

**丙（终点，本次不做）**：句柄这个值**本身就是被计数的引用**，不再有一个带终结器的 wrapper——
计数为零就意味着没有任何持有者，也就不存在「还活着的 wrapper 指着已释放的块」这个问题，
**既不需要终结器，也不需要乙那条「块反向找到 wrapper」的登记表（乙不取）**。丙随「程序的值移出回收堆」
那一步（收敛阶段）做，到那时把终结器路径整个删掉。**判别用（`Ref` 活着、计数归零、终结器随后到达 ⇒ 甲下
`finalized=1` 且不崩）是甲到丙过渡期的看门人，丙落地时才能删，现在不能少。**

## 6b. 还没做的：四个后端执行条目（作者点名为「剩下的主要工作」）

`applyRefEntriesForNode` 不在本次提交里。落点已经确定（anchor 的 `noteCountedCreation*` 就挂在这三处，是
四个后端共用的那种位置）：`nodevm.cpp#NodeVMSchedPass::call` 的 `case GCNodeKind::Oper` 之后、
`fastvm/native_call.cpp#FastVMSchedPass::invokeNativeOperator` 之后、`taskflow.cpp#TaskflowExecSchedPass::executePreparedOperator`
之后；读 `graph->execPlan().refEntriesAt(topoIndexByRef[node])`，`Retain` 走 `retainCountNow`、`GiveBack` 走
`giveBackCountNow`。**两条必须写进实现的约束**：1) 先看槽的类型是不是 `TypeCode::Ref`——`retainCountNow` 今天
只查「是不是 `ForeignResourceObject`」，直接对每个值调用会把 python 对象与文件句柄也算进去；2) 只执行**产出
位置是算子节点**的那些值（`refProducedAt` 那一行的 `topo[idx]` 是 `GCNodeKind::Oper`）：算子的产出是四个后端
唯一都经过同一个函数的值，其余产出（CALL、FILL）在 `fastvm` 里不经过这个入口——只做一边会变成
「retain 跳过、give-back 执行」，那是**早释放**，比不释放坏。跳过的那一类落在退出清扫上（`live` 留着，
永不为零），符合「宁可少释放也不要早释放」。

**重量时的判据（作者原话三条）**：1) 四个后端都执行条目；2) 资源在计数归零时释放；3) 运行中途读数不随次数
增长（跑 1 次与跑 10 次的 `live`/`finalized` 对比，`ref.boxes_made_per_call_ten` 与 `..._thousand` 正是这一对）。

**下一步（给接的人，按顺序）**：1) 按 §6b 在三个入口执行条目（§6a 的甲已经落地，所以这一步现在只剩调用）；
2) 删终结器那条路径——但那要等 §6a 的丙；3) 再量 §4 那张表，那时 `live` 才是「持有者还剩几个」。

## 7. 这一页没有做的事

没有改期望值、没有加 `known_fail`、没有动 `faccf38b9` 的折叠、没有改 `docs/design-principles.cn.md`、
没有改 `planStorageReuse` 的复用判据（`owned` 仍是对的：读在更新**之前**才妨碍复用，读在更新**之后**是程序
本来的意思，用这一列去卡它只会白白少复用）。**没有让任何后端执行条目**（§6b），所以甲落地后运行时读数与
落地前逐位相同——这正是甲作为过渡的样子：它只换了「谁在计数归零时做什么」，而今天还没有人把计数走到零。