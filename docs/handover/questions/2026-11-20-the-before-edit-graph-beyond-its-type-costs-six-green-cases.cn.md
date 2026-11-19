# 编辑前的图越界报成内部错误之后，六条一直是绿的用例红了

**日期**：2026-11-20
**提出者**：车道 `lane-edit-beyond-type`（基线 `c75611862`）
**状态**：待裁定（本页写死一条默认）
**触发**：作者 2026-11-20 04:50 下发（`0ea4dc518`）第 2 条 P\* 必需项：「编辑前的图若已超出它的类型，提交就不问编辑后的效应（`commitViolation` 那一支）……那是编译器缺陷，**改成内部错误报出**，不再跳过。」

## 1. 问句

这一支落地后，全量里有 **六条一直是绿的用例**变红。它们是**该按编译器缺陷去修生产者**（默认甲），还是说明这条读数**问早了**、要收窄到只问前端建的那张图（乙）？

## 2. 读数（同一棵树，Release，主机，`-Jobs 8`；探针见 §3）

```
改前（只有探针，无行为改动）：total 7179, pass 7143, fail 2, skip 9, known 25
改后                        ：total 7179, pass 7137, fail 8, skip 9, known 25
⇒ 除这 6 条外，每一个计数相同；两条既有红（consistency 的 runner.backend_matrix、
  new_values.sort_rebinding.graph）在两次运行里都在。
```

六条（三族只有两族）：

```
① feat.compile.effects_syntax :: pass_boundary.orphan_effect_refused      —— 1 条
② feat.modules.tiles :: compiled_region_parallel / host_resident_parallel
                       / native_parallel / native_parallel.scheduler
                       / native_parallel.linear                          —— 5 条
```

诊断原文（改后，逐字，节选到事实为止）：

```
():?:?: [Error]: Pass 'test::bounds_flip' failed with an internal error: the graph 'main' a commit
was asked to edit already reached past the type its own signature declares, before the edit:
node 3 of graph 'main' (:io/println), in a graph whose type declares uses var stdout, makes an
access the type does not cover (write stdout): the front end accepted a graph that is beyond its
own type, which is a defect of the compiler and not of the program: no commit can judge an edit
against it. (name=RuntimeDiag::PassInternalError, code=0x0400000C)

():?:?: [Error]: Pass 'tiles::test_outline_compiled' failed with an internal error: the graph
'__g_49__' a commit was asked to edit already reached past the type its own signature declares,
before the edit: node 2 of graph '__g_49__' (a call of 'chain'), in a graph whose type declares
nothing, has an effect outside the program: … (name=RuntimeDiag::PassInternalError, code=0x0400000C)
```

## 3. 这一支原先「跳过」的是什么（改前的逐字事实）

`commitViolation`（`src/runtime/resources.cpp`）在**这次编辑动了已有节点**（`changed` 非空、`before` 非空）时：
先量「编辑前的图是否已越界」，**已越界就不问编辑后的效应**（跳过的是**效应那一问**，不是整条路径：顺序那一问照问），
然后照旧收 `alreadyUnordered`、跑 `orderIncomplete`。跳过时**既没有返回成功也没有返回空**——
它把这次提交当作"效应方面无话可说"继续往下走。

探针（临时写 `/tmp` 的文件，未提交）在**改前**的全量里命中 **6 次**：`main` 1 次、`__g_49__` 5 次，
即 §2 那六条。⇒ 这一支今天**不**是死枝，它是那六条用例一直绿的**原因**。

## 4. 两族各是什么

```
① pass_boundary.orphan_effect_refused
   用例**本来就在测一个非法图**：它用只存在于测试里的 `test::bounds_flip`
   （include/camel/passes/test/bounds.h）把一次 `stdout` 的写法 write→read 翻掉，
   好让 pass 边界那条 `PassBoundaryResources` 有东西可拒。我的这一支在**边界之前**
   （提交里）就拒了，于是它期望的那句 "neither a read of its value nor a place in its chain"
   不再出现。⇒ 改期望能消红，但那条检查就此失去唯一的覆盖。

② tiles 五条
   用例**不是非法程序**：`native_parallel.cml`，`expect_exit = 0` + 精确 stdout。
   越界的是 `tiles::test_outline_*` 造出来的 `__g_49__`：它的型**什么都不声明**，
   体里一次 `chain` 的调用带来**程序外的效应**（`tiles:driver`）。这正是
   `2026-11-18-the-tiles-pass-publishes-a-body-before-its-type` 记的那族「先发体、后补型」。
   ⇒ 旁证：GPU 层（只观测、不判绿红）**早就把同族记成红**，理由码是 `0x040001B0`
   （`PassEffectBeyondType`）；本次改后它们变成 `0x0400000C`（`PassInternalError`），
   gate 于是报「RED FOR ANOTHER REASON」（5 条）与「NOT RECORDED」（4 条）。
```

**一句**：①是**故意**造的非法图（测试装置的问题），②是**真缺陷但生产者不是前端**（tiles 那一遍的发布顺序）。

## 5. 两条路与代价

- **甲（默认）：规则照下发保留，修生产者。** ②由 tiles 车道按体加宽（同一问题上已有页）；①把测试装置换成不经过 commit 的形式，或把用例的期望改成新诊断并另加一条覆盖 pass 边界的用例。
  代价：两条车道各有一笔活；在它们落地之前，`feat/compile` 与 `feat/modules` 各带红（1 + 5），七计划读数不是基线。
- **乙：收窄到只问「前端建的那张图」。** 代价：需要一张图的「谁建的」标记（今天没有这样的读数），且 `__g_49__` 那种遍造图越界就继续不报——正是下发要堵的那类；`bounds_flip` 那条仍会红（它改的是 `main`）。
- **丙：退回跳过。** 作者已否（下发明写「不再跳过」），不列为候选。

## 6. 默认（写死：甲）

按下发保留这一支：它把一直静默跳过的越界显形，六条红是**读数本身**，不是抖动。
在生产者的修复到位之前，`feat/compile :: pass_boundary.orphan_effect_refused` 与
`feat/modules/tiles` 五条按红记账；本页是它们的依据。

拆法（2026-11-20 协调人裁定，按两类分别处理）：①那条**拆成两条**——一条仍测 pass 边界
（换一个不越界的装置），一条测「越界图被拒」；②tiles 那一族**先修生产者再合**，位置与建议修法见 §7。

## 7. `native_parallel.cml` 那五条：体在哪一步发、型在哪一步写（纯读代码，基线 `462587e7a`）

### 7.1 是哪些图、越的是哪一处

`__g_49__` 是**前端**为 `test/cases/modules/tiles/native_parallel.cml:8` 的 lambda
（`(i: int): float => float(sum(chain(x)))`）合成的图，不是遍造的（名字形状见
`src/compile/gir/builder_helpers.h:400`）。诊断点的 node 2 是「a call of 'chain'」，而
`chain` 正是 `:2-5` 那个纯张量函数 —— 也就是 tiles 遍要搬去设备的那一个。
⇒ **`__g_49__` 自己的算子一个都没改，越界是「被调方的体变大、调用方的型没跟上」。**

### 7.2 时间线，逐处行号

```
体被改、被发   modules/tiles/src/region_outline.cpp:972  declareBodiesOverDrafts(session)
               modules/tiles/src/region_outline.cpp:986  return session.commit();   ← 一次 commit
型被写         modules/tiles/src/region_outline.cpp:350-352  session.edit(graph).edit()
                     .setFuncType(type->withResources(clause));
               量的是 session.prepared()（:309）＝该会话要发布的、已编码的副本
               （src/runtime/draft_session.cpp:1903-1914，裁定 f61196246 的落点）
晚一批的那处   modules/tiles/src/region_outline.cpp:989  settleDeclarations
               由 region_pass.cpp:82 / region_test_pass.cpp:404 在**下一次** commit 之后调，
               它的定点是 for (;;)（:1003）
```

⇒ 两次加宽都对，但**中间隔着至少一次 commit**：`__g_49__` 是在「`chain` 已经加宽、它自己还没
加宽」的那一次 commit 里被发布出去的 —— 这就是 `2026-11-18-the-tiles-pass-publishes-a-body-before-its-type`
记的「先发体、后补型」，只是那一页的修法（`f61196246`，让遍在 commit 前读 `prepared()`）只覆盖到
它当时量到的那些图。

### 7.3 `declareBodiesOverDrafts` 为什么漏了它：四个出口，各带行号

`region_outline.cpp:325-353` 的循环对每张图做四件事，任一为真就**跳过这张图**：

```
① :330-333  staged->graph(id) == nullptr  这张图不在本次编码的闭包里
            （collectCommitClosure，draft_session.cpp:1361：从 runtimeRoot_ 沿 refs() 走，
             made 由 :1403-1405 补种；refs() 的五个来源见 graph.cpp:2751-2789，
             静态函数值算 kGraphRefStatic，算子操作数里的图不算）
② :339-341  型是 Generic（"promises nothing"）
③ :343-345  covers(declared, body) 已经成立 ⇒ 无事可做
④ :346-349  widened 出来的 clause 仍不盖住 body ⇒ 留下，交给 commit 拒
```

两处**范围/深度**限制：`:308` 的定点只跑 **4 轮**；`settleDeclarations` 无上限但晚一批。

`covers` 只有两道关：名字+模式（`src/core/resource_semantics.cpp:443-451`）与 **`external` 标志**
（`:469-471`：`source.external && !target.external` 直接 false）。`tiles:driver` 是
`externalAmbient`（`modules/tiles/src/module.cpp:940-943`）⇒ 体的 footprint `external=true`，
而「什么都不声明」的型 `external=false` ⇒ **`external` 是硬的一关**，名字对不上是次要的。
`widened` 会把 `external` 带过去（`resources.cpp:1424` + `sem::ambientCarriesExternal`），
所以「能加宽」是有解的那一档。

### 7.4 该修哪一处（建议，最小）

把「被调方变大 ⇒ 调用方**当场**跟上」做在**发布它的那一次 commit 之前**：

```
① region_outline.cpp:306 declareBodiesOverDrafts 的定点不许因「图不在本次编码闭包里」而跳过
   （出口①）——作者在 :313-317 已经为「本遍造的图」补种过一次同样的洞，前端造的图是同一条理由；
② :308 的 4 轮换成「到不动为止」，与 settleDeclarations:1003 的 for(;;) 一致。
```

不改检查、不给已发布的图开设型入口（那正是 `2026-11-18` 页 §四 否掉的那条）。

### 7.5 `native_parallel.cml` 会不会因此变合法 —— **会（5 条转绿）**，附条件

**不是「仍红但理由换成真越界」**，条件是 7.3 的出口①（或出口③+①）就是它红的原因。理由：
越界效应是**遍自己插的设备提交**（upload / exec / download 写 ambient `tiles:driver`），
`widened` + `ambientCarriesExternal` 写得出这条 clause；而这条链上的型全是机器推的
（`sync` 体、lambda 的 `nothing`），没有作者手写的、比体更窄的 clause，所以
`f61196246` 的 ① 「这一遍没有改承诺」成立，加宽是有解的。

唯一会让它变成「仍红而理由是真越界」的情形：出口②（型是 `Generic`）或出口④
（`widened` 不盖住体）为真 —— 那说明这张图的型**本来就写不出**它要的承诺，是第二轮问题
（要给 lambda 一个能写 clause 的型），不是加宽能修的。

**这一步要一次读数钉死**（占主机，按规矩报等位）：在 `region_outline.cpp:330/339/343/346`
四个 `continue` 前各打一行临时探针（图名 + 出口号），只跑
`test/plans/feat/modules/tiles.plan.toml` 的五条，五行之内就知道是哪一个出口。