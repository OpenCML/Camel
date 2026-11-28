# 基线 73：72 表 141 名

**73 的基线 = 72 表 141 名。** 下一轮晋级（73）逐名对它比；比较方式是 `plan_id` + 用例名，逐名，不拿总数相减。甲/乙/丙 三个词的定义在 `coordinator.md`「基线口径」一节，本页只管"哪张表、怎么读、哪一条要特别读"。

## 是哪一张表

| 项 | 值 |
|---|---|
| 红表 | `internal/reports/2026-11-28-promotion-72-reds.tsv` |
| 行数 | **141**（两列：`plan_id`，然后是用例名；按 runner 写 `test_timings[]` 的原样，两侧 trimmed） |
| 计划数 | 40 个 `plan_id`（同一张表里既有 `feat/*`，也有 `examples`、`transforms`、`stated_extents` 这类别名） |
| 所测 tip | `0967afd6159a8d9b952e9a1e58f61038c7e0f630` |
| binary | built `2026-11-28T10:55:10+08:00`，`-Tier fast -Jobs 2`，起跑 load 1m 31.35 |
| 那一层的读数 | `total 9919 / pass 9717 / fail 141 / skip 28 / known 33 / timed out 0 / re-run 0` |
| 与上一张比 | 71 表 **142 名** ⇒ **甲 0 / 乙 1 / 丙 141** |
| record | `internal/reports/2026-11-28-promotion-72.record.json`（`red_table_rows` 与 `next_promotion_compares_against` 都指向这张表） |

## 怎么用它

- **甲/乙/丙 只用 `coordinator.md` 的定义**：甲 = 旧表无、新表有；乙 = 旧表有、新表无；丙 = 两边都有。写"甲"时逐个名字先去**旧表**里查一遍。
- **别名当同一计划读**：`transforms` ↔ `compile.transforms`、`stated_extents` ↔ `compile.stated_extents`（72 的 record 的 `comparison.method` 里写明）。
- **表里的名字不是"绿"，是"这张表量的那一刻不红"**。要断言某条绿，得在同一 tip 上跑那一条；要断言它没变，得拿同一 tip 的两张表比。
- **跨 tip 的两张表不能直接逐名比**：先当"base 的差"处理（`references/pitfalls.md`「同一个"绿"还必须在同一个尖端上」）。
- **引用写法**：交回里写"对 72 表 141 名（`2026-11-28-promotion-72-reds.tsv`，tip `0967afd6159a`）"，不要写"对上一版"。

## 逐名比时最容易漏的一处：属主列写裸名

表的属主列（`plan_id`）有两族写的是**裸名**，而计划文件在 `test/plans/feat/compile/`：

| 表里写的 | 行数 | 计划文件 | 按目录拼的写法 |
|---|---|---|---|
| `transforms` | **8** | `test/plans/feat/compile/transforms.plan.toml` | `compile.transforms` |
| `stated_extents` | **1** | `test/plans/feat/compile/stated_extents.plan.toml` | `compile.stated_extents` |

- 66–72 这几张表**都**写裸名（逐张 `grep` 核过：`transforms` 各 8 行、`stated_extents` 各 1 行；`compile.transforms` / `compile.stated_extents` 在表里 **0 行**）。merge 位的逐名比较把两种写法当**同一计划**（`2026-11-28-promotion-72.record.json` 的 `comparison.method` 明写）。
- ⇒ 按 `^(compile\.|modules\.)` 之类**前缀过滤属主列**，会**正好漏掉这 9 行**，分母少 9。
- **实例（今天真发生）**：`task-404` 换底后把本层目录内的 72 表行数算成 **31**、实际 **40** —— 31 = `compile.*` 4 行 + `modules.*` 27 行；40 = 31 + 那 9 行裸名。于是写出"31/31 仍红"这种**既对又误导**的话（它自己的 `甲 0` 与"40 条全在表里"本来就互证分母是 40）。
- **判法**：先按"本层跑到的目录"取表行数，**再套别名**，然后只问一句"这些行是不是全部仍红"——**不去数未跑目录**（数了就会把"没跑"读成"转绿"）。

## 71→72 的唯一一条"乙"要按过渡重钉读，不是修复

`modules.onnx.export_loop_carried_array_field` 是 71 表里有、72 表里没有的那一条（两张表都已核对）。它**不是被修好的**：

- 它断言的前提已被撤回。`D-1128-39` 推翻"ONNX 侧无法携带变化形状"的结论：ONNX 的 `Loop` **可以**携带 sequence（`SequenceEmpty`/`SequenceInsert`/`SequenceAt`/`ConcatFromSequence`）或长度逐轮变化的张量 + `Concat`；"scan 输出形状固定"只约束 scan 输出，**不约束携带量**。所以 "a Loop carries a state whose shape is the same on every round" 不再是事实；这个导出器**还不会**把收集循环降成序列，那是实现缺口，`task-383` 在补。
- 它转绿是因为**位置被改钉**：`ba4d1ec233`（`onnx tests: the carried-array refusal's column follows the call it now names`）只改了一行 —— `expect_diagnostic_column = 81` → `9`，把早已存在的拒绝钉到新的调用位置（`walk(x, { xs: [0], k: 0 })`，行 22 列 9）。消息、诊断名、code、"不写模型文件"全未改。
- 而 `verify_script` 断言的句子仍是那句已撤回的前提（`test/plans/feat/modules/onnx.plan.toml` 的 `export_loop_carried_array_field` 条目，verify 里那句"…and a Loop carries a state whose shape is the same on every round"）。按 `D-1128-52`：这条要改成正向用例（导出成功、模型里有 `Loop` 且携带量是 `sequence` 或长度逐轮变化的张量、ORT 与 `nvm` 逐值相同），**降级落地前照旧红**；改它的是 `task-383` 那条车道。

⇒ 引用这张表时：**不许**把这条写成"修复"。写法只有两种 —— "过渡（钉位置 / 钉文本，前提待撤回方向落地）"或"仍红"。它在口径上算 `乙`（旧表有、新表无），但**这个乙不代表缺陷少了一条**：`72.record.json` 的 `comparison.notes` 写成 "the loop-tape-values work `ba4d1ec233` is what repaired it"，那句话在这个前提下要按过渡读。