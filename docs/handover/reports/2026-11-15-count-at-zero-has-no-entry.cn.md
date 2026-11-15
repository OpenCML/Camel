# 归零当场释放今天没有入口：计数不在持有者身上，在 wrapper 身上

- 日期：2026-11-15
- 车道：`lane-refcount`（工作树 `D:\Projects\Camel-wt\refcount`，基线 `df057ee8a`，并入答复 `4716dd04c`）
- 前置读数：`docs/handover/reports/2026-11-15-count-at-zero-before.cn.md`（四后端各 10 轮，两个宽度）
- 判据：原则 10.5；作者 2026-11-15 的裁决
  （`docs/handover/reviews/2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md` 文末）
- 状态：**没有可提交的 `src/` 改动**。第 1 节是「今天唯一入口在哪」，第 2 节是「为什么不能只改运行时」，
  第 3 节是停下报的那一条。

## 1. 唯一能把 `live` 减一的地方，今天只能从收集器进

| 位置 | 做了什么 | 减少 `live`？ |
|---|---|---|
| `src/core/rtdata/foreign.cpp#ForeignResourceControlBlock::release` | `refCount--`，到 0 时 `releasedControlBlocks++`、`liveControlBlocks--`、`delete` | **是（唯一一处）** |
| `src/core/rtdata/foreign.cpp#ForeignResourceControlBlock::releaseFromWrapperFinalizer` | `finalizedWrappers++` 后调 `release()` | 是，**只从收集器进** |
| `src/core/rtdata/foreign.cpp#ForeignResourceObject::finalize` | 收集器发现 wrapper 已死 → `mm::deferRelease` → 上一行 | 是，只由收集器调用 |
| `src/core/rtdata/foreign.cpp#ForeignResourceControlBlock::releaseReference` | `release()`；调用方只有 `createWithControlBlock` 的分配失败与 `disposeAllForeignResources` | 否（后者 `retain→dispose→release` 净零） |
| `src/core/rtdata/foreign.cpp#ForeignResourceControlBlock::cloneOrRetain` | 只 `retain()` | 否 |

**所以 `live` 今天的定义是「退出时还没有被任何一次收集看见的死 wrapper」，不是「持有者还剩几个」。**
`fvm`/`jit` 稳读 2、`nvm` 稳读 0，都是这个定义的函数：前者收集少跑一次，后者刚刚好跑够
（`docs/handover/reports/2026-11-15-count-at-zero-before.cn.md` 表一、表二）。

## 2. 为什么「归零即释放」不能只改运行时：计数长在 wrapper 上，持有者在槽里

一块的 `refCount_` 起于 1（`include/camel/core/rtdata/foreign.h#ForeignResourceControlBlock`），这一份跟着
**一个 wrapper 对象**走；而一个值在运行时是在**槽**（`slot_t`，`include/camel/core/slot.h`）之间搬的，
槽上没有任何计数：

- `include/camel/core/context/frame.h#Frame`：`Frame::set` 只写 `dynamicArea_[idx] = slot`；
- `include/camel/core/rtdata/tuple.h#Tuple` 与 `include/camel/core/rtdata/struct.h#Struct`：`set` 只写 `data_[index]`；
- `include/camel/core/rtdata/array.h#setSlot` / `#setSlotSlow`；
- **整体拷贝不经过上面任何一个**：`include/camel/core/rtdata/aggregate.h#aggregateSlotsMut` 是按字节取槽数据的入口，
  `include/camel/core/rtdata/func.h#Function` 的 `clone` 深拷贝闭包 tuple，
  `src/core/rtdata/foreign.cpp#ForeignResourceObject::clone` 再 clone 一次资源。

结论：`let b = a` 之后两个槽指同一个 wrapper，计数还是 1；闭包捕获把句柄放进闭包 tuple，计数还是 1。
**要在归零那一刻释放，就必须让计数等于槽的数目**：每一个**写槽的地方**加一、每一个**被覆盖或清掉的地方**减一，
并让产出处那一份由计划在「最后一次使用」时交回（作者裁决第 3 条）。这不是一处改动：`Frame::set` 是热路径，
四个后端各有一份槽写入，聚合还有整体拷贝路径，槽上还得有一个「这个槽可能装句柄」的静态标志（否则每次写槽都要
认一次对象种类，10.5 的「小型逻辑计算不得因此变慢」不答应）。

## 3. 半套的版本会把还活着的盒子放掉（这是停下的理由）

`docs/handover/reviews/2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md` 的「实测」一节
已经量过只改运行时的结果：在 `Frame::set` 加「新的 +1、旧的 −1」、在 `FramePool::release` 每个句柄槽 −1，
四个后端**同时**读 `boxes_are_counted` `created=1001 released=0 live=1001`（差的正是产出处那一份），并且
`value.counter_closure` 与 `value.ref_one_cell_two_holders` **四个后端一起变红** —— 两个用例都把句柄**捕获进
闭包**，捕获那一次没有计数，于是「槽清空」不等于「持有者没了」。

这两个用例今天是绿的，不是绿的错：`test/plans/feat/consistency/values.plan.toml -Jobs 8` 在这一棵树上
`total: 144, pass: 144, fail: 0`（八个条目 `value.counter_closure.*` 与 `value.ref_one_cell_two_holders.*`
各四后端，全 `[PASS]`）。所以那四条红是真的被半套的计数弄红的，不是它们本来就红。

按任务边界那条：「**少留一个的代价是崩溃**」。所以本车道没有把半套的计数提交上去：它能让这一格变绿，靠的是
提前释放还活着的盒子。**停在这里报。**

## 4. 要做的话，这是工作清单（每一条都不是本车道能独立验证的小步）

1. 计划侧：`src/execute/exec_plan.cpp#planStorageReuse` 已按执行顺序算了「谁在谁之后」，在它上面补一张
   「每个 topo 位置之后要交回的引用」表（`ExecPlan` 加一个与 `storageReuseAt` 同形的数组）。
2. 核心内存层：`retainCountNow` / `giveBackCountNow` 两个原语，作用于**槽里的值**，四个后端共用。
3. 四个后端的槽写入与聚合写入走同一组入口：`Frame::set`、`include/camel/core/context/frame.h#clearGcSlots`、
   `src/passes/sched/taskflow/taskflow_frame_pool.cpp#TaskflowFramePool::release`、`Tuple::set`、`Struct::set`、
   `Array::setSlot`、`Variant`/`Tagged`，以及 `aggregateSlotsMut` 的整体拷贝路径。
4. `ForeignResourceObject::finalize` 在块已被计数释放后必须成为空操作，否则 wrapper 的 `control_` 会指向已
   `delete` 的块（今天不会，因为归零只发生在收集里）。
5. 验收按作者那一页：`live` 不随次数增长，**且** `counter_closure`、`ref_one_cell_two_holders` 仍绿。

## 5. 这一页没有做的事

没有改 `src/`、`test/`、期望值，没有动 `live <= 3` 那句断言，没有加 `known_fail`，没有删 prl 臂。
`test/plans/feat/compile/ref.plan.toml` 那一格前面的注释仍在，因为让它成立的那件事没做。