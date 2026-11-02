# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。

## 现在在等作者：10 页

| 页 | 问什么 | 状态 | 裁定 | 还堵着 |
| --- | --- | --- | --- | --- |
| [`2026-11-02-is-with-grad-a-stub`](2026-11-02-is-with-grad-a-stub.cn.md) | `with_grad` 按清单记为 stub，实测是"值与梯度"同 `grad` 一致：按"不是 stub、用例已补"结案，还是清单指的形态另有其处 | 待裁 | — | 中-12 的结案口径 |
| [`2026-11-02-the-generator-reads-a-draft-node-after-its-builder`](2026-11-02-the-generator-reads-a-draft-node-after-its-builder.cn.md) | `stream.*` 在 Windows 上崩在 `builder_support.h#nodeGraphOf`（草稿节点不属于任何 compile graph builder），同树 Linux 全绿、五个后端都崩，所以是建图不是调度；问该谁去找那个节点的 owner、修好前 Windows 腿要不要继续红 | 待裁 | — | Windows 腿的红；那个节点的 owner |
| [`2026-11-02-who-restores-order-after-inline-moves-a-statement`](2026-11-02-who-restores-order-after-inline-moves-a-statement.cn.md) | 内联改变块归属之后，次序该由内联自己补还是由图定型后的统一通道补 | 待裁 | — | 次序的归属：内联还是定型后的通道 |
| [`2026-11-03-may-a-printed-name-be-the-definitions-own`](2026-11-03-may-a-printed-name-be-the-definitions-own.cn.md) | `tensor:sum` 与 `tensor:sum_axis` 共用一个写名 `sum`，边界在哪只有定义知道、节点不知道；问打印件能不能用定义自己的名字（`sum_axis(m, 0, false)`），还是必须复现源里的 `<>` 写法 | 待裁 | — | 打印器的往返：节点上只有一个写名 |
| [`2026-11-02-who-updates-the-root-table-when-the-root-is-replaced`](2026-11-02-who-updates-the-root-table-when-the-root-is-replaced.cn.md) | 宏重写换根时谁负责更新根表；`nn` 那两条 bytecode 用例的根换过之后旧根还在表里 | 待裁 | — | `gru_lite_large_bytecode`、`residual_mlp_large_bytecode` 两条 poison 红 |
| [`2026-11-02-what-carries-the-residuals`](2026-11-02-what-carries-the-residuals.cn.md) | 残差用什么承载（三条路各写过一遍）；`split` 不再交出残差元组之后，闭包里俘获的残差该由谁持有 | 待裁 | — | 残差藏进闭包这条线的入口 |
| [`2026-11-02-can-a-sync-place-be-derived-from-the-graph`](2026-11-02-can-a-sync-place-be-derived-from-the-graph.cn.md) | 同步链的"位置"（`kGCNodeFlagSyncStatement` 记的那个事实）能不能从图上推出来：照 07:30 的谓词实现，终点侧 1594 个终点落在三条之外、全量 720 红，起点侧照字面 185 红 | 待裁 | — | 标记能否删；B 要重写 `edges.cn.md` 一节 |
| [`2026-11-02-can-the-pow-rows-be-built-from-a-base`](2026-11-02-can-the-pow-rows-be-built-from-a-base.cn.md) | `:op/pow_*` 那一族 12 行能不能由一条底算子（`pow_bf`/`pow`）生成，还是每行都得自己写 | 待裁 | — | 那 12 行的归属 |
| [`2026-11-02-is-the-cpp-backends-elementwise-comparison-missing`](2026-11-02-is-the-cpp-backends-elementwise-comparison-missing.cn.md) | `std::cpp` 下数组 `==`/`!=`/`===` 与逐元素算术的现状，是没翻译还是翻译错 | 已答：`==`/`!=` 对，`===` 与逐元素算术都没有翻译 | — | `std::cpp` 的逐元素层 |
| [`2026-11-02-who-reports-a-lost-capture`](2026-11-02-who-reports-a-lost-capture.cn.md) | 捕获照抄不了的时候（类型没有批量形式、或调用链上拿不到值），该由变换按名字拒绝，还是由一条与变换无关的"值与它的图捕获对得上"检查来报 | 待裁 | — | 报错的归属：变换还是独立检查 |

**这张表由 `node scripts/check-doc-links.mjs --questions-index` 对着目录核对**：行数与页数不符、或者某一行指向目录里没有的页，它都会红。

> **一条裁定有没有落地，看代码和提交，不看这张索引**（作者 2026-11-02 08:10）。"已裁已落/已裁未落"那张状态表整节删掉了：维护一张追着代码改的表，本身就是一份没人该维护的台账。审查清单上的条目修完，写一行"已修，提交号"在那页里。


## 裁决里点名要我们做的三件返工
22:50 的裁定点名了三件容易读漏的返工（不是新问题，是那一笔要求我们改的现有东西）：
1. **去两个环境变量开关**：`CAMEL_RULESET_INDEX`（`program.cpp:662`）与 `CAMEL_TILES_TEST_REASONS`（`region_test_pass.cpp:302`）都违反"不加环境变量开关"；改成正式遍/参数——规则集索引做成一个 `std::rulesets` 之类的翻译遍，tiles 做成测试遍的参数打印原因，用例钉住读数。
2. **删 `taskflow.cpp:2797` 那条 `DESIGN-QUESTION`**：并行 `map` 的裁定已给（保实现、3.6 改措辞），标记随之删；补部分故障、四后端同判的用例。
3. **`reinterp<jvp>` 是第一优先的消费者**：规则集索引的下一笔不是"等变换算子"，就是 `reinterp<jvp>` 按索引逐个原语替换、缺规则报 9.9；不另造检查。
这三件都记在 `301c3790e` 的 22:50 节里；这里单列，因为它们最容易被读成"背景"。
## 已撤：1 页
| 页 | 问什么 | 状态 | 裁定/去向 | 还堵着 |
| [`where-the-tag-for-an-operator-can-sit`](../reviews/questions-2026-10-31-where-the-tag-for-an-operator-can-sit.md) | 标签能不能挂在别处的算子声明上 | 已撤 | 20:50——没有跨模块挂标签这回事；结论并入上面 `the-ruleset-module-has-no-rules-in-it` 那一行 | 无 |
| [`questions-2026-11-02-what-does-tq-match`](../reviews/questions-2026-11-02-what-does-tq-match.md) | `T[?]` 匹配什么 | 已裁（作者 2026-11-02 11:10） | `T[..]` 匹配任意秩、`T[?]` 只匹配秩 1 | 一条还没兑现的读数欠账（页里记着） |
## 留档里仍写着"待裁/等裁决"的页（已裁，别再当待裁）
- [`reviews/questions-2026-10-30-cell-type.md`](../reviews/questions-2026-10-30-cell-type.md)：页首说"已由作者答复…不再更新"，第 5 行却还写"裁定：待裁"；16:30 已裁（只求类型的推导）。
- [`questions-2026-10-31-how-an-indirect-call-hands-back-a-version`](../reviews/questions-2026-10-31-how-an-indirect-call-hands-back-a-version.md)：页内写"等作者裁决"，21:35 已裁并已落。
- [`questions-2026-10-31-what-carries-a-wait-for-a-write`](../reviews/questions-2026-10-31-what-carries-a-wait-for-a-write.md)：页内写"等作者裁决"，20:35 已裁。
- **22:50 裁完的四页**（[`a-version-port-that-must-carry-a-value`](../reviews/questions-2026-10-31-a-version-port-that-must-carry-a-value.md)、[`two-references-write-one-object-outside-a-sync-block`](../reviews/questions-2026-10-31-two-references-write-one-object-outside-a-sync-block.md)、[`parallel-map-fault-what-runs-after`](../reviews/questions-2026-10-31-parallel-map-fault-what-runs-after.md)、[`who-consumes-the-ruleset-index`](../reviews/questions-2026-10-31-who-consumes-the-ruleset-index.md)）：页内还写着"要作者答的一句"，已裁未落，别再当待裁。
这些页的页内状态是它主人的活，本索引只记判断，不改页。**页内状态可能落后于索引；以索引为准**——下一个人别读到页里的"待裁"就去问一个已经答过的问题。
## 不是问题、只是记录
[`region-count-account`](../reviews/questions-2026-10-30-region-count-account.md)、[`two-behaviour-diffs-left`](../reviews/questions-2026-10-30-two-behaviour-diffs-left.md)（页首自己写明是记录）、[`output-order-jitter`](../reviews/output-order-jitter.cn.md)、[`random-red-unused-pure-result`](../reviews/random-red-unused-pure-result.cn.md)（两份现象报告，12:35 已裁；后者由 `ca94c1759` 修）。它们不占待裁位。
## 报告里提到"待裁/待定/未定"但不是在等作者
`code-review-types`、`principles-audit-2026-10-26`、`code-review-indexing`、`code-review-index`、`code-review-macro-modules-gc`、`code-review-onnx-tiles`、`code-review-python`、`defun-dispatch-notes`、`gpu-g4-g5-plan`。这些词是技术用语（未定类型、未定 extent、仍未定的机制），没有一页在等作者；要等裁定的问题一律单独成页。
## 原则第 15 节的 8 条：2 条已成页，6 条还在原则里
判据同上：只有今天有人被它堵住的才值得成页，其余留在这里，等它真的挡住一条车道再动。原文在 [`docs/design-principles.cn.md`](../../design-principles.cn.md) 第 15 节。
| 条目 | 问什么 | 今天堵着谁 |
| 类型运算 | 类型表达式要不要有交、差、并这类运算（文法里没有，也没预留运算符） | 无；要加的时候写法和含义一起定 |
| `typeof` 常量 | `typeof` 作用于常量值时的含义，以及它在泛型里的用法 | 无；GCT 只用 `typeof <变量>`，口径已按 16:30 定成"只求类型" |
| 类型函数失败 | 类型函数求值失败或不终止时怎么办 | 无；`Tangent`/`ParamsOf` 才刚开始写，还没人写坏过 |
| 块外多处引用写 | 同一可变对象在同步块之外被两条引用写，按什么排 | **已裁**（22:50，见"已裁"表）；与 `a-version-port-that-must-carry-a-value` 相邻但不是同一问 |
| 补贴标签 | 能不能在别处给一个声明补贴标签，调度是否例外 | 无；20:50 已把内置原语的规则定到规则集模块 |
| 并行 map 故障 | 并行 `map` 出故障后，下标更小、还没开始的元素是否仍执行 | **已裁**（2026-10-31，`301c3790e`）：取①——保实现、改 3.6 措辞；代码里的标记已删，用例 `attempt.partial_fault` 钉住 |
| 并行 map 故障 | 并行 `map` 出故障后，下标更小、还没开始的元素是否仍执行 | **已成页**（待裁，见上表）；`code-review-backends.md:37` 记着代码里还留着 `DESIGN-QUESTION(parallel-fault-lower-elements)` 标记 |
| 并行 map 故障 | 并行 `map` 出故障后，下标更小、还没开始的元素是否仍执行 | **已裁**（22:50，见"已裁"表）；`code-review-backends.md:37` 那条 `DESIGN-QUESTION` 要删（见"返工"一节） |
| 算子实参装入 | 内建算子的实参是否自动装入（`xs->append<5>`，`xs: (int?)[]`） | 无车道被堵；但审计 A25 记着今天要写 `5 as int?` |
| 源码声明流水线 | 是否允许入口文件写出 `pipeline std::macro, tiles::compile, std::nvm` | 无；流水线仍只在命令行拼 |
| GIR handle 名字什么 | 编译期 `node_handle_t` 名字一个事务（与 `NodeView` 同型）还是一张草稿 | **已成页**（待裁）；**堵 b6 的最后一块**：`nodeDraftOf` 66 个 + `draft()` 132 个调用点；**同页第二问**：region 的 id 能不能在 extract 时就分配（`tiles` 那 9 处等它） |
## 裁定去哪找
- 本批 15 页的裁定：[`reviews/questions-2026-10-29.md`](../reviews/questions-2026-10-29.md)，按时间排，末几节是 15:30–22:10。
- 22:50 的四页裁定（版本口 / 看不见的写 / 并行 `map` / 规则集索引）在同一文件末节，提交 `301c3790e`。
- 更早的批量复核：[`questions-2026-10-23`](../reviews/questions-2026-10-23.md)、[`-24-evening`](../reviews/questions-2026-10-24-evening.md)、[`-26-late`](../reviews/questions-2026-10-26-late.md)、[`-29`](../reviews/questions-2026-10-29.md)、[`-30-jax`](../reviews/questions-2026-10-30-jax.md)、[`-30`](../reviews/questions-2026-10-30.md)。
- 单条裁定留档：`reviews/questions-2026-10-30-*.md` 与 `-10-31-*.md`，页首一般有 `裁定：` 一行。
## 写页的规矩
- 一页一问，文件名 `questions-YYYY-MM-DD-<主题>.md`。
- 三段：现象（`file:line`）、两条路的代价、我方默认建议；末尾一句要作者答的话，选项要能照字面执行。
- 裁回来：`git mv` 到 `reviews/`，页首写 `裁定：`（结论 + 落在哪个提交）；`questions/` 空 = 没有待裁。
- **已裁的页必须带 `裁定：` 行**，否则读者不知道是哪个裁定；`who-consumes-the-ruleset-index` 落进 `reviews/` 时就是这么做的（好先例）。
