# 待裁索引：谁在等作者，堵着什么

> **一页一问；索引按"堵住多少工作"排序，因为作者的时间应该花在最贵的那页上。**

> **索引的价值在于与目录一一对应；一旦少一页，读者就会以为没有那一问。**

> **这个目录的机制**：作者把已经读过的页整体挪进 `reviews/`；`questions/` 只放**还在等回答**的页。所以 `questions/` 空不是没人在问，是问题都被读过了。新的页从车道落进来，落进来就在待裁表加一行；**现在 `questions/` 里有 6 页**（`gir-handle-names-a-transaction-or-a-draft`、`partial-destructuring-of-a-value-tuple`、`tiles-narrow-dtype`），其余五页已裁完、整体挪进 `reviews/`（`a-version-port-that-accepts-more-than-void` 于 23:50 裁：选 A）。
>
> **哪些未决值得成页**：不是所有未决都值得成页——只有**今天有人被它堵住**的才值得。其余留在原则里，等它真的挡住一条车道再成页。所以第 15 节那 8 条不占待裁表，清单在文末，每条写明今天堵着谁。

状态四种：**待裁**（在等作者）、**已裁已落**（裁定有，改动已在 develop）、**已裁未落**（裁定有，改动还没进 develop）、**已撤**（问题作废）。"还堵着"一栏写的是这条裁定今天还挡着哪些具体工作；已经落地的写"无"。
## 现在在等作者：0 页（4 页都已裁，3 页的改动在车道上）

`questions/` 里现在没有一页在等答复：4 页都被读过、都裁了，改动按裁定往 develop 走。新页从车道落进来时才在这个表里加行。

| 页 | 问什么 | 状态 | 堵着谁 |
|---|---|---|---|
| [`does-rt-array-carry-a-shape`](questions-2026-10-31-does-rt-array-carry-a-shape.md) | 05:30 裁 **A**：`rt::Array` 带 `rank`/`extent` | 已裁未落 | 守卫与“形状 + 嵌套打印”第一步已落；队列：下标 → 切片 → `reshape` → `shape` → `numel` → 逐元素 |
| [`which-results-cross-the-bridge`](questions-2026-10-31-which-results-cross-the-bridge.md) | 同一个 `T[..]`，两种表示：`tensor:*` 的结果走 `rt::Foreign`（05:50）还是原生带形状数组（05:30）；判据是"算子来自哪个模块"还是"形状是否具体" | 待裁 | 堵 `tensor:ones/zeros/reshape/load_npy` 与 `rand:*_array` 的表示，也堵 `std::cpp` 第 5 步 |
| [`std-cpp-needs-a-tensor-type`](questions-2026-10-31-std-cpp-needs-a-tensor-type.md) | 05:50 裁：**张量是 `rt::Foreign` 过桥**，走设计稿第 9 节，第 5 步开工 | 已裁未落 | 369 个程序记作“等第 5 步（外部对象）”；第一笔是 `rt::Foreign` 句柄 |
| [`mem-blocks-has-no-block-space`](questions-2026-10-31-mem-blocks-has-no-block-space.md) | 05:10 裁 **A**：拒绝并让拒绝说实话（目标不具备，不是尚未实现） | 已裁已落 | 无；普查把 10 个记为“目标不具备” |
| [`generator-narrowing-remains`](questions-2026-10-31-generator-narrowing-remains.md) | 04:50 裁 **A**：删掉生成器收窄 | 已裁已落 | 无；两处拒绝已删，三条形状转成消费它们的正例 |


原则第 15 节另列着 6 条未决（另 2 条已成页、22:50 已裁），见文末。

## 已裁：按还堵着的活排

| 页 | 问什么 | 状态 | 裁定 | 还堵着 |
|---|---|---|---|---|
| [`a-version-port-that-must-carry-a-value`](../reviews/questions-2026-10-31-a-version-port-that-must-carry-a-value.md) | `void` 调用写过的变量，它的读该靠什么后于那次调用 | 已裁未落 | 22:50——A：新算子 `:var/after(cell, call) : var T`（第二个实参接受 `void`），读写成 `:var/get(:var/after(cell, call))`；两条都是数据边，3.1 的"用到"沿数据边到调用；验收 `callback_write_ignored` 打印 `3`、`effects_syntax` 11 条红消失、拿 `133` 量 | `:var/after` 算子本身；`orderAfterLastWriter` 里那条 ctrl 替身随之可拆 |
| [`two-references-write-one-object-outside-a-sync-block`](../reviews/questions-2026-10-31-two-references-write-one-object-outside-a-sync-block.md) | 同一可变对象被两条引用写、第二条在这一层看不见时，读拿谁的版本 | 已裁未落 | 22:50——②：**保守的写者集合**（程序里所有写该变量的函数，建图前整个程序扫一遍）；`twice` 里 `counter` 在集合里，`return counter` 的版本是 `:var/after(counter, f(v))`；**20:35 那句就此收回** | 保守写者集合的实现；`twice` 应打印 `3` |
| [`parallel-map-fault-what-runs-after`](../reviews/questions-2026-10-31-parallel-map-fault-what-runs-after.md) | 并行 `map` 一处故障后，下标更小、还没开始的元素是否仍执行 | 已裁未落 | 22:50——①：保今天的实现，3.6 改措辞（"已经开始的元素跑完；下标大于已知最小故障、还没开始的元素不再开始，下标更小的仍会开始；报下标最小的故障"）；删 `taskflow.cpp:2797` 的 `DESIGN-QUESTION`；补部分故障、四后端同判的用例 | 3.6 措辞；`taskflow.cpp:2797`；四后端同判用例 |
| [`who-consumes-the-ruleset-index`](../reviews/questions-2026-10-31-who-consumes-the-ruleset-index.md) | 规则集索引建好了、打印得出来，但没有 pass 查它 | 已裁未落 | 22:50——`reinterp<H>` 就是消费者：`reinterp<jvp>` 按索引逐个原语替换，缺规则就在那个原语处报编译期诊断（9.9），验收是标量函数的 `jvp`；不另造"没有变换也报缺规则"的检查 | `reinterp<jvp>` 是第一优先的消费者；标量 `jvp` 验收 |
| [`the-ruleset-module-has-no-rules-in-it`](../reviews/questions-2026-10-31-the-ruleset-module-has-no-rules-in-it.md) | `autodiff` 算不算 `jvp` 那族的规则集模块，还是把前向规则搬进 `rulesets/jvp.cml` | 已裁已落 | 22:10——(b) 搬进 `rulesets/jvp.cml`；规则集就是 `stdlib/rulesets/` 下的模块，按路径前缀判断 | 无。已落地 `3bf5324e2`（索引键加元数）`67b4c17bc`（5 条前向进 `rulesets/jvp.cml`、5 条转置进 `rulesets/transpose.cml`）`13d385530`（语言参考三句）；`autodiff` 只留包装与切线函数 |
| [`jvp-family-anchor`](../reviews/questions-2026-10-30-jvp-family-anchor.md) | `#jvp` 这族规则没有挂点：补声明、挂标签、还是按算子 id 查表 | 已裁未落 | 09:35 ①，20:50 改成"内置原语的规则写规则集模块、按左边归属"，22:10 补上"搬规则" | 与上一行是**同一件活**；索引已落（`program.cpp:658-659` 按 `rulesets.` 前缀建），规则还没搬 |
| [`what-carries-a-wait-for-a-write`](../reviews/questions-2026-10-31-what-carries-a-wait-for-a-write.md) | 值在门后等一次写、或等一次调用，该由哪种边承担 | 已裁未落 | 20:35——3.1 已经答了，不加第四种边；三件事：门的等待=同步前驱、三族遍改读"数据使用+同步链"、然后停造 ctrl 重测 | **②a 停造 ctrl**；`simplify`/`unread_gate`/`dropped_effects` 三族遍；`gates.cpp:19` 的使能条件 |
| [`reinterp-rule-set`](../reviews/questions-2026-10-30-reinterp-rule-set.md) | 规则集 `H` 以什么形式交给算子 | 已裁未落 | 09:15——C：`H` 是编译期名字；规则集是一个模块；`transforms` 收 `reinterp`/`split`/`transpose` | **`reinterp` 还没实现**（`git grep reinterp` 只中 `reinterpret_cast`），`modules/transforms/` 只有骨架；第 3–5 步都等它 |
| [`a-version-port-that-accepts-more-than-void`](../reviews/questions-2026-10-31-a-version-port-that-accepts-more-than-void.md) | `:var/after` 的第二个实参只接受 `void`（按 22:50 的字面），还是任意值 | 已裁未落 | 23:50——A（比我们的主张更宽）：接受任意值，包括 `void`；第二实参不被读，只表示"这次调用之后的版本"；签名写成 `(var T, any) => var T`，语言参考写同一句 | 语言参考与签名注释写成 `(var T, any) => var T`（今天的实现已对） |
| [`generator-cell-type-by-inference`](../reviews/generator-cell-type-by-inference.cn.md) | 单元的类型由"只求类型的推导"定，还是由运行时状态的首个写入定 | 已裁未落 | 16:30——A：`typeof` 只用"只求类型"的推导 | **第 11 条生成器的收尾**：`src/compile/gct/builder.cpp:2164` 仍报 `FeatureNotSupported("generators")` |
| [`literal-head-type-expression`](../reviews/questions-2026-10-30-literal-head-type-expression.md) | `Tangent<T>` 这类算出来的类型能不能当字面量的头 | 已裁未落 | 10:10——②的规则、①的工作量：字面量头走类型表达式解析器；同一笔补 `zero_tangent` 的元组/结构体臂 | **`zero_tangent` 的聚合臂**（`stdlib/rulesets/jvp.cml:53` 仍是 `case _ => ()`）；规则集模块合入前的最后一块 |
| [`match-type-guard`](../reviews/questions-2026-10-30-match-type-guard.md) | `match type` 里能不能用类型函数做守卫 | 已裁未落 | 11:10——②：不是加守卫，是修缺陷；`T[..]` 匹配任意秩，`T[?]` 只匹配秩 1 | **`zero_tangent` 的任意秩臂真正生效**（修 `match type` 的实施例，本次未核）；`stdlib/rules.cml` 里靠"只匹配秩 1"写错的条目逐条改 `T[?]` |
| [`one-operator-two-names`](../reviews/questions-2026-10-31-one-operator-two-names.md) | `array:argmax_axis` 与 `tensor:argmax_axis` 两条身份怎么合 | 已裁未落 | 21:35——一个算子一个 URI，取 `tensor:`；`array.cml` 改 `#op`、删 C++ 侧注册 | **六个轴归约的 ONNX 导出与签名合并**；`stdlib/array.cml:405/413` 仍是 `array:` |
| [`grad-builds-its-graph-at-runtime`](../reviews/questions-2026-10-30-grad-builds-its-graph-at-runtime.md) | 去函数化的验收数不到 `grad` 造出的那个函数值，归第 6 条还是第 7 条 | 已裁未落 | 12:35——A：本条只管程序里写出的函数值；`grad` 造图归第 6 条 | **第 6 条把 `grad` 改写成 `reinterp`/`split`/`transpose`**；`indirect_calls` 读到 0 作联合验收 |
| [`defun-tag-in-the-type`](../reviews/defun-tag-in-the-type.cn.md) | 文档与 `defun.cpp:8-10` 哪个为准；老 `std::defunctionalize` 是否退役 | 已裁已落 | 12:35——文档为准；老 pass 退役，`std::defun_value` 长成完整一步；笔记进 `docs/design/defunctionalization/README.cn.md` | 无；它解锁第 7 条 |
| [`function-type-structural-vs-enum`](../reviews/function-type-structural-vs-enum.cn.md) | 函数类型按结构比较（5.2）与"每个函数类型一个 enum"怎么并存 | 已裁已落 | 12:35——一族一个 `enum`；效应限定是子类型；5.2 补一句 | 无（`design-principles.cn.md:354` 已写）；族定义给去函数化用 |
| [`how-an-indirect-call-hands-back-a-version`](../reviews/questions-2026-10-31-how-an-indirect-call-hands-back-a-version.md) | 穿过函数值的调用写下的捕获，版本怎么交回来 | 已裁已落 | 21:35——A：调用节点本身就是版本；删 `PendingIndirectCallVersion` | 无；`callback_write_ignored.cml` 已按"打印 3"验 |
| [`narrow-float-tangent`](../reviews/questions-2026-10-30-narrow-float-tangent.md) | `bf16`/`f16` 算不算浮点，`Tangent<f16>` 的射程 | 已裁已落 | 10:30——①：窄浮点也是浮点，切空间是自身；另排"窄浮点算术+显式转换"一条 | 无（`derivative.cpp:548-549` 已设 BF16/F16）；新排的窄浮点算术还不存在 |
| [`operator-family-names`](../reviews/questions-2026-10-30-operator-family-names.md) | 算子族在 stdlib 里怎么露出名字，`add` 能不能重载 | 已裁已落 | 13:50——写 `func operator +`，不重载 `add`，保留名不列函数一览 | 无；`prelude.cml:85` 起已有 4 条 `func operator` |
| [`reserved-name-in-library`](../reviews/questions-2026-10-30-reserved-name-in-library.md) | 库里的声明能不能叫保留名 `__add__` | 已裁已落 | 13:50——`__add__` 是内部名，库里写 `func operator +`；`ReservedIdentifier` 不动 | 无 |

## 裁决里点名要我们做的三件返工

22:50 的裁定点名了三件容易读漏的返工（不是新问题，是那一笔要求我们改的现有东西）：

1. **去两个环境变量开关**：`CAMEL_RULESET_INDEX`（`program.cpp:662`）与 `CAMEL_TILES_TEST_REASONS`（`region_test_pass.cpp:302`）都违反"不加环境变量开关"；改成正式遍/参数——规则集索引做成一个 `std::rulesets` 之类的翻译遍，tiles 做成测试遍的参数打印原因，用例钉住读数。
2. **删 `taskflow.cpp:2797` 那条 `DESIGN-QUESTION`**：并行 `map` 的裁定已给（保实现、3.6 改措辞），标记随之删；补部分故障、四后端同判的用例。
3. **`reinterp<jvp>` 是第一优先的消费者**：规则集索引的下一笔不是"等变换算子"，就是 `reinterp<jvp>` 按索引逐个原语替换、缺规则报 9.9；不另造检查。

这三件都记在 `301c3790e` 的 22:50 节里；这里单列，因为它们最容易被读成"背景"。

## 已撤：1 页

| 页 | 问什么 | 状态 | 裁定/去向 | 还堵着 |
|---|---|---|---|---|
| [`where-the-tag-for-an-operator-can-sit`](../reviews/questions-2026-10-31-where-the-tag-for-an-operator-can-sit.md) | 标签能不能挂在别处的算子声明上 | 已撤 | 20:50——没有跨模块挂标签这回事；结论并入上面 `the-ruleset-module-has-no-rules-in-it` 那一行 | 无 |

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
|---|---|---|
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
