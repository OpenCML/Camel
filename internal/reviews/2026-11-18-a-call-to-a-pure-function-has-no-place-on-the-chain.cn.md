# A call to a pure function has no place on the chain, so two of the three rows read the same

**Question for the language author.** The ruling says inlining **at a statement takes over its
place**, and **at a pure node only picks the region**. Built as an operator, that gives three rows,
and the middle one is empty for a case the ruling seems to name.

## What was measured

```
at a call site of its own     → expanded whole, the call erased
at a statement on the chain   → the body takes the statement's place; prints 2 -> 1
at neither                    → the body is put inside the target's region; chain byte-identical
```

A bare call to a pure function -- `inc(2)` as an expression, not a statement -- falls in the third
row, because the builder does not put such a call on the chain at all. So the reading for "at a call"
and the reading for "at a pure node" are **identical**: `inc_calls=2->2 body=0->1 chain_same=true`.

## The question

The ruling's phrase is "at a statement". A pure call is not a statement in the graph, but it is a
call in the source, and the sentence reads as if it is talking about what the author wrote. So:

**Is a pure call's absence from the chain a property to respect -- the third row is correct and the
ruling means the graph's statements -- or is the intent that a call, being a place the author wrote,
must be given a place before inlining can take it over?**

## Default, and work started on it

**Default: the third row is correct; no place is manufactured.** Reasons: (1) the graph is what the
ruling is about, and the graph has no place there; (2) manufacturing one would put a node on the
chain that no statement asked for, which changes execution order for a call the language has decided
carries none; (3) the observable behaviour of the two readings is the same today, so the difference
would be a structural one with no test that could prefer either -- and a choice made with nothing
able to disagree with it is exactly the kind this week's other findings were about.

This round does not wait: the third row is what is landed and pinned. If the answer is "give it a
place", the next lane adds one row and one reading; nothing already landed has to move.

## Recorded separately, not a question

The second branch's body is currently read by nothing. An operator cannot reach its region's exit
gate, so the body goes into the graph and the commit keeps it as dead code. That half of "only picks
the region" has no road today. Stated here as a fact measured twice, not as a question with a
default -- a default about it would decide what the exit gate is for, which is a larger question.

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

第三行对，不给纯调用造位置。原则 3.4 说"纯计算的语句不在链上"，并说这个分类"处处同用：离开同步链……内联后不连顺序边"；图编辑设计稿 `inline` 行也写着"`at` 是纯节点时只选区域"。
