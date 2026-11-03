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
## 5. 判据算出了答案却不交出去——"先算后丢"，不是"少一个 if"

**症状**：`func three(): float[3] { return [1.5, 2.5, 3.5] }` 被拒，而同一份字面量在声明、传参、`as` 三处都过。
探针读数是 `declared=float[3] written=float[] array=1/1 eq=0`、`arr=1 raw=3 fits=1 elem=float` ——**形状判据说"相符"，值却还是开放数组**。

**为什么**：`arrayLiteralFitsShape`（`src/compile/gir/builder_control.cpp`）的秩一分支
`if (extents.size() < 2) return holdsStatedLength(type, array.get());`
**直接返回，出参 `lengths` 一次也没写过**——只有秩二那条路会填它。下游 `ArrayDataFactory` 于是按"没有 extent"建出 `f32[]`。
判据判过了，但**判过的那个长度没交出去**；下游不是判错，是拿不到。

**怎么绕**：看到"bool 返回 + 出参"的判据，**在每条 `return true` 前问一句"出参这时是全的吗"**；
出参是可选指针（`= nullptr`）或"只在一条分支上写"的，就是这一类的候选。修的时候从**位置的声明**取值，别去下游补（下游没有那个信息）。

**同类第二处（形状相同，2026-11-03 已按裁定补齐）**：`Builder::compositeWithHolesInType`（同文件）原先只有 **struct** 分支写 `holeSlots`
（末尾 `holeSlots = std::move(slots)`），**array 分支直接 `return factory.build()`**。它当时不坏，只因为调用方 `builder_expr.cpp` 先把
`holeSlots` 预置成 `holes`（数组是位置性的，索引相同）——**是预置的默认值把断线挡住了，不是没断线**。现在 array 分支也写
`holeSlots = refHoles`，**调用方的预置保留**（两道都在、互为印证），于是以后多一个不预置的调用方也拿不到字面量自己的索引。
这一笔的判据是"**行为不变**"：`feat/compile` 2197 = 2192/2/3、`casts` 16/16、`parse` 115/115、`compile/transforms` 80 = 77/2/1、
`split` 19/19、`opt` 1174/1174，**六个读数与改之前逐字相同**——若有一个变了，说明它今天其实在用，就该回滚而不是庆祝。

**已核过、不属于这一类**：`flattenLiteral`（每层先 push 自己的长度，货真价实地交出去）、
`ArrayType::extentForms`（`true` 时 `out` 总是整份）、`holdsStatedLength`（纯判据，没有可交的东西）。

**近似但不算的一处**：`src/compile/gct/builder.cpp#literalShapeOf`。每层都填 `lengths`/`at`，但**ragged 时留下的是半份 `lengths`**、
同时把 `shaped` 置 false——靠调用方先看 `shaped` 才不出事。不是"不交"，是"交了半份、靠标志位兜"。它旁边现在写了这句话
（`lengths` 半份、调用方必须先看 `shaped`；谁先读 `lengths` 后看 `shaped` 就是同一个事故）。把半份改成整份会动行为，**没做**。
