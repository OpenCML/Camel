# 问题：交给作者的地方

放在这里的每一页是**一个**尚未裁定、需要作者决定的问题；文件名照 `reviews/` 的规矩：`questions-YYYY-MM-DD-<主题>.md`，一问一页，几页之间不互相引用。

写清楚三件事：现象（现在的行为，出处到文件与行）、为什么不能自己定（压到设计原则的哪一条），以及**两个（至多三个）可以照字面执行的选项**——选项要写成作者圈一个就能派工的样子，不要问开放题。

裁定回来后，把这一页挪到 `docs/handover/reviews/` 留档，并在答复落在的那一页（通常是 `docs/handover/reviews/questions-YYYY-MM-DD.md` 的裁定节）里写回一句"已答复，见 …"；`questions/` 只留还没答的。

作者每轮先读这个目录，所以这里空着就是"没有待裁定问题"，不要放占位文件。

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
| `generator-cell-type-by-inference.cn.md` | 生成器单元的类型由「只求类型的推导」定，还是由运行时状态的首个写入定 | 待裁 |
| `questions-2026-10-30-grad-builds-its-graph-at-runtime.md` | 第 7 条的验收数不到 `grad` 在运行期造出的函数值：这一段归第 6 条还是第 7 条 | 待裁 |
| `questions-2026-10-30-cycle-guard-edge-kinds.md` | 环的判据：守卫走 5 类边（含 `sync`）、冻结走 4 类，这个差是有意还是缺陷 | 待裁 |

表里三行就是现在待裁的全部；已答复的页都挪到了 `docs/handover/reviews/`。

2026-10-30 05:40 的裁定答的是 A 的取值形状（错误值进单元、`pairs(n)` 的跨 `yield` 携带量、
`return`、可错元素）；cell-type 与 write-before-read 各自末尾问的那一句，由 2026-10-30 07:20 的裁定
一并答复；rows-of-name 那页由 08:10 的裁定答复（`RowsOf<T>` 沿用）。七页的裁定原文见
`docs/handover/reviews/questions-2026-10-29.md` 末节与 `docs/handover/reviews/questions-2026-10-30-gate-control-edge.md`。

## 已答复（2026-10-30，现在在 `docs/handover/reviews/`）

| 页 | 问题 |
|---|---|
| `questions-2026-10-30-what-counts-as-a-statement.md` | `sync` 块里「谁算语句」 |
| `questions-2026-10-30-may-fault-boundary.md` | `sync` 链上「可能出故障」的边界 |
| `questions-2026-10-30-cell-type.md` | A 的"只带类型"的单元，类型从哪来 |
| `questions-2026-10-30-write-before-read.md` | "写之前读是编译错误"由谁证 |
| `questions-2026-10-30-what-keeps-a-nested-calls-value-alive.md` | 嵌套调用的值靠什么存活 |
| `questions-2026-10-30-tiles-12-not-10.md` | tiles 读到 12 |
| `questions-2026-10-30-rows-of-name.md` | 类型函数原语 `RowsOf<T>` 这个名字沿不沿用 |
| `questions-2026-10-30-undeclared-faults.md` | 没声明 `faults` 是第三态还是没写完的表：答「第三态有意义、读数保守，表按 reach 补完」 |
