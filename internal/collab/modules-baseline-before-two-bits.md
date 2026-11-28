# `pure` 拆两位之前的 `feat/modules` 底表

用途：`D-1128-120` 把 `pure` 一位拆成两位之后，`feat/modules` 上约 25 条断言要逐条判甲/乙/丙。判的人**唯一该信的尺子**是本页 —— 它给出"这些红在基线上本来是什么"。没有这张表，就会再出一次"丙恒为 0"那一类错（`internal/collab/pure-one-bit-four-readers.md` §6）。

**本页只读**：没有改代码、没有改断言、没有改计划、没有标 `known`、没有推远端。

## 1. 跑的什么（原样）

```
pwsh -NoProfile -File <tree>\internal\tools\camel-remote.ps1 `
  -Path D:\Projects\Camel-wt\mm-capacity `
  -Plan 'test/plans/feat/modules' -Tier fast -Jobs 4 `
  -SlotWaitSeconds 2400 -NoGpuWatch -QueueOnTreeLock
```

基 = `origin/integration` @ **`1de5b5f98b`**（`backlog: one kernel, one name -- prl stays`），枝 `lane/modules-baseline-read`（从该基新起，未换底、未提交）。

```
BUILD_EXIT=0
[provenance] tip (dirty: 3 files) | binary built 2026-11-28T13:21:17+08:00 |
             tree /home/zjwei/camel-work/mm-capacity | tier fast | jobs 4
[SUMMARY] total: 2095, pass: 2058, fail: 28, skip: 4, known failures: 5
[TREE] clean: 3 before, 3 after, 968 directories watched
[TREE] 3 paths were already uncommitted when this run started, not this run's:
       test/cases/probe/xfail_probe.py,
       test/plans/feat/modules/probe_xfail_check.plan.toml,
       test/plans/feat/modules/probe_xfail_probe.plan.toml
```

⚠ **这 3 个未跟踪文件不是我的**（另一格留在该树的探针）。其中两个是 `test/plans/feat/modules/` 下的计划 ⇒ **它们被算进了本次 `feat/modules` 层**：`[SUMMARY]` 的 28 条红里，**有 1 条（`xfail_probe`）来自这份外来探针，不属基线**。所以 **`feat/modules` 的基线红数 = 27**。

## 2. `[SUMMARY]` 与红名逐名（28 条，按 `[FAIL]` 原文顺序）

**基线红，27 条（全部在 141 名表里）**：

1. `op_stats.table.nvm`
2. `op_stats.table.fvm`
3. `export_unsupported_op`
4. `export_impure_op`
5. `pass_export_refusal_located.request_in_arm_json`
6. `as_refusal.constant_fails.pass`
7. `as_refusal.constant_fails.operator`
8. `slice_runtime`
9. `si_as_fails`
10. `si_as_fails_fvm`
11. `si_as_fails_jit`
12. `si_as_fails_prl`
13. `fusion_group.profile.none`
14. `npy_generic_wrapper_mismatch`
15. `rename.env_graph_identity`
16. `rename.process_graph_identity`
17. `types_and_node_kind`
18. `node_kinds_match_the_graph`
19. `constant_folding_makes_a_value`
20. `constant_folding_survives_a_safepoint`
21. `outline_delivers_the_graph_it_makes`
22. `outline_delivers_a_graph_the_bytecode_vm_can_compile`
23. `rules.repeat_counts_rounds`
24. `rules.repeat_honours_the_budget`
25. `rules.alternate_alternates_rounds`
26. `rules.until_reads_the_predicate_before_a_round`
27. `rules.until_honours_the_budget`

**表外红，1 条（外来探针，不是基线）**：

28. `xfail_probe` ← 由本次运行开始前就存在的未跟踪计划 `probe_xfail_probe.plan.toml` 产生

## 3. 逐条一行（机器可读版同目录 `modules-baseline-before-two-bits.tsv`）

| # | 名字 | 在 141 名表里 | 今天红的已知原因 |
|---|---|---|---|
| 1 | `op_stats.table.nvm` | 是 | 未定位 |
| 2 | `op_stats.table.fvm` | 是 | 未定位 |
| 3 | `export_unsupported_op` | 是 | 未定位 |
| 4 | `export_impure_op` | 是 | 未定位 |
| 5 | `pass_export_refusal_located.request_in_arm_json` | 是 | 未定位 |
| 6 | `as_refusal.constant_fails.pass` | 是 | 未定位 |
| 7 | `as_refusal.constant_fails.operator` | 是 | 未定位 |
| 8 | `slice_runtime` | 是 | 未定位 |
| 9–12 | `si_as_fails` / `_fvm` / `_jit` / `_prl` | 是 | 未定位 |
| 13 | `fusion_group.profile.none` | 是 | 未定位 |
| 14 | `npy_generic_wrapper_mismatch` | 是 | 未定位 |
| 15–16 | `rename.env_graph_identity` / `rename.process_graph_identity` | 是 | 未定位 |
| 17–18 | `types_and_node_kind` / `node_kinds_match_the_graph` | 是 | 未定位 |
| 19–20 | `constant_folding_makes_a_value` / `constant_folding_survives_a_safepoint` | 是 | 未定位 |
| 21–22 | `outline_delivers_the_graph_it_makes` / `…_bytecode_vm_can_compile` | 是 | 未定位 |
| 23–27 | `rules.repeat_counts_rounds`、`rules.repeat_honours_the_budget`、`rules.alternate_alternates_rounds`、`rules.until_reads_the_predicate_before_a_round`、`rules.until_honours_the_budget` | 是 | **schedulers 库编不过**：`'0 .. rounds' has type 'Range'`；根在"编 stdlib 库时 prelude 的导出表是空的"，`lane-witness` 正在修 |
| 28 | `xfail_probe` | **否** | 不是基线：外来未跟踪探针计划 |

比法：拿本次 28 个 `[FAIL]` 名字逐名对 `internal/reports/2026-11-28-promotion-76-reds.tsv`（141 行，两列 `plan<TAB>case`）。**27 条全部命中，1 条（`xfail_probe`）表外。**

## 4. 判据：红数必须等于 `[SUMMARY]` 的 `fail`

- `[SUMMARY] fail: 28` ↔ 本页红名 **28 条**：**对得上**。
- 其中 **27 条属基线、1 条属外来探针**；一张干净树上的同一层应为 **27**。判甲的人请用 **27** 当基线尺子，别把 `xfail_probe` 记进去。

## 4.5 判甲的人只需记这一行

**本表的 27 条是基线；任何拆两位之后新出现的红都算甲，包括 `resident_cpu_block`（它今天不红）。**

## 5. 一条需要负责人回看的事

你给的已知原因里有一条是 **`resident_cpu_block` = 读 `nodeIsFreelyArrangeable` 的 downstream（拆两位会让它变）**。**本次运行里 `resident_cpu_block` 不是红的** —— 它既不在 28 条 `[FAIL]` 里。所以拆两位之后若它变红，那是**新增**，不是基线红；判甲时它应走"新红"那条路，而不是本表。

## 6. clean-tree confirmation（判甲用这一个数：**27**）

上面 §1–§4 那次是在 `mm-capacity` 上跑的，那棵树当时有别人留下的两个 `feat/modules` 探针计划 ⇒ 28 条里混进 1 条外来的。**同一层在一棵确认干净的树上重跑，读数是 27**：

```
$ cd D:\Projects\Camel-wt\perf
$ git status --porcelain        （空）
$ git checkout -B lane/modules-baseline-clean origin/integration
  → Switched to a new branch 'lane/modules-baseline-clean'
$ git log -1 --format='%h %s'
feddc24ee0 backlog: twenty-seven is the ruler, and six plans are absent by design

-Build → BUILD_EXIT=0
[provenance] tip feddc24e | binary built 2026-11-28T13:28:51+08:00 | tree …/perf | tier fast | jobs 1
[SUMMARY] total: 2093, pass: 2058, fail: 27, skip: 4, known failures: 4
[TREE] clean: 0 before, 0 after, 950 directories watched
```

`tip` 后面没有 `(dirty: …)`；跑之前、跑之后 `git status --porcelain` 都是空。

**27 ↔ 27 全 `==`**（`Compare-Object -ReferenceObject <表里 27 条> -DifferenceObject <本次 27 条> -IncludeEqual`，两侧都先 `Sort-Object`）：`table count = 27`、`run count = 27`、输出 27 行全部 `==`，**没有一条 `<`（只在表里）或 `>`（只在运行里）** ⇒ 集合完全相同。

**与脏树那次相比的三处变化，本身就是"那棵树被别人写脏了"的证据**：

| | 脏树（`mm-capacity`） | 干净树（`perf`） | 差 |
|---|---|---|---|
| `total` | 2095 | 2093 | **−2**（少的正是那两份外来探针计划） |
| `known failures` | 5 | 4 | **−1** |
| `fail` | 28 | **27** | **−1**（少的就是 `xfail_probe`） |

⚠ `jobs 1`：我传的是 `-Jobs 4`，负载下运行器把它夹到了 `1`。**名字集不受影响**（这是判甲唯一用的东西），但**性能行不能拿这种读数**。

## 7. 边界

- 本页只覆盖 **`feat/modules` 的 `-Tier fast` 层**；其它目录未跑。
- §1–§4 那次的树上有 3 个外来未跟踪文件，我**没有**删、没有动、没有提交。
- 未改代码、断言、计划；未标 `known`；未推远端。