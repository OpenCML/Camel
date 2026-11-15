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

一张图里能看见的持有者不止「读几次」这一种，所以走查还要处理一种情况：**JOIN 交出去的是一臂的值**，
所以 JOIN 的读算在两臂的值身上（哪一臂跑，就是那一臂的引用回来）；**端口**同理，它交出去的是它拿到的
那个值。这两处不处理，表的「谁持有」就会整体错一位。

## 2. 怎么读：`test::plan_refs`

`src/passes/test/plan_refs.cpp#PlanRefsTestPass`（`test::plan_refs`，纯测试遍，不在任何默认流水线里）
把表按图打印，形如：

```
PLAN_REFS main produced 11 copies 4 give-backs 15 kept 0 tail 14 tail-kind 0 balanced yes
PLAN_REFS main at 1 value 1 kind 11 copies 2 kept 0 uses 3
PLAN_REFS main at 3 give 1 x1
```

第一行是**这张图的平衡**（`produced + copies = give-backs + kept`，`balanced` 由打印出来的四个数
重算，不由计划自报）；`at <位置> value <ref>` 是产出位置与它多拿的份数；`at <位置> give <ref> x<n>`
是位置跑完后回来的引用。`tail-kind` 是返回值的节点种类（`0` 是常量，`8` 是调用，`11` 是算子），
它解释「为什么 `kept` 是 0」：`main` 的返回值是个常量，所以它算出来的值一个都不外留。

`test/plans/feat/tools/plan_refs.plan.toml` 钉了两条读数（同一个程序跑两遍、两遍字节相同）：

| 程序 | `main` 的读数 | 说明 |
|---|---|---|
| `ref_boxes_made_per_call_ten.cml` | `produced 6 copies 0 give-backs 6 kept 0` | 六个值，每个读一次或没人读，所以 `copies` 是 0；`__root__` 是 `1 copies 0 give-backs 0 kept 1` |
| `ref_one_cell_two_holders.cml` | `produced 11 copies 4 give-backs 15 kept 0` | **一个被读三次的值多拿两份**（闭包捕获是那三次读之一），另有被保留的图读 `1 copies 1 give-backs 1 kept 1` |

第二条是这张表存在的理由：计数要跟持有者走，而「读三次」在表上就是 `copies 2`。纸面上先对，
运行时就才有东西可以照做。

## 3. 这张表**不**决定什么（也是没有任何后端读它的原因）

- 把值**存进聚合**（数组元素、tuple 字段）是一次持有，边上看不出来：计划不知道 `:op/array_set`
  会把元素留在数组里；
- **闭包捕获**是同一个问题，`Function` 深拷贝的是闭包 tuple（`include/camel/core/rtdata/func.h#Function`），
  捕获那一次加不加计数，表里现在没有一列；
- **被调方**保留实参也是它的图自己的事，另外那张图会为自己的保留说话；
- **后续的图**读到的值（一个存起来的 `Ref` 句柄）在这里根本没有名字。

所以这四条没解决的持有者，会让「按表交回」交早。表是**描述**，不是结论；下一班要做的是把这四种持有
接进同一套计数（`retainCountNow` / `giveBackCountNow`），那时这张表才会被四个后端读。

## 4. 为什么另起一次走查，而不是并入 `planStorageReuse`

`src/execute/exec_plan.cpp#planStorageReuse` 已经在执行顺序上算了「谁在谁之后」，看起来该长在一起。
没有合的原因是可验证性：**「只加表」这件事的判据是读数逐字节不变**，而一次改了两件事的走查，
读出来变了也分不清是哪件。所以 `planRefGiveBacks` 是独立的一次，读同一条
`camel/execute/graph_runtime_support.h#executionPredecessorsOf`（「谁的值先跑」的唯一答案），
各自写各自的表；等真要执行释放那一天，两者再合。

## 5. 下一班：这份表怎么用

1. 先把四种持有者（聚合存储、闭包捕获、被调方保留、跨图）接进计数，或先把它们**排除**在表外并让
   执行处拒绝（宁可「不释放」也不要「早释放」）。
2. 核心内存层两个原语：对**槽里的值**加减，四个后端共用。
3. 每个后端在计划给的位置上交回：表里 `refGiveBacksAt(i)` 就是「位置 i 跑完之后要还的东西」。
4. `src/core/rtdata/foreign.cpp#ForeignResourceObject::finalize` 在块已被计数释放后必须成为空操作。

## 6. 这一页没有做的事

没有改 `planStorageReuse`，没有加任何计数，没有动 `live <= 3`、没有加 `known_fail`、prl 臂仍在，
`test/plans/feat/compile/ref.plan.toml` 那一格前面的注释保持原样。