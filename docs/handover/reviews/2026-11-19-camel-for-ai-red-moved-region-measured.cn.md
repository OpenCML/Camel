# 实测：页里那条 `camel-for-ai` 的红已经修好，今天红的是**另一个 region**（`stdlib-signatures`）

**测的是哪一页**：`docs/handover/questions/2026-11-16-camel-for-ai-diagnostics-doc-stale-on-develop.cn.md`
（页问：`camel-for-ai` 在 `develop` 上就是红的，这份生成物该由谁重生成、要不要每车道各修一遍？）

**这一页的结论（一句话）**：页说「差的是 `IntervalHasNoOpenForm` 那条诊断」——它已经不在树上了：
`docs/reference/camel-for-ai-diagnostics.md` 今天带着这条（`Syntax — errors — 20`），
那一段由 `f6d05103e` 重生成。**检查今天仍然 exit 1，但红在另一个 region**：
`docs/reference/camel-for-ai-stdlib-signatures.md` 的 `stdlib-signatures`（源侧 552 条）。
⇒ **同一类病（改了源、没跑生成器）换了个地方复发，所以页问的「谁重生成」不再是一条一次性的红，而是一条会复发的红。**

## 四样东西

| 项 | 值 |
|---|---|
| 总体 | `docs/reference/` 下由 `scripts/camel-for-ai.mjs` 生成的两个 region（`diagnostics` 与 `stdlib-signatures`），不是整个 `docs/reference/` |
| 时刻 | 2026-11-19，检出 `c049f90ec`（`lane-ledger` = `origin/develop`） |
| 来源 | `node scripts/camel-for-ai.mjs --check`（**就是闸门那条**：`internal/tools/camel-remote.ps1:432` 的 `camel-for-ai` 用的同一个脚本同一个参数），本地跑，不构建 |
| 前提 | 「红/绿」按这个脚本的退出码与它自己那句话判，不按 `git status`；不改生成物（本次没跑 `--write`） |

## 1. 检查器今天的那两行

```
$ node scripts/camel-for-ai.mjs --check
docs/reference/camel-for-ai-stdlib-signatures.md: the 'stdlib-signatures' region is not what its source produces (552 entries); run `node scripts/camel-for-ai.mjs --write`
1 problem(s) in the AI reference
exit=1
```

**只有一条**，而且是 `stdlib-signatures`，不是 `diagnostics`。页里把它写成「一条诊断，不是一片」的那条
（`IntervalHasNoOpenForm`）今天在生成物里：

```
$ git grep -c "IntervalHasNoOpenForm" -- docs/reference/camel-for-ai-diagnostics.md
docs/reference/camel-for-ai-diagnostics.md:1
$ git grep -n "Syntax — errors" -- docs/reference/camel-for-ai-diagnostics.md
docs/reference/camel-for-ai-diagnostics.md:34:**Syntax — errors** (`SyntaxDiag`) — 20
```

页里记的是 `19 → 20` 加上那一条表行；今天正是 `20` 且带那一条 ⇒ **页的 §1 与 §3 描述的差已经被补上**。

## 2. 补上它的那一笔

```
$ git log --oneline -2 -S "IntervalHasNoOpenForm" -- docs/reference/camel-for-ai-diagnostics.md
f6d05103e docs(reference): regenerate the diagnostics region the registry outgrew
```

⇒ 页的默认建议（「按 `registry.tsv → node scripts/camel-for-ai.mjs --write` 重生成，不手改生成物」）
是**按它做完的**，而且是一次单独的重生成提交。

## 3. 今天这条红是谁带来的：两次「改了源、没跑生成器」

```
$ git log --oneline -3 -- docs/reference/camel-for-ai-stdlib-signatures.md
71b046452 docs: the AI reference follows its sources again, and one block stops claiming a counter-example
$ git log -1 --format="%h %ad %s" --date=iso 71b046452
71b046452 2026-11-18 21:31:40 +0800 …
$ git log --oneline "71b046452..origin/develop" -- docs/stdlib-reference.md
88f855ebb refactor: the optimizer's old sub-pass names are gone, and `std::opt` stays as the default pipeline's name
3d9cbbc08 feat: `outline` makes a group of a graph's nodes a function the commit delivers
```

两次都**只改了源**（`git show --name-only` 各只回 `docs/stdlib-reference.md` 一行），
**没有一次带上生成物** ⇒ 就是页里 §「为什么它值得一页」说的那个动作漏了，只是换到了另一个 region。

## 4. 这一页还剩什么要作者

页的 §3 已经把它自己划成两半：「重生成」是执行动作（做了），「要不要一道墙拦它」留给作者。
今天能加给作者的只有**新的那一半证据**：

1. 这条红**确实会复发**，而且在同一天里复发到了第二个 region（`diagnostics` → `stdlib-signatures`）；
2. 复发的方式一模一样：**改源那一笔不跑生成器**，两笔都可点名（`88f855ebb`、`3d9cbbc08`）；
3. 所以「每车道各修一遍」这个选项今天更贵了 —— 每复发一次，所有车道都要再证明一次「不是我的」。

**⇒ 要作者裁的那一句没变**（要不要把它变成提交检查表上的一条），但它现在有了可引的复发记录，
不再是一条一次性的红。