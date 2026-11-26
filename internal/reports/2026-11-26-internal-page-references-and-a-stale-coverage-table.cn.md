# 内部页引用：债比名字小，但生成表整张过期

## 一句话

`test/` 与 `src/` 里对 `internal/` 页面的引用，**人写的那部分基本是好的**（98% 指得着）；真正过期的是**生成的覆盖表** —— 它按整棵树重算，落后 353 个文件、约 4395 行。这两件事混在一起时，看起来像“20 条引用坏了”，实际是“5 条引用要改 + 1 张表要重生成”。

## 口径

```
git grep -n 'internal/' -- test src   = 1394 行 / 143 个文件（test 93 / src 50）、1420 个引用 token、1127 个去重目标
  A 指得着                 1394 条（98.2%）
  B 页在、路径没跟着搬家        17 条（`internal/questions/` → `internal/reviews/`）
  C 目标已撤                  3 条（`internal/tools/build-queue`、`build-queue/locale`、`watch-fsh-dev.ps1`）
```

排除的假线索 26 个，记下来免得再查：
- 17 个是 `#include "tiles/internal/*.h"` 的后缀匹配（21 个头文件都在 `src/stdlib/tiles/include/tiles/internal/`）；
- 4 个是测试自己写进**临时仓**的夹具路径；
- 1 个是跨行断开的路径（`…-spelling-` + 下一行 `spelling.cn.md`，目标存在）；
- 12 行带 `#` 的里只有 1 条是真引用（`camel-gate-state.ps1#Select-LiveCamelWaiters`），且指得着。

## 已修（`lane/ast-rerecord b2cfce8143`，5 文件各 1 行，只改人写的引用）

```
test/plans/feat/compile/transforms.plan.toml:2401   desc 里的 `internal/questions/…` → `internal/reviews/`
test/plans/feat/trans/cml.plan.toml:24              desc 同上
test/plans/feat/modules/graph_edits.plan.toml:10    注释 “the questions of 2026-11-18 under internal/questions/” → reviews/
test/plans/feat/modules/remat.plan.toml:20          注释里那页现名 internal/reviews/questions-2026-11-01-who-owns-the-count-of-a-splits-residuals.md
test/plan-selection.json:278                        删掉已撤的 `internal/tools/watch-fsh-dev.ps1` 与 `:71` 那半句
```

## 判据：裸 grep 归零不是正确判据

`internal/questions/` **仍是活目录**（13 页，且 `plan-selection.json` 有一条目录规则指着它），所以 `git grep 'internal/questions/'` 永远不会是 0。正确的两条是：

```
A：`internal/questions/<page>` 形式、且该页不存在 = 0        （修后剩 15 条，全在生成的覆盖率表里）
B：`internal/tools/build-queue` / `internal/tools/watch-fsh-dev` 的引用 = 0   （修后剩 2 行，同在生成的表里）
```

修完裸口径是 24 行：21 行在 `test/selection/coverage-table.json`（15 错误页 + 6 仍在 questions 的页 + README 行）、3 行是活目录的合法引用（`plan-selection.json` 的目录规则与说明、`run-tests.mjs:431` 引的那页确实还在）。

## 剩下的那张表：属元层，要在尖端重生成一次

```
记录表（4e850d2c5d 生成）：files 8591 / rows 2929 / noneRows 1451 / uncovered 1406
现在生成：                files 8944 / rows 2690 / noneRows 1125 / uncovered 1080
Compare-Object 差异 4395 行；第一处差在第 5 行 inputs 摘要（files 摘要 sha 变了）
生成到临时路径的产物里 `internal/questions/` 只剩 1 条目录行、`build-queue` 0 条 ⇒ 重生成确实消掉那 17 条
```

**为什么不放在某条车道的账上：**表按整棵树生成，重生成会把树里另外 **353 个文件**一起写进提交 —— 那是别人今天的改动，替人补账会把提交的目的弄混。**做法**：尖端稳定时做一次「只读 `--check` → 生成 → 再 `--check`」，一笔提交、说明只写“按当前树重生成”。

## 两条既有红（不是这次 5 行带来的）

```
node test/tools/check-plan-fields.mjs → exit 1，334 行，全是既有 tags/attempt 红，无一行涉及这 5 个文件
node scripts/plan-inventory.mjs       → exit 1，基线同为 exit 1（“4 slow test(s) in files the list does not name”）；
                                        把 5 个文件暂存出树复跑基线，diff 前后均 65 行，文件按原样放回
```

## 给后来人的三句

1. **先分清“人写的引用”与“生成的表”**：前者错了是引用错，后者错了是表过期，修法完全不同。
2. **`internal/questions/` 是活目录**，判据要落到“这一页在不在”，不是“这个前缀出现几次”。
3. **生成表不手改**：只读检查 → 重生成 → 再只读检查；谁改的手写引用，谁不为表的差异负责。