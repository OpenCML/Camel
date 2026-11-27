# 任务卡 · `opt::inline` 验收补三项（`D-1128-51`）

**来源**：`D-1128-51`（2026-11-28）。验收文档：`internal/plans/2026-11-28-opt-inline.cn.md`（35 行，第 3/4 节 + 验收段）。**任务板 256 已满，本卡先落 `internal/collab/`，起活时建板卡并 `reassign`。**

**优先级**：CC 格（Figure 1 用的就是 `opt::inline`）。**三项一枝交。**工作树 `D:\Projects\Camel-wt\op-names`（`lane/opt-inline`，已合入 integration，此为其后继活）。

## 现状与缺口

- 第 3 节要 `inline.by_cpp` / `inline.by_camel` 一对、`std::jgir canonical=true` **逐字节相等**；语料至少四条：Figure 1 例子（`inc(x) * 3 + (x + 1) * 4`）、捕获变量的闭包调用、增长预算为 0（一个都不展开）、递归（不展开）。**现状**：`camel_inline` 计划比的是"每图节点数与调用数"，语料只有递归四形，**Figure 1 / 闭包 / 预算三条缺**。
- 第 4 节要 Figure 1 读数页有 `f->opt::inline->opt::cse->opt::algebra` 那一版**三张图**（结尾 `x → (+1) → (*7)`）并与 `std::cxx::inline` 版逐字节比。**现状**：只有 cxx 版。
- `opt.cml` 头上四条 unaligned 要收掉：每图直接调用数、调用图强连通分量（**Camel 里由 `callee` 遍历可算**）、内联历史（**遍自己记**）、"调用是否分支臂首"（若图模块确实无读法，才补一个内核读，并先给出查证依据）。

## 判据

1. 第 3 节那对用例在尖端绿，“内联确实发生/确实没发生”两条断言都成立；贴四语料各自读数。
2. `std::jgir canonical=true` 两边**逐字节相等**（写文件比 sha256 或 `cmp`，要贴实数）。
3. `mutual_recursion` 与 C++ 一致。
4. Figure 1 读数页三张图 + 与 cxx 版逐字节比。
5. `opt::inline` 的 `@example` 在库示例检查里编得过。
6. **新增红 0**：`feat/opt` + `feat/modules` + `feat/compile`，`-Tier fast`，对 `newestRedTable()`（promotion-66，147 行）逐名。
7. `[provenance] tip` + `binary built` + `numstat`，`BUILD_EXIT=0` 同一次运行。

## 禁

- 放宽断言、加 `known_fail`、改测试计划判据以变绿（测试计划属元层，只能由决策者改）。
- 为“少内联也行”留白：四条 unaligned 必须收，收不掉就报决策者，不在代码里留注释了事。