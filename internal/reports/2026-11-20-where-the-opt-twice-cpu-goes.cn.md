# `opt_twice` 的 CPU 花在哪：两张表的第一列，与一个「未触达不是相同」的判据

- 日期：2026-11-20｜车道 `lane-opt-twice`，基线 `origin/develop` = `8ff4db7c2`（分支上合并为 `13e8bd2d`）｜依据：`internal/reviews/2026-11-19-the-0.2.45-blocker-list-by-severity.cn.md` 的 S3-2 行，与 `96a166fbb`（2026-11-19 的下发）对此的裁定。

## 1. 这一页是什么，不是什么

S3-2 要交的**不是**「让那条用例变绿」，是**同一张表的两个版本**：

```
① 整程序检查在一次 `std::opt` 里被问了几次
② 每一遍 `opt` 占这一次运行 CPU 的比值
③ 改成「只查波及区域、同一遍内不重复问」之后，同一张表
```

有 ①–③ 前后两版，作者才能判「该改多少」；只有「改完绿了」，那不是读数（下发原文：**不上调上限，先量出慢在哪**）。

这一页把**与主机无关**的那一列先落下来（§4），并把两张表怎么取、取在哪棵树上写死（§6）。表本身还没取：这一步要构建主机，本机不建 C++。

## 2. 那条用例现在是什么状态

`test/plans/feat/opt/splice_place.plan.toml` 的 `perf_fib_smoke.opt_twice`（四后端 `nvm/fvm/jit/prl`）：

- **内容侧不是红**：它钉的是同一张图过两遍 `std::opt`、第二遍没有可拼的、没有 fork 被报出、20 轮 `[NNN] <T> s` 与末行摘要不变（entry 的 `verify_script`）。2026-11-19 的闸门把它列在「时间余量，不是内容」那一组里（`reviews/2026-11-19-the-...-34-reds-on-every-leg.cn.md` 第二节）。
- **红在成本线**：`6480 / 6410 / 6550 / 6440 ms` 对 `20000 / 4 = 5000`。那条线是派生的：条目原先没声明 `cpu_budget_ms`，`test/tools/run-tests.mjs#TIMEOUT_MARGIN` 用 `timeout_ms / 4` 兜底。四个后端**一起**超线，所以它说的是工作，不是抖动。

**已改**：该条目现在写 `cpu_budget_ms = 5000`（`8c6b5cb66`）。数字不动、不上调；改的是**什么来判**——派生的那条线在 host 读不到 CPU 时退回墙钟（那是负载写的数），声明后按工作自身 CPU 判、读不到就 skip。`timeout_ms = 20000` 留作 liveness。等表出来，这个值要按 `2026-11-02-a-timeout-should-be-derived-from-a-measurement-not-a-constant` 重新定，而不是继续继承常量。

## 3. 嫌疑落在哪两处（静态，等表确认）

`std::opt` 每问一次「整程序」的东西，代价是**整张程序**：

- `src/execute/pass.cpp#passBoundaryViolation` 的六项（order / resources / ports / entry-exit / credential / containment），其中 containment 按调用点逐个解析被调函数的足迹；
- `src/passes/opt/inline/engine.cpp`：`472d7a5c4`（2026-11-18）起**每个 rewrite session 结束**都问一遍上面那六项；
- `src/runtime/resources.cpp#commitViolation`：`38cc52241` + `eccc6824a`（同日）把提交的分档改成「按这次编辑」，于是 **Release 也会问** effect + order，且每次提交都新造一张 `NodeAccessTable`（in-place 编辑还要一张 **before 图**的表）。`commitChecksEnabled` 已不参与提交检查（同文件的注释逐字说明：档是草稿加的东西，不是构建的回答）。

裁定给的修法方向是「只查波及区域、同一遍内不重复问」，**不是**改回不问。按「先量后改」，`src/` 现在一行没动。

## 4. 与主机无关的那一列（先落）

`test/tools/opt-cost/measure.mjs` 的计数侧（`-vv --log-include DraftSession,Opt` + `--trace effects`），在同一次 `std::opt` 跑两遍里：

| 计数 | 读数 | 读法 |
| --- | --- | --- |
| draft 会话 | **944** | 一次解码一张图 = 一次草稿，提交就发生在这里 |
| rewrite 总数 | **5254** | `cse 2633 · fold 1847 · dce 513 · order 261`；每一次 rewrite 是一次草稿提交 |

⇒ 这就是「整程序检查被问了几次」的**量级**：不是三十次，是五千次量级。计数不随主机负载移动，所以它现在就能交；表的另外两列（比值、前后对照）要主机。

读数取自 11-11 构建 `3d8feecc0`（`-vv --log-include` 与 `--time-phases` 两侧都在既有二进制上验过）。**`--trace effects` 的 containment 计数本机无法验**：这台机器只有已发版二进制，没有新树的构建，所以那一格在主机上第一次读，工具会明说它没读到、不会写 0（§5）。

**历史锚点，非本轮判据**：`312.5 → 343.8 → 468.8 ms`（`v0.2.39` / `v0.2.40` / 11-11 构建，同机 `nvm`，两遍 `std::opt` 占 `187.5 → 328.1 ms`）是**三个不同构建、同一台机、未控负载**的绝对值。它只能说明「趋势向上」，不能说明「这一遍多了多少」。第一笔提交（`cbc440365`）的信息里写着这组数，那里也照此读。

## 5. 两条判据：未触达不是相同

写这个工具时踩到两个**同形**的错，都记在这里，因为它们不是这个工具的事：

1. **`-vv` 的全量日志约 25 万行，撑爆了 `spawnSync` 的默认缓冲**，于是 `drafts` 读成 **84**（真值 944）——差一个量级，而工具不吭声。
2. 一开始把 `-vv` 与 `--trace effects` 放进**同一次**运行：一个构建接受其一、不接受其二时，两侧会一起读成「检查从未被问过」。

⇒ **未触达不是相同**（a missing reading is not a zero）。一个读不到的数不能写成 0，一个没跑到的分支不能写成「没发现问题」。做法：两个读数拆成两次运行；两侧各自缺读数时打印它自己的 exit 号；日志侧用 `--log-include DraftSession,Opt` 把 25 万行收到约 1900 行（同一批 944），因为**读一份被截断的日志就是读了一个更小的数**。

同形的第三例，不是我的：合并后 `node test/tools/check-plan-fields.mjs` 报 `baseline=318 delta=-1 ratchet=lowered`（exit 0）。根在 S3b 的 `02939ffb7` 摘掉了 `element_assigned_append.in_place_form` 的 `known_fail` 连带四行，净少一个计数字段；棘轮本来就允许降低，**不要为它动 `baseline`**。

## 6. 两张表怎么取（顺序与独占都写死）

前/后是**两个构建**，所以是两次跑，不能合成一次：

```powershell
# A 先：六计划里的 opt，单跑
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan feat/opt -Jobs 8

# B 后：测量，独占一次运行，复用 A 的 install
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> `
     -Plan test/tools/opt-cost/measure.plan.toml -Jobs 1 -NoBuild
```

- **B 不与 A（也不与任何计划）并发**：表里的比值是「同一次运行内」的（`--time-phases` 的每遍 CPU 对深度 0 之和），所以它比绝对值耐负载；但和八个计划抢核会改写每一侧的算术。`feat/opt` 的读数同理，必须单跑。
- **表头必须带构建 sha**：工具已经把它打进第一行（`build Camel v0.2.45 (Build …_g<sha>)`），`--json` 里也有 `build` 字段。一张没有构建的表现在有意义，三小时后没有。
- B 的条目**故意是红的**：`expect_exit = 1`，verify 只负责把表打出来（`test/tools/opt-cost/measure.plan.toml` 的 `desc` 与文件头注释都写着这一条）。它不是闸门，任何 tier 也不会自己选中它。

## 7. 未做的半边

- `src/` 一行没动，等 §6 的 A/B 两张表；改法按裁定是「只查波及区域、同一遍内不重复问」，改完要用**同一张表**的第三版作证。
- `modules.nn / gru_lite_large_bytecode`（`3460` 对 `2500`）在同一条 S3 行里，管线含 `std::opt`；若表 ② 说慢在 `std::opt` 的检查，它与本条同因，届时一并看，不另起一页。