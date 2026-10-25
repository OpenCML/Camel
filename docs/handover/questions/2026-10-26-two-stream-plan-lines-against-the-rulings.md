# io-concurrency-streams 计划里两行与现行裁定不符 · 2026-10-26

> 说明（协调者）：两行都在 `docs/design/io-concurrency-streams/plan.cn.md`，都**不由我们擅自改**（`docs/design/**` 需要你授权），所以分开报。第 1 行是实现路线已经改了、计划没跟上；第 2 行是 `module` 声明已删、计划里引的程序还在写它。

## 1. `plan.cn.md:312` 片 F 的「`__iter__` 加一个流的重载」与「路线二」不相容

**逐字（页面上的原句，片 F 那一行）：** 要求给 `__iter__` 加一个流的重载。

**为什么不相容。** 你在 `2026-10-26-generator-state-machine-what-is-missing` 一节裁的是**路线二**（在**图里**展平成步进函数，四个后端一行不改，图原生）。这条路线的实现形态经车道收口后是：**不造流值**——生成器的机器状态并进消费者的 `loop`（`$pc`/`$susp`/`$out` + 提升的局部 + 用户 state，`if $pc == k` 的块链，复用 LOOP/BRCH，不新增节点种类），消费者在 `for` 位融合展开。而 `__iter__` 要的是"把流当值传进去、由它逐轮拉取"，那要求流值有运行期表示；今天设计里明确写着**还没有**：

- `include/camel/core/type/composite/stream.h:28-30`：「A value of this type does not exist yet」；
- `TypeCode::Stream` 只有存储与打印；`src/passes/**` 与 `src/execute/**` 对 `Stream` **0 引用**。

所以按路线二，`__iter__` 这个接法做不到；能做的就是"流出现在 `for` 以外的位置"给一条具名诊断（我们已把它排进这一批）。

**要你一句话**：
- **(A) 以路线二为准**，片 F 那行改成"流值只在 `for` 位被消费；出现在别处是具名诊断"，`__iter__` 的流重载留到"流有运行期表示"之后再谈（**我们建议这条**：它与已裁的路线二、与"四个后端一行不改"一致，也不新增语言表面）；
- **(B) 保留 `__iter__` 重载的意义**，那就等于要求流值有运行期表示，路线二不够，需要你另裁表示形式（那会是新的一批设计）。

**另有你已点头的两处**我们已照办（`:296` 的两条实测读数、`:308` 的 `loop n = 0 for p in pairs(3) do n + 1`），这一行是同一份稿子里的第三处。

## 2. `plan.cn.md:297` 引的程序还写着 `module` 行

**是什么。** 该行引用的程序（片 E 的 t3 围栏）里仍有 `module …` 声明，而 `module` 声明已按前一次裁定删除：`scripts/strip-module-declarations.mjs` 今天报告**全仓无可删**，任何 `.cml` 里都不再有 `module` 行。

**为什么没动。** `docs/design/**` 不在车道的可动清单里（今天全仓唯一一处 `docs.snippets_compile` 的遗留问题就是它：去 `module` 行即绿）。

**要你一句话**：**可以删这一行 `module` 吗？**（删掉后该围栏与仓库其余部分一致，`docs.snippets_compile` 归零。）

**顺带一条同类的**：`docs/handover/questions/` 里那十页旧问题页已按你的要求删掉，但有三处**代码注释**原先引用它们（`modules/tensor/ops/layout_ops.cpp:412`、`src/builtin/modules/globals.cpp:224`、`test/cases/opt/generic/specialize_dim_name_grow.cml:12`），我们已改成引用裁定小节（`docs/handover/reviews/questions-2026-10-24-evening.md` 的对应小节名），不再留悬空引用。