# 2026-11-22 合入轮的失败用例与归因

作者 2026-11-22 的合并纪律第 1 条要求：把这一轮的失败用例列出来，逐条标明是哪条车道带来的。

本页是那一份。**先说一件事：这一轮真正属于「某条车道带进来的」只有一类，其余是余量问题。**

## 一、时间线（每跑一条 CI，按提交排序）

| run | develop 头 | 红腿 | 红用例 |
|---|---|---|---|
| `45001457388` | `1fa180c4f` | **无** | —— 搬家窗口关闭的那一跑，五腿全绿 |
| `45003684718` | `2d384c3c4` | mac / nojit / linux / win | 见 §二 |
| `45005763652` | `c73cd218f` | nojit | `residual_mlp_large_bytecode`、`root_window.residual_mlp.moving_stress` |
| `45007258569` | `4f9315a82` | mac / linux / nojit | `registry.reference_is_generated` ×3 腿，加 nojit 的三条余量红 |
| `45009101214` | `225baaebb` | **无** | —— 五腿全绿 |

## 二、`2d384c3c4` 那一轮：六条阶段 2 车道一起合入

| 用例 | 红腿 | 归因 |
|---|---|---|
| `frontend.ast_snapshot` | mac / nojit / linux / win | **一次 `.cml` 语义改动没有重录 AST 记录**：`lib/std/prelude.cml` 加了 `copy`，而第七道闸门当时只是脑子里的记忆、不是命令 |
| `lsp.editing` | mac / nojit / linux / win | 同上：补全清单里多出 `copy`，期望列表还把它列为排除项 |
| `docs.ai_reference.current` | mac / nojit / linux / win | 同上：内置名字 66 → 67 |
| `gate.plan_inventory`、`gate.case_ledger` | nojit | `scripts/plan-inventory.mjs:312` 的 `walkTreeFiles` 对已搬走的 `src/` 报 ENOENT |
| `plan_refs.every_case_balances` | nojit | 余量，见 §三 |

**修法**：`b5c8eb6ee`（快照重录 + AI 页 + LSP 期望三件一起）与 `af92a56bd`（`walkTreeFiles` 跳过不存在的根）。

**这一轮的根子是同一个**：合并方的判据是「六条车道各自都交了工」，而它们的**交付条件不同** —— 头文件要审、适配层要跑、`.cml` 改动要重录记录。把条件不同的笔合成一次合入，就是用最松的那个条件放行了最严的那一笔。作者当轮的流程批评说的正是这件事。

## 三、`nojit` 的三条：不是答案错，是余量不足

三条的 stderr 是同一句：

```
below the 4x margin: the bound is the work's own time, not a bound
(raise it with a stated reason, or make the test do less)
```

| 用例 | 实际 CPU | `timeout_ms` | 余量 |
|---|---|---|---|
| `residual_mlp_large_bytecode` | 3530 ms | 12000 | **3.4x** |
| `root_window.residual_mlp.moving_stress` | 45770 ms | 120000 | **2.6x** |
| `plan_refs.every_case_balances` | 316220 ms | 1200000 | **3.8x** |

**它们算出来的答案都是对的**：`residual_mlp` 的 `value=0.7921590805053711` 与 `start` 相同；`every_case_balances` 报 `unbalanced=0 skipped=1234 silent=0`。

**而且它们在 `45009101214` 上又绿了**（同一段代码、同一个 `timeout_ms`）。所以这不是稳定的红，是**游走的**：`nojit` 腿本来就慢，邻居一忙就掉到 4x 线以下。

**归因**：六条阶段 2 合入加了真实工作量（`shapeOfKind` 分派等），把这三条从「余量够」推到「余量刚好在线上」。**不是某一条车道的逻辑错。**

**处置建议（未做，等作者裁）**：把这三条的 `timeout_ms` 按 nojit 腿的实测抬到 4x 以上，**并在 plan 里写明理由**（`agents/test.md` 要求「raise it with a stated reason」）。不抬，它就会周期性地红，而每次红都会像这次一样花掉一次 CI 的 20 分钟去分辨「真错还是余量」。

## 四、`45007258569`：`registry.reference_is_generated` ×3 腿

**唯一一条明确由某条车道带进来的红。**

`docp` 车道（`4f9315a82`，106 文件 / 1254 处路径）把 `docs/stdlib-reference.md` 当成「活页」手改了 42 处路径。而它是**生成物**：生成器把 `lib/**/*.cml` 的 doc 散文**逐字抄**进页面（`scripts/stdlib-reference.mjs:82-83` → `:460-470` → `:1192`）。

**它改的 42 条路径全都真实存在**（旧文本 41/42 指向已不存在的路径），所以错不在挪错，而在**改了产物却没改产物抄的来源**。

**修法**：`765895ebe`（生成物与源逐字节对齐，先让 CI 绿）+ `8a4535b82`（改 `lib/**/*.cml` 里 184 处散文，再重生成）+ `a1c360667`（生成器自己的页头常量）。

**这条的教训不是「别手改生成物」那么简单** —— 手改的动机是对的（路径确实旧了）。教训是：**一个能从源生成的页面，你改它一次，就同时失去了「它是生成的」这个性质与「它和源一致」这个判据。**

## 五、这一轮之后仍然开着的

- `nojit` 三条的 `timeout_ms` 未抬（见 §三，等裁）。
- `scripts/layout-move.mjs --check` 在搬家后的树上永远 exit 1（`ENOENT …/modules`）。搬家工具的验收判据只有它被造出来时的那棵树，这条能力已经没有了。
- `gpu-plans.exceptions.txt` 的坑：跳过 46 条的那一跑会让工具相信那 13 个名字已修好。**不要让它删。**