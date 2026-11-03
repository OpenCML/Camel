# 踩过的坑（只记有症状、有读数、有改法的）

> 一条三行：症状 / 为什么 / 怎么绕。没有读数的不要写进来，"感觉容易错"不是坑。
> 本页是这一轮归因车道攒下的，后来人继续往下加。

## 1. 动了 `stdlib/*.cml` 的文档行，`registry.reference_is_generated` 立刻红

**症状**：改完 `stdlib/tensor.cml` 一行 `/// @example`，同一个注册表计划里 `registry.reference_is_generated` 变成
`docs/stdlib-reference.md is out of date: run node scripts/stdlib-reference.mjs --write`。

**为什么**：`docs/stdlib-reference.md` 是**生成物**，它把声明的文档行（含 `@example` 原文）印进表里。改了源、没重录，两边就对不上。
改 `stdlib/*.cml` 的任何文档行、签名行、例子，都会这样，不只是例子。

**怎么绕**：同一次改动里跑 `node scripts/stdlib-reference.mjs --write`（只重写这一个文件，实测只动 1 行），
再 `node scripts/stdlib-reference.mjs --verify-doc` 应得 `docs/stdlib-reference.md is up to date`。两条都是纯 node，不需要二进制。

## 2. `test/plan-selection.json`：给"文件自己所在目录"写 exact-path `directoryRule` 会把镜像掐掉

**症状**：想给 `test/plans/feat/compile/rules_angle_args.plan.toml` 补一条目录规则，写成
`{ "path": "test/cases/modules/autodiff/", "plans": [...] }` 之后，原来能选中 autodiff 计划的路径变成了 `Cannot read properties of null (reading 'names')`，`plansForChanges` 也返回不全。

**为什么**：`directoryRules` 是按前缀匹配再从 `rest` 切段下钻的。一个**正好在 prefixes 根下的文件**，`rest == ""` →
`segments == []` → 下钻循环一次都不跑 → 走到末尾 `return null`，而 `null` 的意思是"这条路到此为止，不往下落"。
于是既没匹配到新写的规则，也把原有的镜像挡住了。

**怎么绕**：这种"某个计划读的用例散落在别人的目录里"的情形，用**file rule**（`rules` 数组）配 `ownCases: true`：
`rules.find` 取第一条命中的 file rule，`ownCases` 让计划只在改动文件确实是它自己的 case 时才被选中，不会把一个目录的改动全拖进只读一个程序的计划。

## 3. 数 `GraphDraft` 这类账本时，`-match` 与 `-w` 两个细节会各错一次

**症状**：同一棵树，手工数出 316/317 处引用，而账本 check 读 307；两边差了 9~10。

**为什么**：两件独立的事叠在一起。① PowerShell 的 `-match` **大小写不敏感**，`graphDraft`（局部变量名）被算进了 `GraphDraft`；
要大小写敏感必须用 `-cmatch`。② 账本用 `git grep -w`（整词），所以 `RuntimeGraphDraftSession`、`GraphDraftGroup` 不该算——
不带 `-w` 就多算。另外账本**按 basename 丢掉两个事务文件**（`graph_txn.h` / `graph_txn.cpp`）：裸数 37 个文件，账本读 35。

**怎么绕**：复现账本读数就用账本自己的口径：`git grep -c -w GraphDraft`，再手工减掉事务文件里的出现次数；
不要用 `Get-Content | Select-String -match` 去"验算"，那不是同一个问题。

## 4. 临时探针会被 `camel-remote.ps1` 一起带上主机——跑完必须删

**症状**：`[camel-remote] triage: pushed lane-triage, 0 changed file(s), 11 untracked` ——**未跟踪文件也算"改动"**，
会被 patch 到主机上参与构建和跑计划。

**为什么**：脚本把"HEAD + 工作树 diff（含未跟踪）"整体送到主机。这对临时探针很方便（不用提交就能在主机上跑），
但也意味着**忘删的探针会一直留在主机的工作树里**，而下一轮别人的 `-NoBuild` 复用可能踩到。

**怎么绕**：探针放 `test/cases/...` 与 `test/plans/...` 之外的临时目录优先；一定要走计划跑，就记下用了哪些文件，
测完 `git status --short` 必须干净，删完再提交。探针文本落 `D:\Temp\`，别落仓库。