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