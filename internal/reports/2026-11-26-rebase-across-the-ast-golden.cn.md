# 换底撞 `ast_hashes.txt` 的正确做法（一次现成样例）

这份页面来自一次真实的换底：一条车道改了三件东西 —— 用例
`test/cases/runtime/oper_shape/two_forms_one_shape.cml`、它的计划
`test/plans/feat/runtime/oper_shape.plan.toml`、以及该用例在
`test/plans/feat/parse/golden/ast_hashes.txt` 里的那一行。换底到 `origin/integration` 时，
冲突正好落在 golden 上（别人的车道也在改同一份记录的别处）。

## 做法（三步，全程不手解）

1. **取对侧整份。** rebase 停在冲突时，对 golden 取「被换底到的分支」那一侧：

   ```
   git checkout --ours -- test/plans/feat/parse/golden/ast_hashes.txt
   git add test/plans/feat/parse/golden/ast_hashes.txt
   git -c core.editor=":" rebase --continue
   ```

   rebase 期间 `--ours` 是**目标分支**（integration），`--theirs` 才是自己那一笔。
   效果：别人的行原样留在记录里，自己那行退回旧值 ⇒ 换底后相对 integration 的 diff 里
   **暂时没有** golden 改动 —— 缺口是显式的，不是被掩盖的。

2. **用记录器重录自己那几行**（glob 只圈自己的用例）：

   ```
   internal/tools/camel-remote.ps1 -Path <树> -UpdateAstHashes -AstOnly "test/cases/runtime/oper_shape/*.cml" -Jobs 8 -NoGpuWatch
   ```

   `check.py snapshot --update --only` 的语义要读清：**`--only` 决定改写哪些行，不决定 hash 哪些文件。**
   没进记录的 `.cml` 会被一并 hash（记录不许留缺口），所以「挡路的不是我的文件」躲不开，
   只能等挡路的原因被修掉（见下面「被挡住的读数」）。

3. **只读复核。** 跑 `feat/parse` 的 `frontend.ast_record`（`check-ast-hashes.mjs` 的 `0 stale` 计数），
   贴原文 —— 这是「记录是解析器的读数」的唯一证据。

## 为什么不能手解

手解 golden 就是**用人写一条解析器读数**；这条读数必须由解析器产出。历史上一天里四个车道
各自手补行，当晚记录又 stale。`frontend.ast_record` 之所以为这类读数单独存在，
就因为它读的是 hash（覆盖每个节点的 token 范围与起点，连 `//` 注释移动都会改它），不是文本。

## 为什么不能「临时把挡路文件移走再重录」

`check.py snapshot --update` 的 drop guard 用三条一起判「记录变短是不是丢行」：
磁盘上还有没有这个文件、base 分支里有没有、有没有被 `--drop` 点名。把挡路的 `.cml` 临时移走，
它们就从 corpus 里消失，「被挤掉的行」落进 guard 看不见的那一类（不在磁盘、也不在 base），
记录于是合法地少几行 —— 2026-11-17 那次 8 行丢失正是这个形状。要删行只能走 `--drop`，且每行都会打印。

## 被挡住的读数（同形状的人可直接复用）

挡路物：7 个 `test/cases/probe/editview/*.cml`（golden 里 0 行 = 还没进记录），
`camel-format` 没有 `EditExpr` / `ViewExpr` 规则 ⇒ `camel-parsecheck hashes` exit 1 ⇒
`run_hashes` 返回 None ⇒ `AST_UPDATE_EXIT=1`、`__CAMEL_REMOTE_AST_FAILED__`、写回被拒：

```
error test/cases/probe/editview/sec12_escape.cml: camel-format: no rule for ViewExpr
error test/cases/probe/editview/sec12_step.cml:   camel-format: no rule for EditExpr
error test/cases/probe/editview/sec2_count.cml:   camel-format: no rule for ViewExpr
error test/cases/probe/editview/sec2_fold.cml:    camel-format: no rule for EditExpr
error test/cases/probe/editview/sec2_log.cml:     camel-format: no rule for EditExpr
error test/cases/probe/editview/syntax_only.cml:  camel-format: no rule for EditExpr
error test/cases/probe/editview/view_only.cml:    camel-format: no rule for ViewExpr
```

被挡住的那一行应该是什么，host 已经算出来了：

```
670031c55d206d6c ok test/cases/runtime/oper_shape/two_forms_one_shape.cml
51b8c3c912e51248 ok test/cases/runtime/oper_shape/cast_pair_conversion.cml
```

golden 现值是 `a5fee9d295247e69`（第一条）；第二条一字未动 ⇒ 解锁后写回**只会改这 1 行**。

同一根因让 `frontend.ast_record` 在尖端上给不出 stale 计数：

```
[check-ast-hashes] out/latest/bin/camel-parsecheck hashes exited 1 after 0.7 s (chunk 4 of 4)
[check-ast-hashes] refusing: a run that did not read the tree has no stale count to report
```

## 一句话

换底撞 golden 时：**取对侧整份 → 用 `--only` 重录自己那几行 → 只读复核**；
不手解、不移挡路文件、不替别人重录。