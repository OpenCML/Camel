# 两个 parse 用例读同一份记录，一行 stale 让它们一起红

`feat/parse/frontend` 里有两条红会同时出现：`frontend.ast_record` 与 `frontend.ast_snapshot`。
2026-11-26 有一条车道只改了 12 个 `test/cases/compile/var/*.cml` 的注释，就同时踩到这两条。
这一页记下原因，免得下一个人再查一遍。

## 一、两条用例读的是同一个文件

```powershell
node scripts/check-ast-hashes.mjs --root .            # frontend.ast_record 的 case
python3 test/tools/parsecheck/check.py snapshot        # frontend.ast_snapshot 的 case
```

两条都读 `test/plans/feat/parse/golden/ast_hashes.txt`：前者用 `camel-parsecheck hashes` 重算每行，
断言 stdout 含 `0 stale`；后者跑 snapshot 的只读模式，断言 stdout 含
`files read as recorded, 0 not in the snapshot yet`。所以**一条 stale 行就是两条红**，红的原因只能有一个。

红的时候两条的输出互为线索：

```
[check-ast-hashes] ast hashes: 4572 file(s) read, 5 stale (4 mismatch, 1 missing, 0 gone)
read differently: src/stdlib/rulesets/jvp.cml
    was 1d27a3dc0b0c111c ok
    now 18824ead4d86a29b ok
```

`ast_record` 的判词行报计数（`N stale`），`ast_snapshot` 的报逐文件（`read differently: <path>`）。

## 二、注释改一个字也会动哈希

记录的键是整份 `.cml` 文本，不是语法树：注释里的一个词改了，这一行就 stale。
2026-11-26 那次就是 12 个用例文件只动 ` *` 注释行，记录里 12 行跟着变
（`12 added and 12 removed`），前端构建与图读数一点没变。

## 三、重录的规矩

重录只用同一个仪器，并且只重录自己动过的行：

```powershell
pwsh -NoProfile -File internal\tools\camel-remote.ps1 -Path <工作树> `
  -Plan test/plans/feat/parse/frontend.plan.toml `
  -UpdateAstHashes -AstOnly 'test/cases/compile/var/*.cml' -Jobs 4
```

`-AstOnly` 的窄化是这步的重点：`--update` 不带窄化会把别人没审过的行一起写进去，等于替别人
宣布“这些改动是审过的”。重录完再跑一次只读检查，剩下的 stale 行如果**都不是自己的**，
就把它们报出去、不要重录。2026-11-26 剩下的 5 条
（`src/stdlib/nn/signatures/nn.cml`、`src/stdlib/prelude.cml`、`src/stdlib/rulesets/jvp.cml`、
`src/stdlib/rulesets/transpose.cml`、新用例 `test/cases/runtime/block_scheduler/returns_41.cml`）
就是别的车道的，归它们重录。

## 四、比红表时要注意

`internal/reports/2026-11-26-promotion-9-reds.tsv` 里 `parse.frontend` 只有 `frontend.ast_record`
一行。表是在这两条共用一份记录的前提下只记了一条；`frontend.ast_snapshot` 跟它同红时不进表。
逐名比的时候要按「原因」而不是「行数」判：`ast_snapshot` 多出来一条红，若 `ast_record` 也在表里红，
那就不是新红，是同一件事的另一条读数。