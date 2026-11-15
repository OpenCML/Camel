# 先让「要交回几份」成为可读的表，再谈归零

- 日期：2026-11-15
- 车道：`lane-refcount`（工作树 `D:\Projects\Camel-wt\refcount`，基线 `df057ee8a`，并入答复 `4716dd04c`）
- 前情：`docs/handover/reports/2026-11-15-count-at-zero-before.cn.md`（修前四后端读数）、
  `docs/handover/reports/2026-11-15-count-at-zero-has-no-entry.cn.md`（唯一减一入口与「为什么不是小改动」）
- 依据：原则 10.5；作者 2026-11-15 的裁决
  （`docs/handover/reviews/2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md` 文末）
- 状态：**计划侧的表算出来了，也能读出来；没有任何后端读它，没有任何引用被交回，读数逐字节不变。**

## 1. 表是什么

`include/camel/runtime/exec_plan.h#ExecPlan` 上多了一组字段（`refProducedAt`、`refCopiesAt`、
`refKeptAt`、`refGiveBackOffsets`、`refGiveBacks`），由
`src/execute/exec_plan.cpp#planRefGiveBacks` 在 `planStorageReuse` 之后算，一次走查，结论是：

| 字段 | 含义 |
|---|---|
| `refProducedAt[i]` | topo 位置 i 的节点**造出**了这个值（不是入口/出口端口、不是常量、不是只做转交的 JOIN/GATE/端口） |
| `refCopiesAt[i]` | 这个值除了自己那一份，还要**多拿几份**（`uses - 1`；被程序保留时是 `uses`） |
| `refKeptAt[i]` | 程序自己留下了这个值（它是这张图的返回值） |
| `refGiveBacksAt(i)` | 位置 i 的节点跑完之后，**回来**哪些引用（`value`、`at`、`count`） |

规则来自作者裁决：产出节点的值由它的**每个使用者各持一份**，**最后一次使用**接走产出那一份，
其余使用是**复制**（加一）；每个使用跑完交回一份。写成引用就是
`produced + copies = give-backs + kept`，`ExecPlan::refBalanceHolds` 量的就是这一条。

一张图里能看见的持有者不止「读几次」这一种，所以走查还要处理三种情况：

- **JOIN 交出去的是一臂的值**，所以 JOIN 的读算在两臂的值身上（哪一臂跑，就是那一臂的引用回来）；
- **端口**同理，它交出去的是它拿到的那个值；
- **把值打进另一个值的一次使用，不等于这次使用一结束就不用它了**：`Fill` 造出闭包 tuple 或聚合，
  被装进去的值由那个新值持有，直到**那个新值自己死**。所以这一份回来的位置是「打包者的最后一个引用
  死掉的位置」，不是 `Fill` 的位置——表里这类行标 `packed`，份数单占一项。

第三条正是 2026-11-15 半套计数放掉活盒子的那一条：闭包捕获了句柄，造出句柄的槽清空了，计数归零，
而闭包还拿着它（`docs/handover/reports/2026-11-15-count-at-zero-has-no-entry.cn.md` 3 节）。把它单列
一项而不是并进 `copies`，是为了让「哪一份是捕获的」在读数上指得出来。

## 2. 怎么读：`test::plan_refs`

`src/passes/test/plan_refs.cpp#PlanRefsTestPass`（`test::plan_refs`，纯测试遍，不在任何默认流水线里）
把表按图打印，形如：

```
PLAN_REFS main produced 11 copies 4 give-backs 14 kept 0 packed 1 tail 14 tail-kind 0 balanced yes
PLAN_REFS main at 1 value 1 kind 11 copies 2 kept 0 uses 3
PLAN_REFS main at 12 packed 1 x1
```

第一行是**这张图的平衡**（`produced + copies = give-backs + kept + packed`，`balanced` 由打印出来的
五个数重算，不由计划自报）；`at <位置> value <ref>` 是产出位置与它多拿的份数；`at <位置> give <ref> x<n>`
是这次使用结束就回来的引用，`at <位置> packed <ref> x<n>` 是打包者死了才回来的引用。`tail-kind` 是返回
值的节点种类（`0` 是常量，`8` 是调用，`11` 是算子），它解释「为什么 `kept` 是 0」：`main` 的返回值是
个常量，所以它算出来的值一个都不外留。

**捕获那一份指得到具体一行**：`ref_one_cell_two_holders.cml` 的 `main` 里，被捕获的值是 `1`，
捕获它的 `Fill`（闭包 tuple）在位置 3，而 `1` 的那一份 `packed` 行在**位置 12**——闭包最后一次被用的
地方。加表时它是 `give 1 x1` 在位置 3（跟着那次使用一起还），现在读数变成 `give-backs 14 packed 1`：
总数没变（15），变的是「那一份什么时候还」，而这正是半套计数放早的地方。计划里钉了这条：
`captureAt > fillAt`。

`test/plans/feat/tools/plan_refs.plan.toml` 钉了两条读数（同一个程序跑两遍、两遍字节相同）：

| 程序 | `main` 的读数 | 说明 |
|---|---|---|
| `ref_boxes_made_per_call_ten.cml` | `produced 6 copies 0 give-backs 6 kept 0 packed 0` | 六个值，每个读一次或没人读，所以 `copies` 是 0；`__root__` 是 `1 copies 0 give-backs 0 kept 1 packed 0` |
| `ref_one_cell_two_holders.cml` | `produced 11 copies 4 give-backs 14 kept 0 packed 1` | **一个被读三次的值多拿两份**（其中一次是闭包捕获，那一份 `packed`），另有被保留的图读 `1 copies 1 give-backs 1 kept 1` |

第二条是这张表存在的理由：计数要跟持有者走，而「读三次」在表上就是 `copies 2`，且其中一份的去处
是一个 `packed` 行。纸面上先对，运行时就才有东西可以照做。

## 3. 平衡式在每一个用例上都成立（`plan_refs.every_case_balances`）

平衡式是**走查的性质**，不是某个程序的性质，所以 `scripts/check-plan-refs.mjs` 拿它扫全树：每个
`test/cases/**/*.cml` 跑一遍 `camel <case> test::plan_refs`，逐图检查。

```
PLAN_REFS_SWEEP cases=3776 read=2571 graphs=23993 unbalanced=0 skipped=1205 silent=0
every graph of every case that compiles balances
```

- `read` 是读得到表的用例（2571），`graphs` 是这些程序的图总数（23993），**`unbalanced=0`**；
- `skipped=1205` 是**不带自己计划那套参数就编不过**的用例（需要 `--coll`、入口参数之类），它们这里
  读不到，所以既不算「平」也不算「不平」；这条限制写在这里，别当成已经验过；
- `silent=0` 是「编过了却一个字没打」的用例，那是缺陷（遍必须为它拿到的每个程序说话），现在是 0。

## 4. 这张表**不**决定什么（也是没有任何后端读它的原因）

闭包捕获与聚合打包（`Fill`）已经进表了。还剩下两类边上看不见的持有者：

- **被调方**保留实参：那是被调方那张图自己的事，它自己的表会为自己的捕获说话，但实参**跨图**这一份
  现在没有名字；
- **后续的图**读到的值（一个存起来的 `Ref` 句柄）在这里根本没有名字。

所以按表交回，这两类还会交早。表是**描述**，不是结论；下一班要做的就是把这两类接进同一套计数
（`retainCountNow` / `giveBackCountNow`），那时这张表才会被四个后端读。

## 5. 为什么另起一次走查，而不是并入 `planStorageReuse`

`src/execute/exec_plan.cpp#planStorageReuse` 已经在执行顺序上算了「谁在谁之后」，看起来该长在一起。
没有合的原因是可验证性：**「只加表」这件事的判据是读数逐字节不变**，而一次改了两件事的走查，
读出来变了也分不清是哪件。所以 `planRefGiveBacks` 是独立的一次，读同一条
`camel/execute/graph_runtime_support.h#executionPredecessorsOf`（「谁的值先跑」的唯一答案），
各自写各自的表；等真要执行释放那一天，两者再合。

## 6. 下一班：这份表怎么用

1. 先把剩下两类持有者（被调方保留实参、跨图读到的值）接进计数，或先把它们**排除**在表外并让
   执行处拒绝（宁可「不释放」也不要「早释放」）。
2. 核心内存层两个原语：对**槽里的值**加减，四个后端共用。
3. 每个后端在计划给的位置上交回：表里 `refGiveBacksAt(i)` 就是「位置 i 跑完之后要还的东西」，
   `packed` 行是「打包者死掉时再还的那一份」，两者都要有。
4. `src/core/rtdata/foreign.cpp#ForeignResourceObject::finalize` 在块已被计数释放后必须成为空操作。

## 7. 这一页没有做的事

没有改 `planStorageReuse`，没有加任何计数，没有动 `live <= 3`、没有加 `known_fail`、prl 臂仍在，
`test/plans/feat/compile/ref.plan.toml` 那一格前面的注释保持原样。