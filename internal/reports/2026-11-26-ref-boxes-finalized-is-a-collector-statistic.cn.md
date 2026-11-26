# `ref_boxes_*` 的 `finalized` 差 1–2：收集器的统计，不是两个后端的语义差

**一句话结论**：`FOREIGN_RESOURCES` 行里的 `finalized=` 数的是"收集器 finalize 掉的包装对象个数"，
它的值取决于**收集器在哪些点收集**，不是程序的函数；同一后端只换收集频率就自相矛盾（9 与 11）。
所以这一族既不是哪一侧的缺陷，也不是数值噪声，而是**把收集器的统计当程序输出打印了**。
本该由负责人裁定"它算不算程序可观察输出"；裁定之前不改引擎、不改用例、不改断言。

## 一、两份读数（同一轮、同一套旗标，最小用例 `ref_boxes_loop_ten`）

旗标取自该用例自己的计划条目（`test/plans/feat/compile/ref/ref.plan.toml`）：
`--gc-stress alloc=7,safepoint=3,kind=both --gc-check foreign,verify=both --gc generational`。

```
nvm  FOREIGN_RESOURCES created=11 counted=11 released=11 live=0 disposed=0 finalized=9  creations=11 checks=11 found=11 missing=0 rows=0 rows_seen=0
fvm  FOREIGN_RESOURCES created=11 counted=11 released=11 live=0 disposed=0 finalized=11 creations=11 checks=11 found=11 missing=0 rows=0 rows_seen=0
```

五个程序可观察字段两侧**逐字相同**：`created=11 counted=11 released=11 live=0 disposed=0`。
盒子生命周期一致：`released=created`、`live=0`，没有一个盒子漏掉。只有 `finalized` 差 2。

差分读数里这一族共五条（`nvm` / `fvm`）：

| 用例 | nvm `finalized` | fvm `finalized` |
| --- | --- | --- |
| `ref_boxes_loop_ten` | 9 | 11 |
| `ref_boxes_made_per_call_ten` | 9 | 11 |
| `ref_boxes_are_counted` | 1000 | 1001 |
| `ref_boxes_loop_thousand` | 999 | 1001 |
| `ref_boxes_made_per_call_thousand` | 999 | 1001 |

## 二、判决实验（本页最硬的一条）

同一个 `camel` 二进制、同一条用例，只换 **nvm 自己的收集频率**：

```
nvm  alloc=7, safepoint=3   →  finalized=9
nvm  alloc=1, safepoint=1   →  finalized=11      ← 与 fvm 相同
```

`alloc=1,safepoint=1` 是"每次分配、每个安全点都收一次"。它把 `finalized` 顶到 11，说明差别不在
"谁多算/少算"，而在"死掉的包装对象有没有赶上一次收集"。换言之：**`finalized` 是收集时间表的函数**。

## 三、行号链（为什么它不是某一侧的执行路径写错）

`finalized` 只有一个 `++` 点，两个后端共用：

- `src/kernel/core/rtdata/foreign.cpp:109-112` —— `ForeignResourceControlBlock::releaseFromWrapperFinalizer()`：
  `counters().finalizedWrappers.fetch_add(1, …)`，随后 `release()`。
- `src/kernel/core/rtdata/foreign.cpp:71-73` —— 它唯一的调用者 `releaseControlBlockAfterCollection()`，
  由收集器的**延迟释放队列**在一次收集之后调用。
- `src/kernel/core/rtdata/foreign.cpp:438-450` —— `ForeignResourceObject::finalize()`：收集器在收集内、
  分配器上锁时调用，只投递 `mm::deferRelease(&releaseControlBlockAfterCollection, control)`，
  不在锁内跑外部删除器（否则会死锁）。
- `src/kernel/core/mm/deferred.cpp:33`、`:52` —— `deferRelease()` / `runDeferredReleases()`。
- `src/kernel/core/mm/gen.cpp:432`、`:457`、`:480` —— 队列在**每次收集之后**被排空。
- `src/kernel/core/context/context.cpp:327` —— 上下文析构时再排空一次；这一步发生在 CLI 打印**之后**。
- `src/tools/camel-cli/main.cpp:310-332` —— `~ReleaseExternalResources()`：`:316` 取
  `foreignResourceStats()` 快照，`:320-328` 打印这一行。**读统计的只有这一处**，两侧同一条代码。
- `src/kernel/core/rtdata/foreign.cpp:483-497` —— `foreignResourceStats()`，纯快照，不含任何判据。

结论：不是"两后端各有一份 finalize 路径"，而是**同一个计数器**；差的是"收集器跑到哪一段"。

## 四、为什么"两侧应同值"这条理由不成立

要 `finalized` 同值，就必须要求两个后端**在同一些点收集**——收集时机由分配节奏决定，
`docs/design-principles.cn.md` 没有这条要求（原理对 finalizer 计数是沉默的；判 #2–#6 时已逐条核过）。
而程序可观察的那五个字段已经一致，说明"盒子的寿命"这一语义两侧相同。

所以本页不给这个差额一个"正常/异常"的说法：它算不算程序输出，由下面的问题决定，不由数值决定。
用例打印了一个收集器统计，于是"同一后端换个收集频率就自相矛盾"本身已经回答了这个问题的一半。

## 五、待答的问题（负责人原话）与两个分叉

**"程序退出时，已释放的外部资源是否保证都执行完终结"**

两个分叉，各自由这句话的一个答案决定：

1. **若保证** ⇒ 退出时 `finalized` 应当等于 `released`；那么 nvm 在宽松收集时间表下少两块就是**缺陷**
   （`alloc=7,safepoint=3` 给 9，`alloc=1,safepoint=1` 给 11，而两次 `released` 都是 11）。
2. **若不保证** ⇒ `finalized` 就是收集统计，不是程序的答案 ⇒ 差分把它排除在比较之外
   （与"追加的 pass 名""测量的耗时"同类：读作占位符，其余字段照常比较）。

证据（本页一至三节，也是这句话的材料）：判决实验（同一二进制、只换收集频率：9 / 11）、
五个程序可观察字段两侧逐字相同（`created=11 counted=11 released=11 live=0 disposed=0`）、
以及"正常计划全绿、只有在 `--gc-stress` 的时间表下才出现这个差"。

## 六、状态与后续

- `internal/backlog.md` 的 `D-1126-150` ① 已记："`finalized` 计数属语义、问负责人、先挂，
  那几条照现在 `backends_agree` 不动"；`D-1126-176` 维持先挂、不动引擎。本页是该条的材料页。
- 裁定之前：引擎不改、用例不改、金标不改、`ref.plan.toml` 的 `backends_agree` 不动。
  差分侧已按裁定把这 5 条记为 `pending`（不计入 `differ`，数值仍在报告里），见 `lane/fvm-diff`。
- 裁定之后的落点：分叉一 ⇒ 执行层（`main.cpp:316` 取快照之前的那一处）；分叉二 ⇒ 工具一处
  （`finalized=<n>` 当占位符）。两条都不是本页能自行决定的。

## 七、复核方式（两分钟，同一个二进制）

三次都在**同一个二进制**上跑：本树 `out/latest/bin/camel`（走 `camel-remote` 时是
`$R/out/latest/bin/camel`，同一棵树、同一次构建）。三次之间**不重建、不换树、不换构建配置**，
只换 `--gc-stress` 的参数——复核一旦引入新变量，9 与 11 的对照就说明不了问题。

```
# ① 两侧各一次，旗标与该用例的计划条目一致
<camel> --gc-stress alloc=7,safepoint=3,kind=both --gc-check foreign,verify=both \
        --gc generational <case>/ref_boxes_loop_ten.cml std::nvm
<camel> … std::fvm
# ② 判决实验：同一个二进制，只换 nvm 自己的收集频率
<camel> --gc-stress alloc=1,safepoint=1,kind=both --gc-check foreign,verify=both \
        --gc generational <case>/ref_boxes_loop_ten.cml std::nvm
```

三次的 `FOREIGN_RESOURCES` 行应当分别是 `finalized=9`、`finalized=11`、`finalized=11`；
`<camel>` 三处写同一个绝对路径。