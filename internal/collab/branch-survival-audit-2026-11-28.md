# 三组存活账（2026-11-28 08:1x，只读）

树 `D:\Projects\Camel-wt\perf`，基线 `origin/integration = 7ca31648fb`。
上一版"20 条"总账**作废**（与 `lane-parked-rebase` 撞）；本页只判三组。

## ① `lane/pass-decl` vs `lane/pass-decl-3` —— 可删 `pass-decl`，留 `pass-decl-3`

证据：

- 两枝各自笔数**都是 14**（`git log --format='%s' origin/integration..<branch>`）。
- `git cherry lane/pass-decl-3 lane/pass-decl`：**13 个 `-`**（同一补丁已在 `pass-decl-3` 里）
  **+ 1 个 `+`** = `cc8af549a8`，它只改 `internal/collab/pending-cards.md`（+6 行，
  标题"the parameter a run's target refuses names the step as written"）；
  而 `pass-decl-3` 的末笔 `bde63eb825` **同一标题**（07:03，`pass-decl` 那笔是 02:49）。
- 这一笔在 `pass-decl-3` 里**已被更新的卡片内容取代**：`git show lane/pass-decl-3:internal/collab/pending-cards.md`
  里没有那行原文，`git diff lane/pass-decl lane/pass-decl-3 -- internal/collab/pending-cards.md` 显示
  `pass-decl-3` 的 `### O.` 已重写成"两条枝的去向（`D-1128-8` ①②）"。
- 两枝相对 integration 的净 diff 规模相同：`79 files changed, 1345 insertions(+), 299 deletions(-)`
  ⇒ 同一组补丁，`pass-decl` 是 237 笔落后的旧基版本。

**判决**：`lane/pass-decl` **可删**（重复且其唯一差异已被取代）；`lane/pass-decl-3` 是活的那条（behind 35，07:03）。

## ② `lane/cc-loop-forward`（4 / 111）—— 仍需人；内容独有，不能判删

- `git cherry origin/integration lane/cc-loop-forward`：**4 个全是 `+`** ⇒
  `9fce5da25f`、`c5d16bc078`、`bf57e0dbe5`、`1e13905533` **integration 里都没有**。
- 最后提交 `1e13905533` 2026-11-28 05:49:56（2 小时前），此后该枝**没有任何新提交**
  ⇒ 05:49 之后**没有人接手**。
- 内容性质：`task-383` 的交接（`c5d16bc078`、`bf57e0dbe5`）+ 一条测试
  （`1e13905533` "the gradient type's printed text carries the names and the resource clause a signature has"）。
  那 4 笔不是能丢的重复补丁，是另一条线（383 的 ONNX 落点）的交接与判据。

**判决**：**仍然独有** ⇒ 不判删；**该换底**（behind 111）后进合并批；因为它承接的是别的卡，进批前
需要那条线的负责人确认"这 4 笔是不是仍要"。`lane-parked-rebase` 若按"落后>100 就删"处理到它，会丢东西。

## ③ `lane/index-axis`（1 / 29）与 `lane/editview-commit`（16 / 24）—— 都在推进

- `lane/index-axis`：唯一独有提交 `e46300eb77` 2026-11-28 07:10:58
  "compile: a subscript axis one dimension too far is refused where the call is built"；
  `upstream=origin/integration`、`unpushed=1`。**判决：仍在推进（刚做完，等合）**，behind 只 29，换底成本低。
- `lane/editview-commit`：最后两笔 `67e8e99da6` 07:52:23
  "graph: an effect qualifier does not split the draft type"、`36718a6bcd` 07:36:15
  "test: the two graph-pass libraries that commit a draft take the draft"；**无 upstream** ⇒ 16 笔只在本地。
  **判决：仍在推进**，且 07:52 那一笔看起来正是 `D-1128-59`
  （"effect qualifier 不该把 draft 类型劈开"）要的修法，进合并批前该让 59 的负责人先看它。

## 一句

按此账：**可删 1 条（`lane/pass-decl`）**；`cc-loop-forward` 判"仍需人 + 换底"（4 笔独有，别按落后>100 删）；
`index-axis`、`editview-commit` 都在推进。
**没有删任何枝、没有 push、没有动 `src/**`。**