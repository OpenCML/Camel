# 编辑前的图越界报成内部错误之后，六条一直是绿的用例红了

**日期**：2026-11-20
**提出者**：车道 `lane-edit-beyond-type`（基线 `c75611862`）
**状态**：待作者过目（本线已落地 §7.3 的修正：同一条规则，读数按自己的程序版本量 —— 六条红全消）
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

**⇒ 这两行是 §7.3 修正之前的状态。修正之后（§7.4）是 `7179/7143/2/9/known 25`，六条全绿。**

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

**一句（已按 §7 更正）**：两条其实都**不是**越界 —— 都是**我的读数**把两个程序版本混在一起量出来的假红。
①的图在它自己的程序里合法；②的 `tiles` 那一遍**把型加宽了**（见 §7.1 的探针）。§4 与 §5 的初版判断留在这里
只作对照，结论以 §7 为准。

## 5. 两条路与代价（初版，已被 §7 作废）

- **甲：规则照下发保留，修生产者。** 初版说 ② 由 tiles 车道按体加宽 —— **不成立**：它已经加宽了。
- **乙：收窄到只问「前端建的那张图」。** 代价：需要一张图的「谁建的」标记（今天没有这样的读数），且遍造的图越界就继续不报 —— 正是下发要堵的那类。
- **丙：退回跳过。** 作者已否（下发明写「不再跳过」），不列为候选。

## 6. 结论

按下发保留这一支（不跳过、报内部错误），**并且把读数放在它自己的程序版本里量**（§7.4）：
这样今天**零新增红**，六条全绿，规则仍然有牙。

初版曾按「两类分别处理」排出「①拆成两条用例、②先修 tiles 再合」这两步 —— **两步都不需要了**：
①继续测 pass 边界并且绿；②tiles 那一遍是清白的。修正一处之后全量
`7179/7143/2/9/known 25` 与**无行为改动的探针运行逐项相同**，2 条红就是两条既有 consistency 红。

## 7. 探针的结果，以及初版猜错的地方（`462587e7a` 之后）

### 7.1 四行探针（跑完删掉，仓库里不留改动）

在 `region_outline.cpp` 的加宽循环四个 `continue` 前各写一行到 `/tmp/camel-tiles-widen-probe.log`
（不写 stdout，免得动任何用例的期望），只跑 `test/plans/feat/modules/tiles.plan.toml`：

```
1423 行，出口计数：
  620  exit2-generic            619  exit3-covered            184  widened
    0  exit1-not-in-closure       0  exit1b-no-type            0  exit4-widened-not-cover
```

`__g_49__` 自己的行（逐字，不同用例各一份）：

```
round=0 exit=widened graph=__g_49__ declared=[pure] body=[uses any unknown]
round=0 exit=widened graph=__g_49__ declared=[pure] body=[uses var tiles::driver, any unknown, any foreign]
round=1 exit=exit3-covered graph=__g_49__ declared=[uses var tiles::driver, any unknown, any foreign] …
```

⇒ **遍把型加宽了，而且加宽到覆盖住。** 初版 §7.3 的四个出口假设里 **出口①一次都没发生**，
所以初版 §7.4 的「出口①不许跳过 + 4 轮换成到不动为止」**不是这条红的修法**（这里留档，免得下一个人再走一遍）。

### 7.2 真正的原因：一次读数混了两个程序版本（是读数错，不是图错）

`commitViolation` 拿 `program`（**这次要发布的那个**）去量 `before`（**上一版的图**）。而一次调用的足迹是按
**被调方声明的 clause** 读的 —— 本次 commit 恰好正在改写这些声明。`tiles` 在同一次 commit 里既加宽 `chain`
（`chain` 的体里多了设备提交），又把调用它的 `__g_49__` 加宽：用**新版**的 `chain` 去量**旧版**的 `__g_49__`，
它的体就凭空多出 `tiles::driver`，于是旧的 `pure` 型「不盖住」⇒ 报内部错误。**那张图在自己程序里一直是合法的。**

这正是 `2026-11-18-the-tiles-pass-publishes-a-body-before-its-type` §二.2 立下的那条约束
（「任何『拿草稿 + 现在的程序去量』的读数，答的是没人有的那个程序」）的**反面**：拿**旧图 + 新程序**去量，
得到的是比真实更大的足迹。约束是对称的：**一次读数，一个程序版本。**

### 7.3 修法（已落地）

```
include/camel/runtime/resources.h:434   commitViolation(..., const Program *beforeProgram = nullptr)
src/runtime/resources.cpp:1921-1940     量 before 用它自己的程序版本（自己的 walk / resolver / table）
src/runtime/draft_session.cpp:2013      调用处传 program_（`graph` 正是它名下的那一版）
```

`beforeProgram` 为 `nullptr` 时读 `program` —— 两个程序是同一个对象的那种调用者，这就是完整答案。
**规则本身一个字没改**：还是「编辑前的图越界 ⇒ 内部错误」，只是终于在自己的程序里量它。

### 7.4 读数（全量，Release，主机，`-Jobs 8`）

```
探针（无行为改动）  total 7179, pass 7143, fail 2, skip 9, known 25
修正后              total 7179, pass 7143, fail 2, skip 9, known 25   ← 逐项相同
六条新红：全部转绿（pass_boundary.orphan_effect_refused + tiles 五条）
两条红：consistency 的 runner.backend_matrix、new_values.sort_rebinding.graph（既有，与本次无关）
```

### 7.5 `native_parallel.cml` 会不会因为修 tiles 变合法 —— 这个问题已经不需要答

它**本来就合法**；`tiles` 那一遍**已经**在建图时把 `uses var tiles::driver` 写进了 `__g_49__` 的型。
初版 §7.5 里「会变合法（5 条转绿）」这个答案方向对，但归因错了：不是「修了才合法」，是**一直合法**。

## 默认建议（写死）

**默认建议**：按 §6/§7 的结论（即初版那个**甲**）办 —— **保留下发的那一支**：编辑前的图若已越界就报
内部错误、不再跳过；同时坚持 §7.3 那处修正（`commitViolation` 收 `beforeProgram`，量 `before` 用它自己的
程序版本），**乙（收窄到只问前端建的那张图）不采用**（它要一张图的「谁建的」标记，而遍造的图越界就继续不报，
正是下发要堵的那类）。
**判据**：全量 Release 主机 `-Jobs 8` 读数是 `total 7179 / pass 7143 / fail 2 / skip 9 / known 25`，
六条（`pass_boundary.orphan_effect_refused` + `tiles` 五条）全绿，两条既有 consistency 红
（`runner.backend_matrix`、`new_values.sort_rebinding.graph`）不动；`beforeProgram == nullptr` 时回落读 `program`
是两个程序是同一对象的那种调用者的完整答案，不许另立第二套判据。
