# 实测：引用检查今天报 **27** 条，不是 16 —— 其中 23 条指向的仍是只在 `origin/fsh-dev` 上的那一页

**测的是哪一页**：`internal/questions/2026-11-18-the-citation-check-demands-a-page-that-lives-on-the-authors-branch.cn.md`
（页问：`develop` 上的十六条引用指向一份只活在作者分支上的设计页，检查该不该按分支解析？）

**这一页的结论（一句话）**：页里那个数（16）今天已经变成 **23**（同一个目标页，另有 2 条是同一目标的折行），
整条检查报 **27** 条 unverified citation；页的**结构性事实**仍然成立：
`docs/design/graph-editing/README.cn.md` 在 `develop` 的树里不存在、在 `origin/fsh-dev` 上存在
（`git cat-file -e` 直接验过）。另有 **2** 条与「分支布局」无关（一条引的页两个分支上都没有，
一条是行号式的引用），它们不该被那条裁定顺带赦免。

## 四样东西

| 项 | 值 |
|---|---|
| 总体 | 全树被 `scripts/check-doc-links.mjs --citations` 检查到的引用（`src/`、`include/`、`modules/`、`test/`、`docs/`）；不是「某一份文件里的引用」 |
| 时刻 | 2026-11-19，检出 `c049f90ec`（`lane-ledger` = `origin/develop`） |
| 来源 | `node scripts/check-doc-links.mjs --citations`（**就是闸门那条**：`internal/tools/camel-remote.ps1:433` 的 `check-doc-links` 用的是同一个脚本同一个参数），本地跑，不构建 |
| 前提 | 「引用」按检查器自己的口径（文档链接/引用注释里写的路径），不按 `git grep` 的行数；后者会把问题页自己也算进去 |

## 1. 检查器自己的那一行

```
$ node scripts/check-doc-links.mjs --citations
…
27 unverified citation(s)
exit=1
```

## 2. 27 条按目标分

```
$ $l = node scripts/check-doc-links.mjs --citations 2>&1
$ ($l | Select-String "graph-editing/README.cn.md").Count
23          ← 指向 docs/design/graph-editing/README.cn.md
$ $l | Select-String -NotMatch "graph-editing/README.cn.md"
modules/graph/txn.cpp:467: 'not-a-place.cn.md': no such file
src/compile/gir/builder_control.cpp:4562: 'var_through_with_grad.cml:26': names a line; cite 'var_through_with_grad.cml#Symbol' instead
src/runtime/draft_outline.cpp:7: 'README.cn.md': no such file
src/runtime/resources.cpp:1837: 'README.cn.md': no such file
```

| 类 | 条数 | 与页的关系 |
|---|---|---|
| `docs/design/graph-editing/README.cn.md` | **23** | 就是页说的那一族（页记 16，今天 23） |
| 裸 `README.cn.md`（相对路径） | 2 | **同族的折行**：路径被写成了两行，检查器只读一行 |
| `not-a-place.cn.md` | 1 | **不是同族**：见 §3，它连 `origin/fsh-dev` 上都没有 |
| 行号写法（`cml:26`） | 1 | 与分支布局无关，是引用格式问题 |

那两条「裸 `README.cn.md`」是真实的原文折行，不是另一种写法：

```
src/runtime/draft_outline.cpp:6   … this copies a part of a graph into a callee, and `docs/design/graph-editing/
src/runtime/draft_outline.cpp:7   // README.cn.md` 7 and 15.3 name the two as each other's reverse.
src/runtime/resources.cpp:1836    … the table of docs/design/graph-editing/
src/runtime/resources.cpp:1837    // README.cn.md 8.1 decides which of the six checks a commit is asked, …
```

⇒ 按作者的写法，这两条指的仍是那一页；**检查器按行读，所以它把同一个目标数成了第二条**。
所以「指向 `graph-editing/README.cn.md` 的引用」今天按读者数是 **25** 条，按检查器数成 **23 + 2**。

## 3. 三条能直接判的归属

```
$ git cat-file -e origin/fsh-dev:docs/design/graph-editing/README.cn.md    → 存在
$ git cat-file -e origin/fsh-dev:docs/design/graph-editing/not-a-place.cn.md → 不存在
$ git cat-file -e origin/fsh-dev:docs/design/inline/not-a-place.cn.md       → 不存在
$ Test-Path docs\design\graph-editing\README.cn.md                          → False
$ git ls-tree -r --name-only origin/fsh-dev -- docs/design/graph-editing
docs/design/graph-editing/README.cn.md
```

⇒ **`docs/design/graph-editing/` 在 `origin/fsh-dev` 上只有一个文件**。所以：
23 条 + 2 条折行 = **25 条是「页在作者分支上」那一个原因**；
而 `modules/graph/txn.cpp:467` 引的 `not-a-place.cn.md` **不在任何分支上**，它是一条真坏引用，
不该被「按分支解析」那条裁定一并放过。

## 4. 这一页还剩什么要作者

页的裁定问题（检查该不该认 `origin/fsh-dev`）**没有**被测量settle：它要的是「文档流向」的判定。
测量settle掉的是三件事，且只这三件：

1. 数不是 16，是 **23**（整行写法）+ 2（同一条被折成两行）= **25**，全条检查是 **27**；
2. 缺的那份文件只存在于 `origin/fsh-dev`，`develop` 从来没拥有过它（`Test-Path` False，`cat-file` 两向都验了）；
3. 27 条里有 **2 条与分支布局无关**（`not-a-place.cn.md`、行号写法那条），任何按分支解析的方案都救不了它们 ——
   所以即使作者选了「按 `origin/fsh-dev` 解析」，检查也不会变绿，除非那两条另外修；折行那 2 条则要检查器
   能把跨行的路径接起来，否则它会一直把同一个目标数成两条。

**⇒ 建议作者那一句改成：「按分支解析」+「先把两条真坏引用修掉」。** 前半句是裁定，后半句是测量给的。
## 复读：树在本轮期间动过

lane-ledger 检出时 origin/develop 的尖端是 c049f90ec（2026-11-19 12:41 +0800）；本轮结束时已是
782007912（同日 12:52，「Revert the packing and the rule from develop」）。本页的 27 这个数是
**在 c049f90ec 的树上**跑出来的，**没有**在 782007912 上重跑 —— 那两笔改动不碰 docs/design/
与引用注释，但这句话要给下一次读数留个位置：**新尖端上的数可能已经不是 27。**
