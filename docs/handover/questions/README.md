# 问题：交给作者的地方

> **判据：一个页在 `questions/` 里活着的唯一理由，是有一个还没答的问题。**
> 已经有裁定、或者已经修掉的页，挪到 `docs/handover/reviews/` 留档（`git mv`，不删，页首留 `裁定：` 一行）；
> 只是记录、没有问题的页也不留在这里。

放在这里的每一页是**一个**尚未裁定、需要作者决定的问题；文件名照 `reviews/` 的规矩：`questions-YYYY-MM-DD-<主题>.md`，一问一页，几页之间不互相引用。

写清楚三件事：现象（现在的行为，出处到文件与行）、为什么不能自己定（压到设计原则的哪一条），以及**两个（至多三个）可以照字面执行的选项**——选项要写成作者圈一个就能派工的样子，不要问开放题。

裁定回来后，把这一页用 `git mv` 挪到 `docs/handover/reviews/` 留档，页首写一行 `裁定：`（结论 + 落在哪个提交）；`questions/` 只留还没答的。

作者每轮先读这个目录，所以这里空着就是"没有待裁定问题"。

## 一页怎么写（第 11 条那一路的写法，可以照抄）

每页三段：**现象**（实测/读码得到的事实，带 `file:line`）、**两条路的代价**、**我方默认建议**
（没裁定前按它做，并写明按它做会留下什么限制），末尾一句**要作者答的一句**。放这里的是
"猜不得"的事：两种写法语义不同、或一种要动语义/类型系统，不能由施工方 ad hoc 定
（`docs/design-principles.cn.md`：未决问题归语言作者）。已经能按现有裁定推进的事不进这里；
裁完的页把结论写到页首的 `裁定：` 一行，并在同一提交里把默认建议改写成"已按裁定落地"，
不要删页——下一个人要知道这条路为什么长这样。

## 待裁

| 页 | 问题 | 状态 |
|---|---|---|
| `questions-2026-10-31-tag-argument-of-a-rule.md` | 标签参数能否携带一个 `rule`（`#jvp(add_jvp)` 里的 `add_jvp`）：今天的判据只收"程序里的一个函数" | 待裁 |
| `questions-2026-10-31-what-carries-a-wait-for-a-write.md` | 一个"等一次写"由谁携带（`edges` 车道即将交页，本行占位，页到了再补问题原文） | 待裁（页未到） |
| `generator-cell-type-by-inference.cn.md` | 单元的类型由"只求类型的推导"定，还是由运行时状态的首个写入定 | 待裁 |
| `defun-tag-in-the-type.cn.md` | 文档是否取代 `defun.cpp` 那句"函数值已经是标签 + 环境"；老 `std::defunctionalize` 是否退役 | 待裁 |
| `function-type-structural-vs-enum.cn.md` | 函数类型是结构别名（5.2），"每个函数类型一个 enum"是名义类型，两者如何并存 | 待裁 |
| `questions-2026-10-30-grad-builds-its-graph-at-runtime.md` | 去函数化的验收数不到 `grad` 造出的那个函数值：这一段归第 6 条还是第 7 条 | 待裁 |
| `questions-2026-10-30-jvp-family-anchor.md` | `#jvp` 这一族规则没有挂点：补原语声明、标签挂到 `rule` 上、还是按算子 id 查表 | 待裁 |
| `questions-2026-10-30-literal-head-type-expression.md` | 算出来的类型能不能当字面量的头：这条规则铺到哪为止 | 待裁 |
| `questions-2026-10-30-match-type-guard.md` | `match type` 里能不能用类型函数做守卫 | 待裁 |
| `questions-2026-10-30-narrow-float-tangent.md` | 窄浮点算不算"浮点"：`Tangent<f16>` 的射程 | 待裁 |
| `questions-2026-10-30-operator-family-names.md` | 算子族在 stdlib 里怎么露出名字：`add` 能不能重载成两个参数 | 待裁 |
| `questions-2026-10-30-reinterp-rule-set.md` | `reinterp<H>`：规则集 `H` 以什么形式交给算子 | 待裁 |
| `questions-2026-10-30-reserved-name-in-library.md` | 库里的声明能不能叫保留名：`__add__` 与 `ReservedIdentifier` | 待裁 |

13 页里有 12 页在等裁定；表里 13 行，第 2 行是 `edges` 车道的占位（页还没交上来）。

今晚从这张表里挪走的（`git mv` 到 `docs/handover/reviews/`，页首有 `裁定：` 一行）：

- `questions-2026-10-30-tangent-in-prelude.md`、`questions-2026-10-31-tag-import-rechecks-prelude.md`——已修（`1ab619a57` / `d1b3531b4`）；
- `questions-2026-10-30-generator-read-before-write.md`——作者的答复已落地（`5803e9039`）；
- `questions-2026-10-30-jvp-tag-scope.md`——裁定 ① 已落地（`d1b3531b4`）；
- `questions-2026-10-30-region-count-account.md`、`questions-2026-10-30-two-behaviour-diffs-left.md`——不是问题，是记录（页首已写明）。

`questions-2026-10-30-probe-plan-kept-or-dropped.md` 这一行删掉了：那一页在树里已经不存在（`questions/` 与 `reviews/` 都没有），行留着只会让人去找一个不在的页。

`questions-2026-10-30-allocation-failure-in-the-faults-table.md` 已于 2026-10-30 答复：分配失败
不算 `faults` 表该声明的故障（(b)，进程级界限），页已挪到 `docs/handover/reviews/`。
