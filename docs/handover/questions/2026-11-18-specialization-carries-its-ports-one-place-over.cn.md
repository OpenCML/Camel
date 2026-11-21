# Specialization publishes its ports one place over from its own signature

> **答复在第五节。** 本页前四节保留提问当时的原样。

**Question for the language author.** Found while placing a port-type check; not the thing that lane
was sent to do, and it is the reason that check cannot be asked of every graph.

## What was measured

Putting the port-type check at seal -- where every commit asks every graph -- turns **67 cases red
across two families, 137 places**. Every one of them is a graph the compiler built for itself:
`*$spec*`, `main$lift0`, `split_of_jvp_of_*_backward`.

The one that names the shape:

```
f1$spec0 declares parameter 0 as 'bool'
   and its port node 1 carries the type of parameter 1
```

So the normalized port list of a specialized graph is **shifted one place** against the signature
that same graph declares. It is not that a type is wrong; it is that the list and the signature
disagree about which port is which.

That is a builder/specialize defect. It was not visible before because nothing compared the two.

## Why it blocked the check, and what the check does instead

Asking unconditionally needs this fixed first: otherwise the check refuses the compiler's own
outputs, and refusing a correct graph is worse than not asking. So the check is put only to graphs
**this commit gave a new signature** -- ones whose previous signature exists and differs. A graph
built fresh has no old signature to disagree with.

## Default, and work started on it

**Default: treat this as a defect in specialization and fix the shift, then ask the port check
unconditionally.** The narrow placement is a holding position, not the intended end state -- it
leaves fresh graphs unasked, and a graph that is wrong from the moment it is made is exactly the
kind this week's other four findings were.

This round's work does not wait on the answer: the narrow placement is landed and green, so the
ruling only decides whether the next lane fixes the shift and widens the check. If the answer is
that the shift is intended -- the port list and the signature legitimately index differently -- then
the check should compare against the port list's own order rather than against the signature, and
this page should be read as recording that the two orders differ by design. That reading is not
adopted here because the two are described as the same thing everywhere else in the code.

## 五、答复与本轮的落地（作者 `b68c04b31`；2026-11-18，车道 `lane-ports`）

作者裁定：**错位是缺陷；先修错位，再把端口检查问遍每一张图。** 上面第二节那句「只问被改过签名
的图」按此撤销。

**一、错位复现了，而且树里本来就带着一个。** `test/cases/opt/generic/specialize_dim_name_recursion.cml`
的 `walk$spec0` 声明 `(n: int, xs: f32[n]) => f32 pure`，而它只有**一个**端口节点：类型 `f32[n]`、
标签还是 `n`。声明说的是参数 0 是 `int`，那个端口端的是参数 1。这不是「某个类型错了」，是两份清单
对不上。

**二、把检查问遍每张图、错位还没修时，读数和本页第一节一致。** `opt`+`compile` 两个计划：
`[SUMMARY] total: 4240, pass: 4160, fail: 64, known failures: 16`，被拒的图去重后 24 张，全是编译器
自己造的：`f1$spec0`、`walk$spec0`、`iterate$spec0`、`run$spec0`、`grow$spec0`、`fold$spec0`、
`scan$spec0`、`spin$spec0`、`train_step$spec4`……（本页引的那句 `f1$spec0` 在其中）。本基数数到 64 条，
上一格是 67 条，差在基线不同。

**三、修在 `GraphDraft::removeNormPort`。** 它删端口列表的一项而把签名留在原地，兄弟函数
`removeWithPort` 却是两边都删。现在签名按端口被删的同一个位置删掉那个参数，且经工厂重建：参数名、
`var` 标记、`constas` 标记、尾部 rest、资源集都保住。`reinferDraftTypes` 修不了这事，因为它的前提
是两边数目相等，而被删掉一个端口恰好把那个前提打掉。修好后上面的图读作 `(xs: f32[n]) => f32 pure`，
端口仍是那一个 —— 两份清单一一对应，端口节点的标签也从 `n` 变回 `xs`。

**四、检查改成无条件。** `validateRuntimeGraphPortTypes` 现在问每一次提交编码的每一张图，任何构建，
和载荷检查、调用检查并列，不再是「提交给了它另一个签名」的那种图才问。上面 64 条红的归零由此达成：
`opt 1607/1606/1`（那 1 条见下）、`compile 2633/2617/0`、`linear 869/864/0`、`parse 126/126/0`、
`para 104/104`、`tools 70/70`、`feat.opt.e9 268/268`、`commit_refusal 10/10`、`constructs 18/18`。

**五、还剩一处既有断言记录着错位。** `test/plans/feat/opt/generic.plan.toml` 的
`specialize_dim_name_recursion.graph` 断言那个实例的签名仍是 `(n: int, xs: f32[n]) => f32 pure` ——
正是错位时才会出现的签名，修好后它是 `(xs: f32[n]) => f32 pure`（连同用例顶部那段说明里引的同一句）。
车道不许改既有断言，所以这一处没有动：它是本轮唯一一条红，改与不改留给协调侧。