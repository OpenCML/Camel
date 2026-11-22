# `GATE` 的「值」有五个走法，而在空 gate 上它们并不一致：该统一成哪一种？

**日期**：2026-11-19
**提出者**：车道 O（`lane-facts`，收敛清单第 ④ 件「`GATE` 访问函数」）
**状态**：待作者裁定（本页写死默认建议，按默认当轮开工；**这一轮的开工就是不合代码**）
**触发**：收敛清单要求「`GATE` 布局一个访问函数」。数完发现不是一份代码抄了五遍，而是**五份约定不同**的代码。

## 1. 数出来的 N

在同一棵树上量的（`943d97aee` + 车道 O 的 4 个文件；`git grep -n -E "normInputsOf\([a-zA-Z_]+\)\.(front|back)\(\)"`）：

```
具名 4 条（其中 split 那条已经只是转调 exitValueOf，不是第五种读法）
  include/camel/runtime/draft.h      ungated(draft, node)        9 个调用者（pullback 7、generic 2）
  src/execute/exec_plan.cpp          valueBehindGates(graph, at)
  src/runtime/resources.cpp          throughGates(graph, ref)
  modules/transforms/exit_gate.h     exitValueOf(gate, ref) / exitWaitsOf(gate, ref)
内联 11 条（判 kind==Gate 之后自己读输入的循环）
  src/runtime/loop.cpp#LoopDraftView::nextState        src/runtime/draft.cpp           两处
  src/runtime/draft_writes.cpp                         src/runtime/graph.cpp
  src/runtime/operator_instance.cpp                    src/passes/opt/generic/generic.cpp
  src/passes/opt/generic/screen.cpp                    src/passes/opt/inline/engine.cpp
  src/passes/trans/cpp/function_emitter.cpp 两处      modules/autodiff/pullback.cpp
⇒ 15 条循环，分布在 12 个文件，没有一条是另一条的调用者。
```

## 2. 三条轴上它们不一致（这才是要裁的东西）

```
轴一 读哪个输入：.front()（13 条） vs .back()（2 条）
  modules/transforms/exit_gate.h 的注释记着裁定（0780a83bd）：「a gate's trailing input is a wait
  rather than a use」，所以 .back() 读的是**最后那次等待**，不是值；split 的 ungated 正是因为这个
  裁定才改成 front。**而 draft.h#ungated 仍是 .back()，它恰好是唯一带 9 个调用者的那一条。**
轴二 一个没有普通输入、只有 with 输入的 gate（捕获出来的值）：停在 gate 本身（14 条）
  vs 回落到 with.front()（exitValueOf）。前者把 gate 自己当成值交给调用者。
轴三 走多少步：无界 / guard 64（exec_plan、loop）/ guard nodeCount()+containsNodeRef（resources）。
```

## 3. 问句

**统一成哪一种约定？** 三条路：

```
A（默认） 取 exitValueOf 那一种：读 .front()，没有普通输入时回落 with.front()，两者都没有时答
        kInvalidNodeRef，并把答夹在 [0, nodeCount) 内。
B        取其共同点：读 .front()，没有普通输入就停在 gate 本身（= 今天 14 条的行为）。
C        不合并，各留各的（= 承认这 15 条问的不是同一个问题）。
```

## 4. 默认建议（按此当轮开工）

**取 A**，理由是它与 B 的关系可以一句话说清：**在所有「B 有答案」的输入上，A 和 B 相同（都取 `front()`），
只在「B 没有答案」的输入（空 gate）上多给一个答案。** 「未定义」不能靠合并变得一致，所以只能选一个
有定义的那份；`exitValueOf` 是唯一有定义的那份，且它是新写的、注释里已经写明它是「that one reading」。

**开工顺序（等点名，本轮不写代码）**：

```
① 先量一条：语料里到达 draft.h#ungated 的 gate，有没有带等待输入？
   ⇒ 若有，那 9 个调用者（pullback、generic 的 key）今天读的就是**最后那次等待**，
     改 .front() 会改行为 ⇒ 必须先量、先报，不能当重构做。
② 访问函数提到内核：include/camel/runtime/graph.h 增 valueBehindGates / gateWaitsOf，
   modules/transforms/exit_gate.h 删除并入（它的两个函数正是这两个）。
③ 15 条逐个改成调用内核那一份。
④ src/runtime/resources.cpp 那一条在 wide 手里 ⇒ 等它合入，否则就是两条车道改同一文件。
```

**若作者在 §3 选 B 或 C，请改本节。** 若选 A，§4 的 ① 仍是第一件事：它决定这次改动是重构还是修缺陷。

## 5. 相关页

- `internal/reviews/questions-2026-10-29.md`「每日审查（2026-11-18）」第 5 条 —— 本页的出处
- 车道图（`fsh-dev` 分支上的 `2026-11-18-lane-map.cn.md`，本分支不含此文件）车道 O 第 ④ 件
- `internal/questions/2026-11-19-the-open-loop-give-back-is-12-call-sites.cn.md` —— 同车道的第 ⑤ 件