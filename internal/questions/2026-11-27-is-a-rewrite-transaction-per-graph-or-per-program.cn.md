# 重写事务的作用域：一张图，还是整个程序

**状态**：等负责人回答（默认见末节：按现状记为“图级”）

## 问句

一条重写 pass 在提交它的改动时，事务的作用域是**一张图**还是**整个程序**？
现状是**图级**：按可达图逐图 `edit`、逐图 `commit`，**没有被重写的图根本不进事务**，于是它那一路的闸门记账（`flags`）留在 0。

`opt.cxx_isolation` 那一族有两条同族断言，对这件事给了**相反的期望**：
- `.by_cpp` 期望 `0,2,2,2,2`，**绿**，并在断言文字里写出了现状：“exactly one graph is left with no commit having looked at its gates, because this sweep writes only the graphs it rewrites”
- `.by_camel` 期望 `2,2,2,2,2`（“one transaction over the program and commits it once”），**红**
- 两条的**实测值相同**（都是 `0,2,2,2,2`）⇒ 两份实现没有分叉，分叉的是**两句话**

## 读数

```
`src/stdlib/std/cxx/opt/generic/rewrite.cpp:750-794`  rewriteReachableGraphs()
  :757  for (auto *g : session.collectReachableRuntimeGraphs())
  :758-767  if ((mayRewrite && !mayRewrite(*g)) || (memo && memo->leftAlone(passName, g))) { … continue; }
            ⇒ `continue` 在 `session.edit(g)` **之前**：这张图从未进入事务 ⇒ 提交后 `flags` 仍 0
  :768  const size_t made = rewrite(session.edit(g));
  :791-793  if (total == 0) return program;     // 全程无重写时连 commit 都不做

`src/stdlib/std/cxx/opt/generic/generic.cpp:1938-1948`  CommonSubexpressionPass::apply
  rewriteReachableGraphs(context_, program, "std::cxx::cse", cseDraft, nullptr, nullptr, mayMerge)
                                                   记忆 = nullptr       屏幕 = mayMerge（第 7 实参）
⇒ 与 `:758` 的 `(mayRewrite && !mayRewrite(*g))` 一支对应

实测（`D:\Temp\t122\fail\opt.cxx_isolation\cxx_isolation.cse.by_camel`）：flags 按文件顺序 g0=2 g1=2 **g2=0** g3=2 g4=2；
  g2 有 16 个节点（g0:3 / g1:28 / g3:9 / g4:16）⇒ 不是空图被跳过，是**唯一一张 `mayMerge` 判定“无可合并之处”的图**
  验证脚本 `test/plans/feat/opt/cxx_isolation.plan.toml:154` 用 `sort()` 拼序列 ⇒ 0 被排到最前，读起来像“第一张”
```

**设计页没有规定这件事**：`docs/design-principles.cn.md:248` 那句“**图上没有事务**”讲的是“效应不回滚”，
与“重写事务的作用域是一张图还是整个程序”不是同一件事。查遍 `docs/design-principles.cn.md` 没有第二处。

## 两边的理由

**图级（现状）**
```
· 两份实现已经这样；一份断言已经把它写成了事实
· 没被重写的图不需要提交，也就不该被动过 —— 少写一次事务，闸门记账少一轮
```
**程序级**
```
· “程序级一次提交、每张图的闸门都被看过”是更强的一致性保证：一张图被跳过时，闸门记账仍能回答“它被看过”
· 但如果一条 pass 根本没改某张图，去提交它只是为了让计数好看 —— 这正是“为了让检查变绿改语义”的反面
```

## 默认（不等回答也照这个）

**按现状记为“图级”，并把 `.by_camel` 那条断言改成 `0,2,2,2,2`**，理由三条：
```
① 两份实现的实测一致，`.by_cpp` 的断言文字已经记录了这件事 —— 没有“实现分叉”要修
② 设计页对这件事没有规定，按“实现既然一致、且其中一条断言已写明”记录成事实，比按一句没人写过的期望去改实现安全
③ 改断言不是“让红消失”：这两个值之间的差是**一条设计性质**，记下来、由负责人在本节裁定；改完之后
   `.by_camel` 与 `.by_cpp` 的断言文字要一致，**并注明“事务作用域是图级”，引用本节**
```
**可被这两条推翻**：① 负责人裁定事务作用域是程序级（则两份实现都要改，同族断言保持 `2,2,2,2,2`）；
② 出现“被跳过的图没有被闸门看过”造成的实际错误（那就不是记账问题，是漏看）。

## 影响面

- 影响 2 条红（`opt.cxx_isolation/cxx_isolation.cse.by_camel`，以及同族 `fold.by_camel` 的 differential 差另判）；
  都不阻塞 `develop`（不在晋级新红里）。
- 若裁定“图级”：改的是测试断言的期望与文字（测试面），不动 `src/`。
- 若裁定“程序级”：要改 `rewriteReachableGraphs` 的提交位置，两份实现同改，属语义改动 —— 需要设计页先写下来。