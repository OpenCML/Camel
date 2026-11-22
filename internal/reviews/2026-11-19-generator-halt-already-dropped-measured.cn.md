# 实测：生成器状态今天就是 1 元组，`halt` 已由 `6ce3bf1f2` 删掉

**测的是哪一页**：`internal/questions/2026-11-18-generator-halt-has-no-reader.cn.md`
（页问：`halt` 在状态里没有读者，状态元组要不要从 `(pc, halt)` 收成 `(pc)`？两条路 A/B 选一条。）

**这一页的结论（一句话）**：树上已经是 `S = (pc)` 了 —— `halt` 这个成员在 **2026-11-17/18** 由
`6ce3bf1f2` 删掉（提交正文里写着 `answer: drop the generator's halt bit`），页里「路 A 是改语义、
所以先只写注释走路 B」的默认落后于树：**路 A 已经落地**，页不需要作者再裁定形状，只需要被移出等待队列。

## 四样东西

| 项 | 值 |
|---|---|
| 总体 | `origin/develop` 的 `src/` 与 `include/`（整词 `halt`，大小写不敏感），不含 `docs/` |
| 时刻 | 2026-11-19，检出 `c049f90ec`（`lane-ledger` = `origin/develop`） |
| 来源 | `git grep` 与 `git log` 在本地树上跑；状态形状另读 `src/compile/gir/builder_generator.cpp` 原行 |
| 前提 | 「没有读者」按**代码**判，不按注释判：注释里提到 `halt` 不算读者 |

## 1. `src/`、`include/` 里已经一行 `halt` 代码都没有

```
$ git grep -n -i "\bhalt\b" -- src include
src/compile/gct/builder.cpp:6076:    // The state carried a second member until 2026-11-18: a `halt` bit the author had dropped
src/compile/gct/builder.cpp:6077:    // (`6ce3bf1f2`, "answer: drop the generator's halt bit"). It was written where a round answered
src/compile/gct/builder.cpp:6277:    // in. What the tuple held on 2026-11-16 -- `S = (pc, halt)` -- is the reading of
src/compile/gct/builder.cpp:6278:    // `internal/reports/2026-11-16-the-state-tuple-holds-nothing-but-pc-and-halt.cn.md`, kept as
src/compile/gct/builder.cpp:6279:    // it was written: the `halt` member left the tuple on 2026-11-18.
```

五处命中**全是注释**，且都是「它已经不在了」这一句的说明。页里记的那四个读写点
（写 1 于 `6336`/`6421`、写 0 于 `7001`、唯一读于 `6284`）在今天的
`src/compile/gct/builder.cpp` 上**一个都不剩**：那是一个 8826 行的文件，它在
`6081` 处只剩 `const std::string pcCell = "__gen_pc__";` 一行声明。

## 2. 状态参数今天写出来是 `(int,)`

`src/compile/gir/builder_generator.cpp:230` 的原文（注释即签名）：

```
// The step's own signature: `(__gen_state__: (int,)) => (string, string..)?`.
```

同一个函数里，取第 0 成员起名 `__gen_pc__`：

```
:247   const node_handle_t pcMember = currGraph_->addAccsNode(pcType, 0);
:250   insertNode("__gen_pc__", pcCell);
```

⇒ 状态类型是**一元组**，第 0 位是 `pc`，没有第 1 位。页的 §2「路 A：`S = (pc)`」描述的正是今天。

## 3. 删它的那一笔

```
$ git log -1 --format="%h %ad %s" --date=iso 6ce3bf1f2
6ce3bf1f2 2026-11-17 18:47:33 +0000 Merge develop: the generator's cells are one declaration table, the builder's vocabulary gets its own files; answer: drop the generator's halt bit
$ git merge-base --is-ancestor 6ce3bf1f2 origin/develop ; # 退出码 0
  -> ancestor of origin/develop: YES
```

页正文引的那份报告也在：

```
$ Test-Path docs\handover\reports\2026-11-18-the-generator-state-holds-only-pc.cn.md
True
```

⇒ 页的 §1 里「今天的状态元组是 `S = (pc, halt)`，其中 `halt` 是纯负重」这句在
`6ce3bf1f2` 之后就不成立了；页引的报告已经换成 `holds-only-pc` 这一份。

## 4. 还剩哪一件要作者

**一件编辑动作，不是语言问题**：页的默认（路 B：留着 `halt`、只写注释）与树相反。要定的是
「这页按‘路 A 已落地’改写成已结案，还是删掉」——两个都在 `internal/` 里，本页不动它。

顺带一条可核的读数：`builder.cpp:6076-6080` 的注释本身就写着 `6ce3bf1f2` 与
`the-generator-state-holds-only-pc.cn.md`，所以**代码侧已经把这件事记在离改动最近的地方**；
页里担心的「下一个人会再问一次」在代码里已经有答案，只在索引里还没有。
## 复读：树在本轮期间动过

lane-ledger 检出时 origin/develop 的尖端是 c049f90ec（2026-11-19 12:41 +0800）。本轮结束时
它已经是 782007912（2026-11-19 12:52 +0800，「Revert the packing and the rule from develop」）。
上面每一条**代码事实**都在新尖端上用 `git grep/git ls-tree <tree>` 复读过，结论不变；
本页没有在新尖端重跑任何构建或计划。
