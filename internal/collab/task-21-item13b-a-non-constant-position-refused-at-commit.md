# `task-21` 第 13 条②：「位置不是常量的图在提交时被拒」——原文、候选、下一步

**审查原话（已找到，`internal/plans/2026-11-26-daily-review.cn.md:22`，第 13 条）**

> 13. **`:op/fill`、`:op/access`、`:op/access_key` 改成不会故障，没有测试钉住**：加用例钉住这三类节点可以自由安排；连同"位置不是常量的图在提交时被拒"那一条一起。

也就是说这一条要钉的是**两件**：① 这三类节点可以自由安排（**已完成**，`lane/op-names @ 534ee298d9` 的 `facts.arrangeable_fill_access`）；② **位置不是常量的图在提交时被拒**（未做）。

## 已排除（三次 grep，`lane-kinds-oper` 2026-11-28）

```
git grep -n "not a constant" -- src/compile/src/gct src/compile/src/gir   → 9 行，8 行注释
  唯一真实拒绝：src/compile/src/gir/builder_when.cpp:790
      fail(EvalProblem::Construct, {}, "the key of the access is not a constant");
git grep -n "must be constant|not constant|constant.*position|position.*constant" -- src/compile/src/{gct,gir}
  → 全是字面量 extent（builder_data.cpp:705,1004）、折常量参数（builder_call.cpp:2159,2651）、constas（builder_generics.cpp:1822）
git grep -rln "CommitCheck|checkCommit|kCommit" -- src/compile/src  → 0
  src/compile/src/gct/commit.cpp 不存在
```

⇒ 编译器里**没有"提交检查"这一族**；这棵树里的 `commit` 是**草稿提交**（`src/kernel/runtime/draft*.cpp`、`graph.cml` 的 `commit`/`commit_of`）。

## 下一步（给接手的人，三次读 + 一次跑，不用猜）

1. 读 `src/kernel/runtime/draft*.cpp`（尤其 `draft_session.cpp`、`draft_inline.cpp`）里的提交路径，`git grep -n "position\|constant" src/kernel/runtime/draft*`，找"位置必须是常量"这类判据。
2. 读 `src/stdlib/graph.cml` 的 `fill`/`access`/`access_key` 声明与它们的图级实现（`src/stdlib/graph/*.cpp`），看"位置"在这些算子上是哪个形参、何处要求常量。
3. 若两处都没有 ⇒ **停下报协调者**：把审查那句与三次 grep 的原文一起贴出，说明"提交时拒"这句在尖端上**找不到实现**（那就成了缺陷：审查以为有、实际没有）。
4. 找到后：写一条 `.cml`（访问键/位置由参数决定 ⇒ 非常量），断言诊断原文与位置；落真计划条目并跑读数。

## 口径

- **不许**按名字推（`builder_when.cpp:790` 是常量求值里的拒绝，未必是审查说的那一半）。
- **不许**为凑绿写一条测别的（这三类节点的"可自由安排"已经钉住了，这一条要的是**拒绝**）。
- 完成时把"审查说的那一半在尖端上是否成立"写进结论：成立 ⇒ 用例；不成立 ⇒ 缺陷页 + 一行 backlog。