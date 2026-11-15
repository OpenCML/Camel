# prl 的计数盒归还：读数不是确定的，而且 prl 这一侧改不成确定的

- 日期：2026-11-15
- 车道：`lane-prlcount`（工作树 `D:\Projects\Camel-wt\prlcount`，基线 `f135bbb16`，`origin/develop`）
- 被测二进制：本工作树 `npm run build` 的产物，`out/latest/build-info.json` 记 `f135bbb168 (clean)`
  （本车道**未改** `src/`、未改 `test/`、未改期望值、未加 `known_fail`：下面第 5 节说明为什么没有可提交的修）
- 机器：i7-14700K（28 逻辑核），64 GB；批次进行时机器上另有别的车道在跑
- 原始表：同目录 `*.tsv`（每轮一行）；单轮日志在 `$env:TEMP\prlcount\logs\`、`$env:TEMP\prlcount\case-logs\`

## 1. 一句话结论

**读数的不确定来自「归还」这件事今天只有一条路：收集器。** 唯一会把 `live` 减一的入口是
`ForeignResourceObject::finalize`（`src/core/rtdata/foreign.cpp#ForeignResourceObject::finalize`），它**只在一次收集里**被调用；`Frame::set`、
`TaskflowFramePool::release` 都只清槽、不减计数（`src/passes/sched/taskflow/taskflow_frame_pool.cpp#TaskflowFramePool::release`、`clearGcSlots`）。
于是 `live` 等于「退出时还没有被某次收集看见的死箱子」，而 prl 的收集点落在哪个程序点由线程调度决定，
所以它不是确定的。**把 prl 的收集点变确定（或让 prl 少收集几次）都做不到这件事**：`live` 的不确定度
与「收集次数」无关（见 §4.3，轮询数与收集数每轮完全相同、读数照样在 0/4/5/6 之间），只与「收集落在哪」有关。

## 2. 修前读数（按要求原文）

### 表一 计划级，`--jobs 1`，20 遍

命令：`node scripts/test.js test\plans\feat\compile\ref.plan.toml --jobs 1`，20 遍（`A-before-plan-jobs1.tsv`）。
红 = 该轮出现 `[FAIL] ref.boxes_made_per_call_ten.prl`。

| 轮 | 结果 | 红轮读数（`FOREIGN_RESOURCES` 原文） |
|---|---|---|
| 6 | 46/1 红 | `created=11 released=6 live=5 disposed=5 finalized=6` |
| 8 | 46/1 红 | `created=11 released=7 live=4 disposed=4 finalized=7` |
| 10 | 46/1 红 | `created=11 released=5 live=6 disposed=6 finalized=5` |
| 18 | 46/1 红 | `created=11 released=7 live=4 disposed=4 finalized=7` |

其余 16 轮 47/47 绿（运行器只在失败时打印用例输出，所以绿轮的 `FOREIGN_RESOURCES` 不在这张表里；
按断言它们满足 `live <= 3`，逐轮原文见表二的用例级读数）。
红的一律只是 `.prl` 那一格；`nvm`/`fvm`/`jit` 20 轮从未红。**20 遍里红 4 遍，全部串行、零测试并发。**

### 表二 用例级，`std::prl`，20 轮（`B-before-prl-case.tsv`）

命令：`camel --gc-stress alloc=7,safepoint=3,kind=both --gc-check foreign,verify=both --gc generational
test\cases\compile\ref\ref_boxes_made_per_call_ten.cml std::prl`。

| 读数（created/released/live/disposed） | 次数 |
|---|---|
| `11/11/0/0` | 14 |
| `11/5/6/6` | 2 |
| `11/7/4/4` | 2 |
| `11/6/5/5` | 1 |
| `11/9/2/2` | 1 |

`created` 恒为 11；抖的只有 `released`/`live`，而且 `released == finalized`、`disposed == live` 恒成立，
20 轮退出码全 0（表里省 `;exit=0`）。

**注意最后一行**：prl 有一轮读出的正是 `created=11 released=9 live=2`，也就是 fvm/jit 的那个数。
另外两批各 20 轮（都被下一批覆盖，未留表）里读到的是 `13×(11/11/0) + 3×(11/5/6) + 2×(11/7/4) + 1×(11/6/5)`
和 `17×(11/11/0) + 1×(11/5/6) + 1×(11/7/4) + 1×(11/9/2)`；60 轮里共 2 轮落在 `11/9/2`。
**所以 `11/9/2` 不是 prl「该读到、却没读到」的值，它是这个分布上的一个点**：fvm/jit 每轮都停在那，
prl 每轮按调度抽一次。

### 表三 去掉 `--gc-stress` 的对照（按要求原文，`C-before-prl-nostress.tsv`）

命令同上但去掉 `--gc-stress alloc=7,safepoint=3,kind=both`，只留 `--gc-check foreign,verify=both`：

**20/20 轮全是 `created=11 released=0 live=11 disposed=11 finalized=0`。**

这条不是 prl 的独有现象：同一组旗标下 `fvm`、`nvm` 各跑 3 次，同样都是
`created=11 released=0 live=11 disposed=11 finalized=0`（`D-nostress-all.tsv`）。**四个后端在没有收集的时候
一块都不归还**，因为除收集之外没有任何一条归还路径（§3）。

### 表四 对照后端，20 次（同一组压测旗标，`E-control-*.tsv`）

| 后端 | 20 次读数 |
|---|---|
| `std::fvm` | 20/20 `created=11 released=9 live=2 disposed=2 finalized=9` |
| `std::jit` | 20/20 `created=11 released=9 live=2 disposed=2 finalized=9` |
| `std::nvm` | 20/20 `created=11 released=11 live=0 disposed=0 finalized=11` |
| `std::prl` | 14 × `11/11/0`、2 × `11/5/6`、2 × `11/7/4`、1 × `11/6/5`、1 × `11/9/2` |

### 表五 一个 worker 的 prl（同一二进制，`-t 1`，`F-prl-t1.tsv`）

`camel -t 1 ... <case> std::prl`，20 遍：**20/20 `created=11 released=11 live=0 disposed=0 finalized=11`**。

**同一个二进制、同一份程序、同一组旗标，只把 worker 数从 28 改成 1，读数就变确定。** 抖来自调度，
不来自计数逻辑本身的计算。

## 3. 唯一的减一在哪里

| 位置 | 做了什么 | 是否减少 `live` |
|---|---|---|
| `ForeignResourceObject::create`（`src/core/rtdata/foreign.cpp#ForeignResourceObject::create`） | 新建 control block，`refCount_` 起于 1 | 不是 |
| `ForeignResourceObject::finalize`（`src/core/rtdata/foreign.cpp#ForeignResourceObject::finalize`） | 收集器发现 wrapper 已死 → 排队 → `releaseFromWrapperFinalizer` → `release()` | **唯一** |
| `ForeignResourceControlBlock::release`（`src/core/rtdata/foreign.cpp#ForeignResourceControlBlock::release`） | `refCount--`，到 0 时 `releasedControlBlocks++`、`liveControlBlocks--`、`delete` | 是（只由上一行进来） |
| `TaskflowFramePool::release` / `clearGcSlots`（`src/passes/sched/taskflow/taskflow_frame_pool.cpp#TaskflowFramePool::release`、`#clearGcSlots`） | 帧回池、句柄槽写 `NullSlot` | **不**（只让 wrapper 变成不可达） |
| `disposeAllForeignResources`（`src/core/rtdata/foreign.cpp#disposeAllForeignResources`，退出时） | 对每个还活着的 block `retain→dispose→release`（净 0） | **不**；它只 `dispose`，`live` 不减 |

所以「非压力模式下 prl 不归还」的真相是：**四个后端都不归还**（表三），归还是收集器的事；`--gc-stress` 只是
让收集真的跑起来，于是 `live` 变成「最后一次收集之后还剩下的死箱子数」。

## 4. 为什么 prl 是 0/4/5/6 而 fvm/jit 是 2

### 4.1 fvm/jit 为什么稳

fvm/jit 把 `main` 与 11 次 `burn` 全部放在**一个线程、一份固定字节码**里跑，安全点在固定的程序点
（每个 `FUNC`/`TAIL`，循环末 `LEND`）。`safepoint=3` 于是落在固定程序点上，收集序列每次完全相同：
最后一次收集发生在最后两个箱子出生之前，之后没有收集点看过它们 → 恒为 `released=9 live=2`（20/20）。

### 4.2 prl 为什么不稳

`main` 因为含高阶算子 `:mark/map_arr` 不能交给字节码解释器（`src/passes/sched/taskflow/handoff.cpp#TaskflowExecSchedPass::serialObstacle`
「it has a higher-order operator」），它在调度器上跑，10 次 `burn` 是 10 次 hosted call。
这 10 个元素被分给**几个 worker 是每次不同的**：实测同一命令连续 10 次，
`[taskflow] bytecode: calls=10 interpreters=` **10 / 1 / 10 / 7 / 10 / 10 / 10 / 10 / 5 / 9**
（`G-prl-interpreters.tsv`），而读数 `live` 在那 10 次里是 **2 / 0 / 0 / 6 / 0 / 0 / 0 / 0 / 2 / 6**。
分给谁由 `runElements` 的**探测式**决定（`src/passes/sched/taskflow/taskflow.cpp#TaskflowExecSchedPass::runElements`：`kProbeNs`/`kSpawnNs` 是墙钟阈值，
按「刚测出的速度」决定要不要帮手、叫几个）。

### 4.3 抖的不是次数，是位置

`H-prl-pollcounts.tsv`（15 轮）：**15/15 轮里轮询数、收集数、被配速跳过的数完全相同**
（`pending_polls=12 collections=6 capped=9`），读数却是 `live=0` 13 次、`live=3` 1 次、`live=6` 1 次。
**同一组计数、不同读数**，所以差异只能来自「每次收集落在哪个程序点」：10 个元素铺在 1–10 个 worker 上时，
某次收集可能看见几个箱子的帧还活着（一个都不放），也可能一个都不剩（全放）。这就是 0/4/5/6。
（`G-prl-interpreters.tsv` 里另有 1 轮读到 `collections=12 capped=6`：配速跳过的次数本身也会变，
那是 §5 那条真缺陷的另一面，不是这里的主要项。）

### 4.4 所以「让收集确定」也不管用

- 把压测收集**一对一服务**（`std::prl gc_stress=true`，等于去掉 `capUntilNs_` 的墙钟配速）：
  读数从 `{0,4,5,6}` 收紧到 `{3 ×14, 4 ×1}`（15 次），**仍不是同一个值**。
- 把元素**无条件全部扇出**（`std::prl fanout_all=true`，固定宽度、不再探测）：`live` 仍读到
  `0 ×11、5 ×2、4 ×1、3 ×1`（15 次）。**执行形状变固定了，读数还是抖。**

这两条合起来说明：只要减一只发生在收集里，prl 的读数就**不可能**确定——它必须是「每次收集落在哪」的函数，
而那是调度。要确定，只能让减一**不经过收集**。

## 5. 为什么这一侧修不了（没有可提交的修）

要让减一不经过收集，只有两条路，**两条都已经被作者排除或撤回**：

1. **在入口图返回处收一次**（`requestMajorCollection` + `safepoint`）。已经被 `a380ded2e` 撤回：读数是对的
   （四后端 `released=created`、`live=0`），但它让 `test/plans/feat/opt/defun.plan.toml` 变成
   9 红 8 信号，因为运行中的收集会撞上「函数类型槽里装着带 tag 的 Variant」
   （`2026-11-15-a-function-typed-slot-holds-a-tagged-variant.cn.md`）。作者也判过：时机与机制都不对。
2. **Perceus 式持有者计数**（每多一个持有者 +1、持有者消失 −1、归零即释放）。作者 2026-11-15 裁定归
   **第 4 路**，由**执行计划上的「最后使用」分析**统一给出，四个后端只照着执行
   （`2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md` 的「决策者答复」）。
   这不是 prl 一侧的改动：`Frame::set`、聚合的 `set`、闭包捕获、JIT 内联槽都要走同一组加减入口，
   而且 `create` 自带的那一份要交给产出点。

在那一侧落地之前，prl 的读数**只能**是调度相关的。本车道因此**没有改 `src/`**：任何「让 prl 在
`--gc-stress` 下稳定变绿」的改法都只是把一次抽签换个分布，而且会违反「不许只在 `--gc-stress` 下让它变绿」。

### 顺带量到、但没提交的一个真缺陷

压测收集在调度器一侧是**按墙钟配速**的（`TaskflowGcGate::collect` 的 `capUntilNs_`：收集后
「下一次不早于上次耗时的两倍」）。本用例每轮 12 次轮询里有 **9 次**被这次配速跳过（`capped=9`，
`src/passes/sched/taskflow/taskflow.cpp#TaskflowGcGate::collect`），而 `gc_collections.plan.toml` 的头部已经写明「压测运行必须一对一服务
（`std::prl gc_stress=true`）」。`ref.plan.toml` 的这些 prl 臂没有写这个参数，于是压测收集被墙钟节流。
把配速对压测配置关掉（`--gc-stress alloc=…/safepoint=…` 或 poisoned safepoints 时）会把 prl 的读数
从 `{0,4,5,6}` 收紧到 `{3,4}`（§4.4 第一条实测），但**仍不满足「20 遍同一个值」**，而且会改变
20 个 plan 里所有 prl 臂的收集次数，所以**没有**随本页提交。要做的话是 `mm.h` + `mm.cpp` + `taskflow.cpp`
三处、约 6 行。

## 6. 闸门（本页提交前在 `lane-prlcount` 上跑的四条）

| 命令 | 结果 |
|---|---|
| `CAMEL_TEST_BASE=f135bbb16 node scripts/test.js --changed --jobs 4` | total 33, pass 32, fail 1 —— 唯一红是 `docs.snippets_compile`（常驻红） |
| `node scripts/test.js test/plans/feat/compile --jobs 4` | total 2485, pass 2459, fail 10, known failures 16 —— 10 红全是 `stream.*` 生成器（常驻红） |
| `node scripts/test.js test/plans/feat/opt --jobs 4` | total 1412, pass 1412, fail 0 |
| `node scripts/test.js test/plans/feat/compile/ref.plan.toml --jobs 4` | total 47, pass 46, fail 1 —— `ref.boxes_made_per_call_ten.prl`，就是本页记的那个缺陷 |

本页**只增文档与量测脚本，不改任何 `src/`、`test/`、期望值**，所以前三条与基线一样；第四条的红是这条车道
要修、而这一侧修不了的那一格（`--jobs 4` 下约 1/4 概率撞上，见表一）。

合进 `origin/develop` `4d475aaaf` 并重建之后（tip `79d11165a`，二进制 `50fdda557c`），同样四条、同样
`--jobs 4` 的读数是：`--changed` 3998/4028 红 **14**（11 个 `stream.*`、`docs.snippets_compile`、本车道那一格、
以及 §9 的 `agree_with_fvm`；选择面比上表宽得多，因为合并碰了 `src/compile` 与 `src/trans`）；
`feat/compile` 2466/2490 红 **8**（全 `stream.*`）；`feat/opt` 1412/1412 红 **0**；
`ref.plan.toml` 52/52 红 **0** —— 最后这一行是**抽到绿**，不是那一格变稳了：同一棵树连跑 10 轮它红 5 轮。

## 7. 复现

```powershell
$env:CAMEL_HOME = "D:\Projects\Camel-wt\prlcount\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
cd D:\Projects\Camel-wt\prlcount

# 计划级，串行 20 遍（表一）
pwsh -NoProfile -File docs\handover\reports\2026-11-15-prl-counted-boxes-return\run-plan.ps1 `
  -Tag A-before-plan-jobs1 -Runs 20 -Jobs 1
# 用例级，压测 20 轮（表二）
pwsh -NoProfile -File docs\handover\reports\2026-11-15-prl-counted-boxes-return\run-case.ps1 `
  -Tag B-before-prl-case -Backend "std::prl" -Rounds 20
# 去掉 --gc-stress（表三）
pwsh -NoProfile -File docs\handover\reports\2026-11-15-prl-counted-boxes-return\run-case.ps1 `
  -Tag C-before-prl-nostress -Backend "std::prl" -Rounds 20 -NoGcStress
```

单次读数：`camel --gc-stress "alloc=7,safepoint=3,kind=both" --gc-check foreign,verify=both --gc generational
test\cases\compile\ref\ref_boxes_made_per_call_ten.cml std::prl`（对照后端把 `std::prl` 换掉）。

## 8. 没有回答的

- `live` 该等于几：按原则 10.5 与作者裁定，退出时持有者数为 **0**（nvm 与 `prl -t 1` 今天就是 0）。
  fvm/jit 的 `2` 与 prl 的 0/4/5/6 是同一个缺陷的两面，都要等计划侧的最后使用分析。
- 用例自己的注释说这个程序「2 blocks on fvm, jit and prl, three on nvm」：**已过期**（nvm 今天读 0），
  本车道未改，留给做计划侧那一条的人一并改。

## 9. 顺带发现（都不修，记在这里）

**（一）`trans.cpp` 的 `agree_with_fvm` 在合并后的基线上就红，与本车道无关。**
`--changed` 宽选时会选中它（合并碰了 `src/compile`、`src/trans`）。红法是编译链而不是判据：
`test/cases/trans/cpp/mean_axis_elements.cml` 的 C++ 程序**编不过**（175 个程序里 174 个 same，
1 个 `does not build`，`G:\DevEnvs\LLVM\bin\clang++.EXE`）。该用例最后一次改动是 `3c5770946`，
它是 `f135bbb16` 的祖先 —— 也就是说这道红**在基线之前就在**，不是本车道、也不是这次合并引入的。
**不修**，另派。想单跑它：
`node scripts/test.js test/plans/feat/trans --jobs 4`（约 60 s，因为要编 175 个程序）。

**（二）合并之后必须先重建再跑，否则会读到一批可信的假红。**
本次合并（`origin/develop` `4d475aaaf`）带来了 `src/compile/gir` 的改动；用合并**之前**的二进制跑合并**之后**
的计划树，`ref.one_box_two_names.{nvm,fvm,prl}` 与 `….jit`、`….one_box_two_names_order` 共 5 条会全红，
重建之后 5 条全绿。规则写成一句可复用的：

> **改了 `src/`、`include/`、`stdlib/` 或合进了这样的改动之后，跑任何计划之前先 `npm run build`；
> 二进制不加盖自己不会变，而它给出的红和真红长得一模一样。**

它和另外两种「假读数」同族，都是**同一份东西的两个副本不同步**：主检出里 `out/latest/build-info.json` 是旧件
（构建产物的时间戳不说明它对应哪个源码）；在**没有 `.git`** 的目录里跑 `git`，读到的是别人仓库的答案
（`git` 会沿父目录往上找）。三种情况的共同点是：**读数看起来完全可信，错在它不来自你以为的那个树。**
判据是 `[provenance]` 那一行（`tip`、`binary built`、`tree`），它由 runner 自己打印，不靠记忆。

