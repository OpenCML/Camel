# 实测：`casevm.cpp` 已经不在树里，页问的「删还是修」是删之前的问题

**测的是哪一页**：`docs/handover/questions/2026-11-16-casevm-does-not-compile-in-any-configuration.cn.md`
（页问：`casevm.cpp` 在任何配置下都编不出来，删掉还是连二十处一起修？）

**这一页的结论（一句话）**：树上已经没有 `casevm.cpp`，也没有 `ENABLE_FASTVM_COMPUTED_GOTO` 这个选项；
删它的那笔 `468685ec9` 在 **2026-11-16 21:41 +0800** 就进了 `origin/develop`。所以页里那两个选项，
树已经选了「删掉这一支」，本页不需要作者裁定，需要的是把它从等待队列里移走（移走是编辑动作，不是语言问题）。

## 四样东西

| 项 | 值 |
|---|---|
| 总体 | `origin/develop` 的源码树（`src/`、`include/`、`cmake/`、`modules/`），不含 `docs/` |
| 时刻 | 2026-11-19，检出 `c049f90ec`（`lane-ledger` = `origin/develop`） |
| 来源 | 本页每一节都带命令与原行，命令在本地树上跑（不构建） |
| 前提 | 只用 `git grep` 与 `git log`；`docs/` 里的命中一律不计入「代码里还有没有」 |

## 1. 文件不在

```
$ Test-Path src\passes\sched\fastvm\casevm.cpp
False
$ git ls-files src/passes/sched/fastvm/casevm.cpp
（空）
$ (Get-ChildItem src\passes\sched\fastvm\*.cpp).Count
11
```

目录里现在是十一个 `.cpp`（不含 `jit/` 子目录），没有 `casevm.cpp`：

```
bcdump.cpp  bytecode.cpp  compile.cpp  fastvm_stats.cpp  fastvm.cpp  gotovm.cpp
hosted.cpp  inspectvm.cpp  native_call.cpp  optimize.cpp  runtime_support.cpp
```

`git grep -n casevm` 全树只剩**文档**命中（本问题页、`docs/handover/reports/` 三处、`reviews/` 两处），
`src/`、`include/`、`cmake/`、`test/` 一处也没有 ⇒ 它不是「编不过」，而是**没有被任何一个构建列表提到过**。

## 2. 选项也不在

```
$ git grep -n ENABLE_FASTVM_COMPUTED_GOTO
docs/handover/questions/2026-11-16-casevm-does-not-compile-in-any-configuration.cn.md:8,9,15,39
docs/handover/reports/2026-11-16-what-the-missing-count-was-and-was-not.cn.md:62
docs/handover/reports/code-review-backends.md:14
```

`cmake/Options.cmake` 里同位置留下的是 `ENABLE_FASTVM_JIT`（`:55`），没有 `COMPUTED_GOTO` 那一项
（`Select-String -Path cmake\Options.cmake -Pattern "ENABLE_FASTVM_JIT|COMPUTED_GOTO"` 只回一行）。
⇒ 页里那句「`cmake/Options.cmake:63` 把 `ENABLE_FASTVM_COMPUTED_GOTO` 默认设为 `ON`」描述的树已经不存在。

## 3. 删它的那一笔，且是 `develop` 的祖先

```
$ git log --oneline --diff-filter=D -3 -- src/passes/sched/fastvm/casevm.cpp
468685ec9 refactor(fastvm): delete the second interpreter body and the option that selected it
$ git log -1 --format="%h %ad %s" --date=iso 468685ec9
468685ec9 2026-11-16 21:41:16 +0800 refactor(fastvm): delete the second interpreter body and the option that selected it
$ git merge-base --is-ancestor 468685ec9 origin/develop ; # 退出码 0
468685ec9 is an ancestor of origin/develop
```

同一笔也是这个选项消失的那一笔（`git log -S "ENABLE_FASTVM_COMPUTED_GOTO" -- cmake/Options.cmake`
的第一行就是它）⇒ **文件与选项是同一次删掉的**，不存在「文件删了、选项还在」的残留。

## 4. 这一页里还剩什么不是事实

页的第 5 节列了那二十处错误（`unknown type name 'Frame'` 等），那是**删之前**在 `/tmp` 副本上的读数，
今天无法重跑，因为文件本身不在了。这不影响结论：无论那二十处是二十还是二百，文件已经不在构建里。

**唯一留给人的动作**（不是裁定）：把这页连同 `docs/handover/reports/2026-11-16-what-the-missing-count-was-and-was-not.cn.md`
里的 `casevm` 一段一起标成「已由 `468685ec9` 结案」。本页不改那两个文件。

## 附：这一页为什么曾经显得像要裁定

它的默认建议是「本轮什么都不做，但把它记成待办」——而记成待办之后，下一笔提交选的就是删掉它。
当页面的默认和树的实际走向相反时，只有命令能分辨谁落后：`git merge-base --is-ancestor` 说删的那一支在 `develop` 里。
## 复读：树在本轮期间动过

lane-ledger 检出时 origin/develop 的尖端是 c049f90ec（2026-11-19 12:41 +0800）。本轮结束时
它已经是 782007912（2026-11-19 12:52 +0800，「Revert the packing and the rule from develop」）。
上面每一条**代码事实**都在新尖端上用 `git grep/git ls-tree <tree>` 复读过，结论不变；
本页没有在新尖端重跑任何构建或计划。
