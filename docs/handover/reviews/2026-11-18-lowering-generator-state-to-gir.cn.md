# 生成器状态下移到 GIR：几百行、18 个文件的快照会动

**日期**：2026-11-18
**提出者**：协调侧
**状态**：作者已裁定（`09b5130c0`：**two steps**，按第 6 节默认开工）；**第 9 节是落地的续篇：第一步「最小改动」被实测证明不存在**；**第 10 节（`f9d374161` 之后）把 9.2 的「一个 setter」量翻了 —— 开工前先读 10.1**
**触发**：作者裁定 `04ee677e7`「**lower generator state where types are known, with `T?` slots**」，`lane-genref` 去落地，两堵墙都是实测的

## 1. 裁定被完全采纳，而它指向的不是我原先以为的那一处

```
作者：**lower generator state where types are known, with `T?` slots**
⇒ `lane-genref` 实测后的一句话定位：
  **「槽的类型必须在『创建状态的那一处』就已经知道，而那一处在 arms 之前
    ⇒ 改点不在 `builder.cpp:6192`，而在『状态要在 arms 降下来、类型已知之后才成形』＝ **GIR 层**。」
⇒ **⇒ GCT 现在的顺序 `状态型 → 环 → arms` 本身就是那堵墙。**
```

## 2. 两堵墙（都构建通过后被诊断挡住，不是没试）

```
(a) 拿 `typeOfPlaceholder` 当 `T?` 的 payload
    ⇒ `TypeOfNotWholeType: typeof is written inside the larger type typeof(0)?`
      （`generators.cml:11` 的 `var i = 0`、`:40` 的 `let read = …`，`generators_body_fault.cml` 同）

(b) 换成一个洞：`TypeVarType::create("$gen_slot<n>", true)`
    ⇒ 槽型**成立**：`__gen_step__(__gen_state__: (int, $gen_slot0?))`
    ⇒ 但**创建处绑不上**：`GenericArgumentsMismatch: … leaves a type variable without a type`
      第一次调用传的是 `null`，而 **`null` 什么类型也不给**
```

## 3. 收益面（实测，不是估计）

```
43 个 `:var/new/empty` **全部在生成器体里，落在 18 个文件**
拆开：**30 个程序局部 + 13 个机器格子**
  （`__gen_next_*`×6、`__gen_match_subject_*`×3、`__gen_for_seq/idx`×2、`__gen_tuple_0`×1、`__gen_next_21/22`…）
⇒ 槽落地时 `var_new_empty` **43 → 13**（机器格子不动）
⇒ **⇒ 我上一页写的「收益面是一个用例 + 一个探针」是错的：是 18 个文件 30 处。**
```

## 4. 一个便宜选项，我**否掉**了

```
`lane-genref` 提出的：「把『类型码是 `var T` 且 payload 是被计数句柄』也算持有」（一处判定，估 ~20 行）
  ⇒ 探针就会绿
  ⇒ **但格子留着，`var_new_empty` 不降 43→13** —— 与「the cell should go」**相反**
⇒ **⇒ 我否掉它，理由就是作者那句话本身**：它解决的是**计数判断**，不是**格子该消失**。
  今晚已经有过一次同形的：我原先的默认是「让两处判断一致」，作者答的是「格子该消失」。
```

## 5. 要裁的是什么

**下移到 GIR 是几百行、且 18 个文件的 gir 快照会动。开不开？**

| 路 | 做法 | 代价 |
|---|---|---|
| **A（默认建议）** | **开，但拆成两步**：先只做「状态在 arms 降完之后才成形」这一处**最小**改动，让槽型能拿到真类型；`T?` 与拆槽算子留到第二步 | 快照会动，但**第一步就能量出「动的是哪 18 个、每个为什么该动」** |
| B | 一次做完（下移 + `T?` + 拆槽算子 + 重录全部快照） | 一步到位，但**出问题时不知道是哪一步带来的** |
| C | 不开，把这一半记成欠账 | 43 → 13 的降幅拿不到，探针的 `InvalidArgument` 留着 |
| D | 走第 4 节那个便宜选项 | **与作者的裁定相反**，我否掉了 |

## 6. 默认建议（按此当轮开工）

**走路 A 的第一步，且**先不重录任何快照**。**

```
① **只做「状态在 arms 降完之后才成形」**这一处最小改动
   ⇒ 目标不是让 43→13，是**让槽型能拿到 `Ref<int>` 或 `int`**
② **量出快照动面**：18 个文件里，第一步之后**实际动了几个**、每个为什么该动
   ⇒ 若动面是 18，说明这个改动一次到位；若只动 2–3 个，说明还有别的墙
③ **`T?` 与拆槽算子**（11-16 已授权）**等到 ② 的量出来再上** ——
   它一动快照，就是给半成品锁形状
④ **重录要走 `-UpdateAstHashes -AstOnly` 之外的路**（gir 快照不是 ast），
   **按文件逐个重录并 `numstat` 核对**，不许全树重录
```

**⇒ 理由**：**一条几百行的改动，若不在中间停下量一次，它出问题时**没有任何一个读数能告诉你是哪一半**。** **⇒ 而今晚所有有价值的修正，都是**在中间停了一次**才得到的（`armend`、`refstep`、`refshape`、`loophead`、`genref` 五次全是）。

**若作者认为该一次做完（路 B），请改第 6 节，并说明快照是否允许全量重录。**

## 7. 一处更正（`genref` 自己纠的）

```
「`list_sha` 不随基线变（它是语料指纹）；`refcell` 那页的 `b7f55687…` 是**另一份语料**，不是另一个基线」
⇒ ⇒ 我此前在报告里说过「`list_sha` 随基线不同」——**那是错的，记在这里。**
```

## 8. 相关页

- `docs/handover/questions/2026-11-18-the-state-slot-is-a-ruling-with-no-code.cn.md` —— 本页是它的续篇（那页说「门在类型拿不到」，本页说「那扇门在 GIR」）
- `docs/handover/questions/2026-11-18-a-ref-has-no-unwritten-form-for-the-state-slot.cn.md` —— 起点
- `docs/handover/reports/2026-11-18-the-cell-a-handle-needs-no.cn.md`（`refcell` 报告页）
## 决策者答复（2026-11-18）

**同意路 A，两步走。** 第一步只让状态在臂降完、类型已知之后成形，量快照的动面；第二步上 `T?` 槽与带检查的拆开算子，重录。快照按文件逐个重录、`numstat` 核对，同意。这是工程上的切法，不是缩小目标：终点仍是 30 个程序局部全部进状态元组、`var_new_empty` 43 → 13，剩下 13 个机器格子随后一并清掉（它们同样是生成器跨 `yield` 的状态，终点是 0）。否掉"把装句柄的格子也算持有"那个便宜选项，对。


## 9. 续篇：第一步（「最小改动」）**被实测证明不存在**

> 作者在 `09b5130c0` 答「**lower generator state in two steps**」，本页第 6 节的默认（路 A）被采纳，`lane-genref` 去落第一步。
> 量的结果是：**第一步不是一个「还没找到」的改动，它不是一个改动。**
> 四种走法都死在同一件事上：**状态型必须在 `__gen_step__` 的签名建成时就已是具体类型，而签名在 body 之前建，且没有延后点。**
> 基线：`4d48be5e4`（develop 已越过 `09b5130c0`），干净树，测量用的 patch **全部回退、零提交**。

### 9.1 四种走法，各死在哪一行

| # | 走法 | 死在哪 | 读数 |
|---|---|---|---|
| 1 | **`typeof` 包进 `T?`**：槽型取 `TupleType::nullable(typeOfPlaceholder(init))` | `src/compile/gir/builder_generics.cpp:2857`（`TypeOfNotWholeType`）：`resolveTypeOfs`（`:2767-2852`）只下探 `MatchTypeType`、具名 `Struct`、`OtherType`，**没有 `TupleType` 分支**，占位符活到整型检查 | 构建通过；`generators.cml:11`（`var i = 0`）、`:40`（`let read = …`）、`generators_body_fault.cml` 报 `typeof is written inside the larger type typeof(0)?` |
| 2 | **换成一个洞**：槽型取 `T?` of `TypeVarType::create("$gen_slot<n>", true)` | 生成器变成泛型，**创建处的调用绑不上**：`GenericArgumentsMismatch`「leaves a type variable without a type」——第一次的状态是 `null`，**`null` 什么类型也不给** | 构建通过；`generators.cml`（两处）、`generators_local_across_yield.cml`、探针 |
| 3 | **放宽 tuple 解析**（测量 patch：给 `resolveTypeOfs` 加 `TupleType` 分支） | 状态型**仍然不解析**：它在 **`FUNC` 的签名**里，而类型层只解析 ①被访问的 **TYPE 节点**（`:2910` `visitTypeNode`→`resolveTypeNode`）②泛型实例的**出口型**（`:2516-2522`）；**参数型从不解析**，签名又不可改（`include/camel/core/type/composite/func.h:24`「signature cannot be modified by construction」） | 构建通过；**前两堵墙当场消失**，但 `NoMatchingFunction: (typeof#0, int)，operator '='`（声明的写）与 `DynamicCastForbidden: typeof#0 → int?`（打包） |
| 4 | **倒序**：把 arms 先降下来、再建状态型 | **不是诊断，是 GIR 的取名顺序**（读代码，未量）：arm 里的 `DRef` 必须解析到 step 的 `vari`，而 `vari` 由 `Builder::unpackSlot`（`src/compile/gct/builder.cpp:6984`）在 `fill(...)` **之前**建，它的类型来自状态成员；把 arms 建在前面，它们要读的 `vari` 还不存在 | — |

⇒ **四种走法的交点只有一个**：**状态型是 step 这个函数的形状，而形状在 body 之前定；`null` 救不了它，占位符没人解析它，签名建了又不可改。**

### 9.2 接缝一：谁在 arms 之后建状态型

```
⇒ 只能是「建完再改写」，而且要在 **draft graph 还没封口**时改写 —— 那里已经有同类先例：
  `include/camel/compile/gir/draft_graph_builder.h:98`  `setExitType(Type*)`
  （泛型实例就是这样把出口型补上的，`src/compile/gir/builder_generics.cpp:2516-2522`）
⇒ 缺的不是机制，是**同一形状的第二半**：参数位的那一个 setter，加上**「arms 先建、vati 后建、类型最后补」**这个次序。
⇒ **不要在 GIR 之外再造一套「迟到的类型」**（见 9.4 第 3 条）。
```

> **已被 10.1 推翻（2026-11-18 夜，`lane-girstep` 量）：** 「缺的是**参数位那一个 setter**」不成立 ——
> 缺口在**三处**（端口 / `pack` / `call`），三处都要求「类型已知之后**重建 body**」。原文保留，作为被量推翻的
> 那句话，不要按它开工。

### 9.3 接缝二：谁补 `:accs` / `unwrap`

```
今天这三处都在 GCT 里，因为它们只需要「下标」：
  `src/compile/gct/builder.cpp:6984`  `unpackSlot`（`:accs(state,i)` + 拆 `T?` + `vari`）
  `src/compile/gct/builder.cpp:6281`  `stateZero`（第一个状态：每槽 `null`）
  `src/compile/gct/builder.cpp:6292`  `stateNow`（`yield` 处打包）
⇒ 状态型一迟到，`unpackSlot` 就走不到自己该走的形状：它得先知道 `:accs` 的类型。
⇒ 改写要做的事：为每个**程序局部**在 step 头部插 `:accs` + 拆槽算子，把 arms 里对它的读改写指到新 `vari`，
  在 `yield` 处打包；**机器自己的格子不动**（`__gen_next_*` / `__gen_match_subject_*` / `__gen_for_*` / `__gen_tuple_*`，13 个）。
```

### 9.4 三件「不要做」（今晚最贵的知识）

```
① **不要放宽 `typeof` 进 tuple / `T?`。**
   ⇒ 走法 3 已经量过：它只拆前两堵墙，**第三堵（签名没有延后点）照样在**；
   ⇒ 而且它会推翻一条有测试的拒绝：`typeof_inside_tuple_is_refused`
     （`test/plans/feat/compile/static_expansion.plan.toml`）。
   ⇒ **⇒「就算去请作者放宽也不够」——这是这一轮最值钱的读数，别再把它当备选方案。**
② **不要把这个环当成「类型知道得更晚」。**
   ⇒ 真问题是**类型没法延后携带**：签名在 body 之前建；`null` 绑不上洞，占位符没人解析，签名建了不可改。
③ **不要把它当成需要设计新机制。**
   ⇒ 在 GIR 里它只能靠「**建完再改写**」打破 —— 这是常规手法（泛型实例的出口型就是这么补的），不是新发明；
     给「迟到的类型」另造一套载体，就是把「没有」定义第二次。
```

### 9.5 两步的读数，每一步都要能单独量

```
第一步（裸类型 + 零值起点，`zeroValueOf` 那扇门，`src/compile/gct/builder.cpp:4103`）
  ⇒ 30 个程序局部里 **28 个**的类型有零值（**26 `int` + 2 `string`**）
  ⇒ 读数：`var_new_empty` **43 → 15**
  ⇒ 文件动面：**17 / 18**（唯一不动的是 `generators_match.cml`，它只有机器格子 `__gen_match_subject_0`）
第二步（`T?` + 拆槽算子，11-16 已授权）
  ⇒ 只剩 2 个非等不可：`generators_error_binding_across_yield.cml` 的 `e`（错误结构 `{kind,message,location}`）、
    `generators.cml` 的 `read`（闭包 `() => int uses capture 0`）
  ⇒ 读数：`var_new_empty` **15 → 13**
  ⇒ 文件动面：**+2**（上面那两个），其余 16 个不再动
探针 `docs/handover/probe-refcell/cell_holds_ref.cml`
  ⇒ 第二步之后 `state::set(box, 5)` 才该绿：`box : Ref<int>` 属于「非等不可」那一类
  ⇒ 第一步之后探针**应当仍然** `5 / 5 / 5 / 9` + `InvalidArgument`@`:46`（这正是「每一步都能单独量」的证明）
每一步都要量的两行
  ⇒ `-Check capture-census` 的 `var_new / var_new_empty / list_sha`，以及 18 个文件的 gir 逐个 diff
  ⇒ 基线（`4d48be5e4`）：`var_new=213 var_new_empty=43 list_sha=72c1a623…`；
     六计划 `total 5321 / pass 5299 / fail 0 / skip 2 / known 20`（compile 2627/2610、linear 861/856、opt 1538/1538、para 104、parse 121、tools 70）
```

### 9.6 要不要再问作者

**不要。** 作者那句「two steps」在「建完再改写」这条路上**是可满足的**：第一步裸类型 + 零值起点，第二步 `T?` + 拆槽算子，裁定一个字都不用改。唯一曾需要作者点头的是「放宽 `typeof`」，而 9.1 走法 3 已量出**它不够** —— 所以没有可问的问题，只有要落的代码。

### 9.7 一句话（给主干说明用）

**作者说两步，而第一步被实测证明不存在 —— 不存在的原因不是没找到，是签名在 body 之前建、类型没有延后点，只能建完再改写。**

## 决策者答复续（2026-11-18 07:00）

第 9 节的诊断收下：签名在体之前建、类型没有延后点，所以只能"建完再改写"，缺的是参数位的那个 setter（与泛型实例补出口类型的 `setExitType` 同一形状）加上"臂先建、状态型最后补"的次序。9.4 的三件"不要做"同意，尤其不放宽 `typeof`。

**但 9.5 的两步不是我说的两步，9.6"裁定一个字不用改"不成立。** 9.5 的第一步让 28 个局部以类型的零值作起点、只有 2 个用 `T?`，做完之后就停在两套"未写"的表示上：有零值的类型用零值，没有的用 `null`。这是同一类事两条规则，起点按"这个类型有没有零值"分叉，就是一个按类型性质的特例；11-16 的裁定是"槽写成 `T?`、创建时为 `null`"，对所有槽一样。

所以两步改为：
1. **接缝**：参数位的 setter 与"臂先建、状态型最后补"的次序（9.2）。这是两条路都绕不开的那一步，单独做、单独量：做完之后状态型能拿到真类型，快照按文件逐个看动面。
2. **全部槽一律 `T?`、起点一律 `null`**，读槽用 11-16 授权的带检查拆开算子；`zeroValueOf` 不再被状态槽调用，`gct/builder.cpp:4103` 那扇门删除。读数：`var_new_empty` 43 → 13，探针 `cell_holds_ref.cml` 转绿，计数器用例 `1 2 3`。
拆开的代价（每次读多一次带检查的拆开）由优化遍去掉能证明非空的那些，不靠"有零值就不用 `T?`"来省。

## 10. 续篇（2026-11-18 夜，`lane-girstep`）：9.2 的「一个 setter」被量推翻

> 裁定：作者 `f9d374161`（**`rework: every state slot is T? starting null, no zero-value split`**；同一笔采纳
> 「**the first step of lowering generator state needs a build-then-rewrite seam**」）。
> 本节是**测量，不是意见**：基线 `86606c969`（= `origin/develop`，`git fetch` exit 0，未前进），干净树，
> **零源码改动、零提交、零重录**。它只回答一句：**作者那句话，在这个读数下还成不成立。**
> ⇒ 结论：**前提不成立**，所以这一节不落代码，先把「不成立在哪」钉住。

### 10.1 三处，不是一处

```
① 端口：enterScope（src/compile/gir/builder.cpp:323-376）→ createCompileGraph
   （src/compile/gir/builder_helpers.h:417-444）逐个 addPortNode(type)。
   ⇒ 事后 setFuncType **不改端口**，而 body 里每个 :accs 的类型都从端口来；
   ⇒ draft 没有改节点/端口型的 API：retype.h 那套是**冻图**重建，retypeDraft 明写
     「Node kinds, node ids, the five input lists … are left exactly as they are」（include/camel/runtime/retype.h:21）。
② pack：yield 处 stateNow（src/compile/gct/builder.cpp:6289）要把局部装进 `T?`，走
   coerceTo → castNodeTo（src/compile/gir/builder_literal.cpp:507-523）
   ⇒ **建图时就要具体类型** —— 9.1 走法 1 的 `DynamicCastForbidden: typeof#0 → int?` 就是这一行。
   ⇒ 9.3 的清单（accs / unpack / pack）**没有把它算成一处墙**，而它比签名更硬。
③ call：每个 yield 的 stream 值都 call `__gen_step__(state)`，visitLinkNode
   （src/compile/gir/builder_operators.cpp:2284）在建图时就按被调签名解析参数
   ⇒ **第一处 call 就死**（9.1 走法 2 的 `GenericArgumentsMismatch` 就死在「创建处」）。
⇒ ⇒ **「建完再改写签名」不够**：那些 call 已经按旧签名检查过、参数也已按旧型 `coerce` 过。
   ⇒ 能成的只有两条：**类型已知之后把 step 的 body 重建一遍**，或者**把生成器整体下移到 GIR**。
⇒ ⇒ 9.2 的「缺的是参数位那一个 setter」**作废**（不是被改写，是被量推翻）。
⇒ 次序也不成立：9.2 的「arms 先建、vati 后建」正是 `bc5ce2249` 量过会失败的形状（提交说明：
   「a local declared after the arms would leave them reading the enclosing body's cells」）；
   而 draft 没有重指边的 API（唯一的 rewiring 在 opt 层 `src/passes/opt/generic/writings_extract.cpp`）。
```

### 10.2 (B) 的三条：每条都要一个「怎么量它成立」的读数

```
① 诊断去重
   探测遍会把同一批诊断再发一遍。权威是 userSink()（src/compile/gir/builder.h:2885）；探测遍进 scratch
   sink 并丢弃。
   量法：a) census 的 refused 数（`86606c969` 上 = 1163）与 refused_first 列表逐字不变；
        b) 生成器体里带错的用例仍**只**打一行、位置不变 ——
           generators_read_before_declaration ⇒ UnresolvedReference @9:11；
           generators_cell_retype ⇒ NoMatchingFunction @14:5（test/plans/feat/compile/stream.plan.toml 两条都钉住行/列）。
        ⇒ 任何一条变成两行、或位置移一格，这一条就不成立。
② idIndex_（src/compile/gct/builder.cpp:276/6228 一带的 `idIndex_++`）
   探测遍建的节点要不要占 id：不占 ⇒ 重建时 id 全变（`__g_<n>__` 名字变，17 个文件的快照为一个与语义
   无关的原因而动）；占 ⇒ 有洞。
   量法：**一个不该被这条改动碰到的文件，dump 必须逐字节相同**，不是「节点数相同」：
        generators_match.cml（它唯一的 :var/new/empty 是机器格子 `__gen_match_subject_0`）与
        generators_early_stop.cml（一个 :var/new/empty 都没有）的 `std::jgir` 与改动前同 sha；
        并且 generators.cml 里 `__g_<n>__` / `__gen_*` 的名字与改动前同名。
③ module 注册（`context_->importModule` / `registerGenericTemplates` / `lambdaGraphs_` / `defaults_`）
   探测遍若碰到真注册，要么幂等、要么可回滚。
   量法：a) ① 里那两条 dump；b) feat/modules 与 feat/parse 的读数不动；c) `-Check camel-for-ai`
        （读模块声明）不动；d) census 的 refused 列表不变（注册若改了名字解析，被拒的用例会先动）。
```

### 10.3 两个「人口对不上」，分开写（混在一起就会变成矛盾）

```
① 文件数：**18 是 census 的口**（30 个程序局部落在 18 个文件）；**17 是我 dump 的口**（我 dump 了 24 个
   生成器/探针文件，其中 17 个带 :var/new/empty，共 42 个节点；最多 generators_shapes_consumed 12 个，
   generators.cml 2 个）。两个数不矛盾：census 的 43 含 13 个机器格子，我只数了我挑的 24 个文件，
   差的那一个在语料别处 ⇒ 引用时写「18（census）/ 17（dump）」。
② list_sha：§9.5 的 `72c1a623…` 是 `4d48be5e4` 那棵树；今晚 `86606c969` 上量到 `1a01d883…`，
   而 `var_new=213`、`var_new_empty=43` 一字未变，files 3891 → 3892
   ⇒ **语料动了、计数没动**。差如果两个人口同时变了，它就还不是一个差；只变了一个，就要先报。
```

### 10.4 11-16 授权了，但表里还没有位置

```
include/camel/core/operator_table.h:132  `kWellKnownCount = 22`（id 1..21；id 22 明写留空，见 `:127-131`
「reusing it would make the deletion a rename」）⇒ 新算子只能占 **23**。
`:op/tag_null_value` 是**不检查**的那个（src/builtin/operators/tagged.cpp:39-41，null 返回零槽）。
而「起点 null」在**每个 pull 的头部**都要拆一次（不是只在某个分支的 arm 里），第一次 pull 的槽必然为 null
⇒ 它不是替代品；「检查 / 不检查」是裁定，不是实现细节。
```

### 10.5 会先翻的不是快照，是一条断言

```
test/plans/feat/compile/stream.plan.toml:1205-1211（generator_cell_type_from_first_write）的 verify_script：
  graphs['labels'] 里恰有 1 个名为 's' 的节点，且 outputType 是 'var string'
⇒ 今天那个节点是**外层格子**；裁定之后格子退休，名为 's' 的只剩 step 的局部，而它在 `__gen_step__` 图里
⇒ 这条会从 1 变成 0 —— **它钉住的正是裁定要拆掉的东西。**
⇒ ⇒ 所以「先重录快照」不够：这条要先改断言（改断言要作者点头），它是 17 个文件里第一个必须变的。
```

### 10.6 一句话（这一节要交给作者的）

**「建完再改写」要重写的是 body 的建，不是签名的字：端口建时定死、pack 要具体类型、call 在建图时就检查
—— 三处都指向「类型已知之后把 body 重建一遍」，而这句话不是 9.2 那句「一个 setter」能兑现的。**
⇒ 9.6 的「不要问」也随之作废：现在要问的不是「两步怎么落」，是「重建 body」这条路作者认不认。
