# 待裁索引：谁在等作者，堵着什么

> **一页一问；索引按"堵住多少工作"排序，因为作者的时间应该花在最贵的那页上。**

> **这个目录的机制**：作者把已经读过的页整体挪进 `reviews/`；`questions/` 只放**还在等回答**的页。所以 `questions/` 空不是没人在问，是问题都被读过了。新的页从车道落进来（今天有两条车道手上各压着一页），落进来就在待裁表加一行。
>
> **哪些未决值得成页**：不是所有未决都值得成页——只有**今天有人被它堵住**的才值得。其余留在原则里，等它真的挡住一条车道再成页。所以第 15 节那 8 条不占待裁表，清单在文末，每条写明今天堵着谁。

状态四种：**待裁**（在等作者）、**已裁已落**（裁定有，改动已在 develop）、**已裁未落**（裁定有，改动还没进 develop）、**已撤**（问题作废）。"还堵着"一栏写的是这条裁定今天还挡着哪些具体工作；已经落地的写"无"。

## 现在在等作者：2 页

`questions/` 里有两页在等：一条是别名（同一对象被两条引用写），一条是并行 `map` 的故障边界。上一批 15 页在最近一次合并里整体挪进了 `reviews/`，裁定按时间记在 [`reviews/questions-2026-10-29.md`](../reviews/questions-2026-10-29.md)；旧表写着"13 页待裁"，是一张过期的表。

| 页 | 问什么 | 状态 | 堵着谁 |
|---|---|---|---|
| [`two-references-write-one-object-outside-a-sync-block`](questions-2026-10-31-two-references-write-one-object-outside-a-sync-block.md) | 同一可变对象被两条引用写、第二条在这一层看不见时，读拿谁的版本 | 待裁 | 效应那一族的别名收尾；高阶函数把闭包经参数传进来之后的写；`a-version-port-that-must-carry-a-value` 的边界 |
| [`parallel-map-fault-what-runs-after`](questions-2026-10-31-parallel-map-fault-what-runs-after.md) | 并行 `map` 一处故障后，下标更小、还没开始的元素是否仍执行 | 待裁 | 并行 `map` 的失败报告语义；跨后端一致性用例（`attempt.plan.toml:207`）；3.6 的措辞 |

原则第 15 节另列着 6 条未决（另 2 条已成页），见文末。

## 已裁：按还堵着的活排

| 页 | 问什么 | 状态 | 裁定 | 还堵着 |
|---|---|---|---|---|
| [`the-ruleset-module-has-no-rules-in-it`](../reviews/questions-2026-10-31-the-ruleset-module-has-no-rules-in-it.md) | `autodiff` 算不算 `jvp` 那族的规则集模块，还是把前向规则搬进 `rulesets/jvp.cml` | 已裁未落 | 22:10——(b) 搬进 `rulesets/jvp.cml`；规则集就是 `stdlib/rulesets/` 下的模块，按路径前缀判断 | **第 3 步的验收**（索引非空、标量 `jvp` 跑通）；`autodiff` 只留 `grad`/`jvp`/`vjp` 包装。今天 10 条 `rule` 在 `autodiff`，`rulesets/jvp.cml` 0 条 |
| [`jvp-family-anchor`](../reviews/questions-2026-10-30-jvp-family-anchor.md) | `#jvp` 这族规则没有挂点：补声明、挂标签、还是按算子 id 查表 | 已裁未落 | 09:35 ①，20:50 改成"内置原语的规则写规则集模块、按左边归属"，22:10 补上"搬规则" | 与上一行是**同一件活**；索引已落（`program.cpp:658-659` 按 `rulesets.` 前缀建），规则还没搬 |
| [`what-carries-a-wait-for-a-write`](../reviews/questions-2026-10-31-what-carries-a-wait-for-a-write.md) | 值在门后等一次写、或等一次调用，该由哪种边承担 | 已裁未落 | 20:35——3.1 已经答了，不加第四种边；三件事：门的等待=同步前驱、三族遍改读"数据使用+同步链"、然后停造 ctrl 重测 | **②a 停造 ctrl**；`simplify`/`unread_gate`/`dropped_effects` 三族遍；`gates.cpp:19` 的使能条件 |
| [`reinterp-rule-set`](../reviews/questions-2026-10-30-reinterp-rule-set.md) | 规则集 `H` 以什么形式交给算子 | 已裁未落 | 09:15——C：`H` 是编译期名字；规则集是一个模块；`transforms` 收 `reinterp`/`split`/`transpose` | **`reinterp` 还没实现**（`git grep reinterp` 只中 `reinterpret_cast`），`modules/transforms/` 只有骨架；第 3–5 步都等它 |
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

## 已撤：1 页

| 页 | 问什么 | 状态 | 裁定/去向 | 还堵着 |
|---|---|---|---|---|
| [`where-the-tag-for-an-operator-can-sit`](../reviews/questions-2026-10-31-where-the-tag-for-an-operator-can-sit.md) | 标签能不能挂在别处的算子声明上 | 已撤 | 20:50——没有跨模块挂标签这回事；结论并入上面 `the-ruleset-module-has-no-rules-in-it` 那一行 | 无 |

## 留档里仍写着"待裁/等裁决"的页（已裁，别再当待裁）

- [`reviews/questions-2026-10-30-cell-type.md`](../reviews/questions-2026-10-30-cell-type.md)：页首说"已由作者答复…不再更新"，第 5 行却还写"裁定：待裁"；16:30 已裁（只求类型的推导）。
- [`questions-2026-10-31-how-an-indirect-call-hands-back-a-version`](../reviews/questions-2026-10-31-how-an-indirect-call-hands-back-a-version.md)：页内写"等作者裁决"，21:35 已裁并已落。
- [`questions-2026-10-31-what-carries-a-wait-for-a-write`](../reviews/questions-2026-10-31-what-carries-a-wait-for-a-write.md)：页内写"等作者裁决"，20:35 已裁。

这三页的页内状态是它主人的活，本索引只记判断，不改页。**页内状态可能落后于索引；以索引为准**——下一个人别读到页里的"待裁"就去问一个已经答过的问题。

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
| 块外多处引用写 | 同一可变对象在同步块之外被两条引用写，按什么排 | **已成页**（待裁，见上表）；与车道手上 `a-version-port-that-must-carry-a-value` 相邻但不是同一问 |
| 补贴标签 | 能不能在别处给一个声明补贴标签，调度是否例外 | 无；20:50 已把内置原语的规则定到规则集模块 |
| 并行 map 故障 | 并行 `map` 出故障后，下标更小、还没开始的元素是否仍执行 | **已裁**（2026-10-31，`301c3790e`）：取①——保实现、改 3.6 措辞；代码里的标记已删，用例 `attempt.partial_fault` 钉住 |
| 算子实参装入 | 内建算子的实参是否自动装入（`xs->append<5>`，`xs: (int?)[]`） | 无车道被堵；但审计 A25 记着今天要写 `5 as int?` |
| 源码声明流水线 | 是否允许入口文件写出 `pipeline std::macro, tiles::compile, std::nvm` | 无；流水线仍只在命令行拼 |
| GIR handle 名字什么 | 编译期 `node_handle_t` 名字一个事务（与 `NodeView` 同型）还是一张草稿 | **已成页**（待裁）；**堵 b6 的最后一块**：`nodeDraftOf` 66 个 + `draft()` 132 个调用点 |

## 裁定去哪找

- 本批 15 页的裁定：[`reviews/questions-2026-10-29.md`](../reviews/questions-2026-10-29.md)，按时间排，末几节是 15:30–22:10。
- 更早的批量复核：[`questions-2026-10-23`](../reviews/questions-2026-10-23.md)、[`-24-evening`](../reviews/questions-2026-10-24-evening.md)、[`-26-late`](../reviews/questions-2026-10-26-late.md)、[`-29`](../reviews/questions-2026-10-29.md)、[`-30-jax`](../reviews/questions-2026-10-30-jax.md)、[`-30`](../reviews/questions-2026-10-30.md)。
- 单条裁定留档：`reviews/questions-2026-10-30-*.md` 与 `-10-31-*.md`，页首一般有 `裁定：` 一行。

## 写页的规矩

- 一页一问，文件名 `questions-YYYY-MM-DD-<主题>.md`。
- 三段：现象（`file:line`）、两条路的代价、我方默认建议；末尾一句要作者答的话，选项要能照字面执行。
- 裁回来：`git mv` 到 `reviews/`，页首写 `裁定：`（结论 + 落在哪个提交）；`questions/` 空 = 没有待裁。
