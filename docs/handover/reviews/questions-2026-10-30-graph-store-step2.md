# 图仓第 2 步：材料与代码对不上的两处，按原话记

本页只做一件事：把第 2 步开工前"材料说 A、代码是 B"的两处原样写下来，交作者裁。**设计文档的论断一字未动**（`docs/design/graph-store/README.cn.md` 只换了一处符号引用，原因见 §3 末条）。

施工分支 `lane-gs2`。读法：**代码一律按本分支的基点 `abbc9c229` 现读**，每条都带文件与行；"本分支做了什么"只说这一笔改的，不说方向。

## 1. 冲突一：文档说"第二份草稿"不存在了，代码里它还在，而且每次编码都建一份

**文档原文**（`docs/design/graph-store/README.cn.md:262`，§9.1 表格"编辑用的图"一行，列名是"现在（`src/` 实测）"）：

> 前端与改写共用一份 `runtime::GraphDraft`（`include/camel/runtime/draft.h`），编码直接读它，"第二份草稿"不存在了

**代码**（`src/compile/gir/graph_encode.cpp`，`abbc9c229`）：

- `:437` `prepared.draft = std::make_unique<GraphDraft>();` —— 每编码一张图就新建一份草稿。
- `:493-666` 把源草稿的活节点逐个复制过去，并另起一套稠密编号（`sourceToPrepared`、`nextPreparedId`）：节点头、五段边、payload（FUNC/BRCH/JOIN 里的节点号要重映射）、端口、闭包、entry/exit/output/return、区域。
- `:1138` `emitDraftNativePayload(*prepared.draft, arena);` —— 冻结图的字节从这份副本出，不是从源草稿出。

**差在哪**：文档那一行说的是方向（也确实是要做的），但写进了"实测现状"这一栏；代码里源草稿与冻结图之间隔着一份每次都新建、复制的草稿，且它的节点编号与源草稿不同。同页 `:266` 那句"一次改动前端、内联、自动微分与全部后端，单独一步做不成独立的一步"与代码相符——冲突只在 `:262`。

**本分支做了什么**（见 §3 的证据）：删掉这份副本。`prepareGraph` 只记运行时编号 `runtimeId` 与源草稿里活的节点集 `liveNodes`；`planDraftPayload(draft, live)` 直接按源草稿的活节点出计划，`emitDraftNativePayload` 从源草稿写字节。`abbc9c229` 那一行的代码不再存在，文档 `:262` 从"说的"变成"是的"。

## 2. 冲突二：任务书里的"四张边表"在代码里没有对应物，它和文档里的"4 张旁路表"是两样东西

**任务书原话**（口述，不在文档里）：

> 四张边表（含 ctrl/sync/norm/with 各表）

**代码**：

- 边不按"表"存，也没有 norm/with/ctrl/sync 四张表这种东西。边**种类**是 4 个：`DraftEdgeKind` = `Data`、`Sync`、`Order`、`Ctrl`（`include/camel/runtime/draft.h:59-64`）；存储**槽**是 5 段：`DraftEdgeSlot` = `NormalPorts`、`ContextPorts`、`Ctrl`、`Order`、`Sync`（`:71-77`），写在每个节点自己的尾巴里（`[payload][normal][context][ctrl][order][sync]`，计数在 `:112-129`，取用是 `:181-187` 的 span，`normInputsOf`/`withInputsOf` 是同一段数据的两个角色视图，`:455-475`）。键是节点编号 `gc_node_ref_t`。
- 事务内的用户表 `DraftUserLists`（`:258-299`）同样是节点编号键，随事务一起消失。
- 全仓 `grep` 不到以地址（`GCNODE*`/`DraftNode*`）为键的边表。

**"4 张"在哪**：在文档 §9.1 `:263` 与 §9.4.1 `:303`，指的是 `include/camel/compile/gir/draft_graph_builder.h:207-210` 的 `nodePortNames_`、`funcTargets_`、`drefTargets_`、`operTargets_`——**冷数据**（端口名、函数目标、dref 目标、算子目标）的旁路表，键也已经是节点编号，里面没有一条边。

**差在哪**：任务书把"4 张旁路表"叫成了"四张边表"，又把边种类（4 个）当成表的个数。按它去找"四张边表"找不到；按它去删"四张表"，删到的是冷数据旁路表（那是第 2 步之后要做的事），边一点没动。**这一处不影响本笔**：本笔没碰边存储，也没碰那 4 张表。

## 3. 本笔的证据

只做能逐字节自证的那一半：删第二份草稿。

- **图逐字节不变**：把仓库里 3,359 个 `.cml`（除 `tmp/`、`test/results/` 两个跑测试时写出的产物目录，全部 `.cml`）逐文件跑 `std::gir`，编码前后各存一份 `"<sha256> <路径> <退出码>"` 清单，两份清单只差"少了一个测试临时目录的副本"，**3,359 行的哈希逐个相同**（2,201 个编译通过、1,158 个负例诊断，逐字节一致；清单 sha `b8686cd4db23b6f7777e978850f59d4b22078ee31e26afead481f5be91300f6b`）。36 张 `.dot` 的逐字节对比都在全层里（`test/` 下 28 张金标，加 `docs/tutorial/images/` 的 8 张——后者由 `feat/docs` 与 `test/plans/feat/docs/fixture/images/` 逐张比；`exemplars/` 下另 4 张没有用例比它们），`snapshot differs` 0。任务书说的"至少 39 张"与树里也对不上：实际是 28 张金标 + 8 张教程图。
- **全层**：`-Plan all -Tier fast -Jobs 8`，`[SUMMARY] total: 7802, pass: 7798, fail: 0, skip: 2, known failures: 2`，与删草稿前的基线逐项相同（基线也是 `pass: 7798, fail: 0`），新红 0。
- **为什么能逐字节相同**：旧副本的编号就是从源草稿的活节点按编号升序稠密排出来的（`:503` 的循环），`planDraftPayload` 的顺序与它一致；旧副本逐字段复制的东西，新路径直接从源草稿读同一个字段（`funcType`、`closureType`、`flags`、`captureNames`、`implKernelUri`、`declarationFacts`、静态槽、节点头、payload、五段边、区域）。唯一被显式规定的是"子集的边规则"：顺序边的前驱不在活集里就跟着丢（旧代码 `remapLiveInputs(..., true)` 同一行为），控制区域没有活主人就是死区域（旧代码同一行为）。
- **两处与旧路径必然不同的地方**（都不是 std::gir 能看见的，如实记）：
  1. 旧副本从不复制区域的 `callSite` 与 `inlineOf`（`abbc9c229` 的 `:720-745` 只写 owner/parent/kind/result/origin/attrs/params/ports），所以冻结图里内联区域的这两个字段一直是默认值；新路径从源草稿整条区域读出来，这两个字段有值了。`std::gir` 的 DOT 不打印它们，金标与清单都不变。
  2. FUNC 节点的被调用者编号：旧路径写在副本上（`:552`），新路径写在源草稿上（`patchPreparedFuncPayloads`）。这个字段在编码之后没有读者（读它的只有 `graph_encode.cpp` 自己），但严格说，编码现在会改源草稿的这一处。
- **改了设计文档一处符号引用**（论断一字未改）：`docs/design/graph-store/README.cn.md:270` 原引 `src/compile/gir/graph_encode.cpp#setId`，而删掉副本后这个文件里不再有 `setId`（取号仍在 `prepareGraph` 里），`docs.links.citations` 因此红一条。改成 `#prepareGraph`，只换符号引用，句子的意思不变。若作者认为这一处也不该动，请裁。

## 4. 交给作者的问题

1. **第 2 步是否可拆成 a) 删第二草稿（已做、图逐字节不变）与 b) 其余一次做完（前端/内联/自动微分/全部后端）？** 我倾向拆：a 已经能拿清单+全层自证，且不碰任何语义；b 是文档 `:266` 说的那件"一次改动前端、内联、自动微分与全部后端"的事。
2. **若不拆，a 这一笔留还是弃？** 留下等于先落一半（文档 `:262` 先变真），弃掉等于等 b 一起。
3. **`GraphDraft` 的删除怎么安排？** 现读：`GraphDraft` 出现在 `src/`、`include/`、`modules/`、`stdlib/` 的 95 个文件、850 行里（`git grep -c`，`abbc9c229`），删它必然与"前端在事务中直接建图"同步；b 是否仍按文档 `:303` 的"一次做完"，还是先做"前端在事务中建图"、`GraphDraft` 留到最后删。