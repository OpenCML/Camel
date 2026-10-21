# 第 5 步：控制区域（分支与循环共用帧）——实施计划

依据：设计稿 `docs/design/graph-store/README.cn.md` §3.3、决定 8–9；总纲 A5；代码基线 fsh-dev（含 identity 的 `GraphId`/`Program`、gs/wide 的分块与墓碑）；scans 的 `ExecPlan` 读自分支 `gs/plan`（`include/camel/runtime/exec_plan.h`）。只读调研，没有改代码。

## 0. 现状（读代码得到的事实）

1. **分支**：前端 `Builder::buildBranch`（`builder_control.cpp:1178`）为每个分支臂建一张**子图**（`buildArmGraph`），在父图里用一个 `FUNC` 节点调用它（`createFuncDataNode`），`BRCH` 以控制边指向各臂的 `FUNC`，臂的结果以 with 输入接到 `JOIN`；臂里用到的外层变量走**捕获表**（`CapturedUse`、`captureSites`、`captureInto`，`captures.cpp` 182 行，在 `builder*.cpp` 共 41 处）。`BRCH` 负载里还有每臂的 `GCBranchArm{head, tail}` 锚点。
2. **内联后才是"区域"**：`std::opt::inline` 把臂子图内联进父图后，臂成为 BRCH 与 JOIN 之间的一段节点；所有后端靠**推算**识别它：`collectRuntimeBranchArmRegion`/`resolveRuntimeBranchArmEntry`/`runtimeArmEndsWithValue`/`collectRuntimeTailJoinArmValues`（`graph_runtime_support.cpp`）、`branch_arms.cpp`（359 行，提交时校验"臂不重叠"）、`insideBranchArm`（`draft_inline.cpp`）、GATE 作臂锚点的特殊处理（`dissolveUnorderedGates`）。scans 的 `ExecPlan` 把这些推算集中成 `branchArmEntries`（`exec_plan.cpp` 162 行）。没有跑 `std::opt::inline` 时，臂就是真正的调用，每次进入建一帧（设计稿 3.3"历史"）。
3. **循环**：`LOOP` 节点（kind 15）的负载 `GCLoopRef` 指向条件图与循环体图两张子图（引用表下标，两者都是 callee），With 输入是两图共享的捕获表（`captureTwins_` 保证两张捕获表逐位相同）。后端：nvm 与宏求值器用 `loop_driver.h` 的 `driveLoopRounds`，每轮**调用**条件图一次、循环体图一次；fvm/jit/taskflow/std::cpp 默认先 `lowerLoopsToTailRecursion`（三张新图 rec/next/done，`loop_lowering.cpp`），原生 `SCHD` 指令因为每轮两次嵌套解释器进入反而更慢（`docs/technical/39_loops.md` §5：一千万轮，fvm 原生 2.61 s、降低 1.33 s、手写尾递归 0.49 s）。
4. 涉及面（grep）：`GCNodeKind::Brch` 36 个文件 76 处；`Join` 34 文件 78 处；`Loop` 41 文件 66 处；`branchArms/BranchArm` 48 文件 186 处；`LoopView/LoopDraftView` 14 文件 85 处；`lowerLoopsToTailRecursion` 10 文件。ONNX 导出器不是结构化翻译而是**抽象求值**（`exporter.cpp` 的 `evalBranch`/`translateLoop` 把尾递归翻成 Loop；`LOOP` 节点现在被拒绝）。Tiles 经 `LoopView::bodyGraph()` 读循环体图（`compiler.cpp`、`region_outline.cpp`）。autodiff 对循环体图当函数求导（`pullback.cpp` 3000+ 行，H4d 磁带＋反向循环）。

## 1. 目标与不变量

- **区域是同一张图里标出的一组节点**，不建帧、不传参、共用所在函数的帧与槽位（设计稿 3.3）。控制区域＝语义（节点只在区域被执行时才执行）；标注区域＝记录。
- 分支臂与循环的两个部分都成为控制区域；**前端直接发射区域**，不再有臂子图、循环子图、捕获表。区域内的节点可以直接引用外层区域（含函数根）的节点，因为共用帧。
- 外层节点**不得**读区域内部的节点，区域的值只经所有者节点（`JOIN`/`LOOP`）出来。提交时的校验器强制（红线）。
- 与分块布局、稳定 id 兼容：区域表按 `RegionId` 稳定（不复用，同 NodeId），节点的区域列随块共享。
- 性能：分支性能与今天"内联后形态"相同（执行计划摊平）；循环不再每轮付调用，是帧内跳回。

## 2. 数据模型

### 2.1 节点的区域列与区域表

- `GCNodeChunk` 增一列 `regions`（`uint32`，节点所在的**最内层**区域；0 = 函数根；整块全 0 时列为空指针，同 `permissions`/`attrs` 的做法）。草稿里 `DraftNodeHeader` 加同名字段。放在列而不是 16 位的 `GCNode::reserved`，因为区域数不设上限。
- 图级**区域表** `GCRegion[]`（图头上的数组，版本之间整体复制，通常很小）：
  ```
  struct GCRegion { uint8 kind;      // Arm, LoopCond, LoopBody, Inline, Block
                    uint32 parent;   // 外层区域，0 = 根
                    uint32 owner;    // 控制区域：拥有它的 BRCH/LOOP 节点；标注区域：kInvalidNodeRef
                    uint32 index;    // Arm：第几臂；LoopCond/LoopBody：0
                    uint32 result;   // 区域产出的值所在节点（kInvalidNodeRef：无值）
                    uint32 portBegin, portCount;  // 区域形参（见 2.3）在 regionPorts[] 里的区间
                    uint32 origin;   // 源码位置；Inline：被内联函数的 GraphId 与调用节点见 inlineOf
                    uint32 attrs;    // 标签集（node_attrs.h）
                    GraphId inlineOf; uint32 callSite; };   // 仅 Inline
  ```
  `regionPorts[]` 是区域形参节点的 NodeId 列表。区域表、`regionPorts` 是图版本上不可变的数组，随编码一起写入；校验器与 `std::gir` 读取。
- `RegionId` 稳定：新区域取新编号，删除的区域留墓碑（`kind = Dead`），整理时与 NodeMap 同步。`GraphDraft` 里是 `std::vector<GCRegion>`，`addRegion/setRegionOf(node)/regionOf(node)/regionNodes(r)`；`regionNodes` 靠节点表按区域聚类的索引（草稿内维护 `members_`，O(1) 增删，同读者表的做法），不扫描。
- 嵌套是树（区域不部分重叠），`parent` 链的深度通常 ≤ 5；"A 在 B 内"用祖先链判断。

### 2.2 分支

- `BRCH`/`JOIN` 两个节点**暂时保留**（第一步不合并，见 8 节 R5）：`BRCH` 的 Norm 输入是选择值（`match` 的 `case` 值在 With 输入），拥有 N 个 `Arm` 区域；`JOIN` 的 With 输入第 i 个是第 i 臂的 `result`，值是被选中臂的结果。`GCBranchArm{head,tail}`、`DraftBrchPayload` 里的臂数组、`defaultArm` 删除，臂就是 `owner==BRCH` 的区域，按 `index` 排序；`BRCH` 负载只留 `join` 与 `armCount`。
- 臂内节点之间的顺序仍用数据边与控制边；原来 `BRCH → 臂头` 与 `臂尾 → JOIN` 两类控制边由**成员关系**取代（区域内每个根节点天然依赖所有者），不再存储。
- `match` 的 default 臂＝最后一个 `Arm` 区域，`index` 即优先级。

### 2.3 循环

- `LOOP` 节点拥有两个区域：`LoopCond`（`while`/`for…while` 才有）与 `LoopBody`。**区域形参**是区域内的 `Port` 节点：循环体区域的形参依次是（元素 x，若迭代）、状态 s1..sk；条件区域的形参是状态 s1..sk。形参的槽位就是帧里的槽位，每轮由循环写入（今天由 `bindLoopRound` 写进被调帧的端口槽）。**不再有 With 输入/捕获端口**：区域里的节点直接引用外层节点。
- `LOOP` 的输入：Norm＝（被迭代的数组，初值 a1..ak），Ctrl＝顺序；负载：`{kind, collectionCount, condRegion, bodyRegion}`（`GCLoopRef` 去掉两个图引用，换成区域号）。循环体区域的 `result` 是下一状态（k 元组或单值）后跟 s 个收集值，条件区域的 `result` 是 bool。语义不变（`loop.h` 的注释），收集槽位、磁带形状不变。
- 外部输入：LOOP 节点的"隐含输入"是两个区域的外部输入集合（对根或祖先区域中节点的引用）。拓扑序、数据依赖与死代码判断需要它：由 `regionExternalInputs(r)` 给出（执行计划与图事实各缓存一份，随版本失效；草稿里随 `members_` 增量维护），**不存储**，避免两处不一致。

### 2.4 标注区域（内联来源等）

`Inline`（来源：被内联函数的 `GraphId`、调用节点）与 `Block`（`#[...] {}`、`@d {}`）。对优化透明；任何遍可以把节点移出或消去区域。区域上的声明标签（`attrs`）供设计原则 9.4 的警告与调度（13.1）使用。第一个能落地的部分（R1）。

### 2.5 校验器（取代 `branch_arms.cpp`）

提交时检查（红线）：(1) 区域树合法（父在子之前、不循环、`owner` 的 kind 与区域 kind 相符）；(2) 控制区域内的节点只引用同区域、祖先区域的节点或区域形参；(3) 区域外的节点不读区域内部节点，区域的值只经 `JOIN.with`/`LOOP` 输出；(4) 有副作用或会出错的节点只在其应在的控制区域（不被移出）——由节点的纯度/故障事实判定；(5) `JOIN.with[i]` 的节点在第 i 个臂区域内（或是其 `result`）；(6) `LOOP` 的形参个数与种类等于 k（及元素），`collectionCount` 与 `result` 元组成员数一致（取代 `validateRuntimeLoopInterface` 的"两张图接口"检查）。违反则报 `PassInternalError`，指名遍、区域与节点。

## 3. 前端发射

- `Builder` 持有"当前区域"（`currRegion_`）：`addXNode` 都把节点记入当前区域。`buildBranch` 创建 `BRCH`/`JOIN`，为每臂 `beginRegion(Arm, owner=brch, index=i)`，在**同一张编译图**里 `visit` 该臂，`endRegion(result)`；名字作用域压入子作用域，直接解析到外层节点。
- 因此删除：`buildArmGraph`、`createFuncDataNode(arm)`、`addCaptureSite`（臂用途）、`CapturedUse`（臂/循环用途）、`captureTwins_`、`makeLoopGraph/buildLoopGraph`、`coerceArmExit` 里"改臂图的出口"（改为在臂区域里加 `CAST` 并更新 `result`）。保留函数/lambda 的捕获（函数仍是独立图）。`rejectDiscardedValue`、`EffectNotExecuted`、`IgnoredSideEffect` 等诊断改读"区域内节点"，不再读臂子图。
- `var`/`sync` 语义：区域内的 `var` 写是对外层 `var` 节点的普通有序写（同帧），不再需要为臂传 cell；被闭包捕获的 `var` 仍用 cell。需要在 `builder_operators` 的变量赋值路径核对 `written` 判定的来源（现在来自 `capturedUsesOf(arm)`）。
- 循环：`visitLoopNode` 先在当前区域建初值与序列，再 `beginRegion(LoopCond/LoopBody)`，用 `Port` 节点建形参，`visit` 条件/循环体，`endRegion(result)`；`markLoopStateInPlace` 等"原地写"判定改读区域内节点。
- 前端产出直接是最终形态，不再有"未内联的臂＝真正调用"这一档。**默认流水线不跑 `std::opt` 时，`if` 也不再每次进入建帧**（性能收益，见 9 节）。

## 4. 执行计划（scans 的 ExecPlan）怎样表示区域

- `ExecPlan::topo` 仍是一个线性序，但区域成员**连续**：根序列里，`BRCH` 之后依次是各臂区域的节点段（段内按拓扑序，嵌套区域的段嵌在其中），最后是 `JOIN`。`ExecNode` 增加 `region`（`uint32`）。
- 新表：`regionRanges[r] = {begin, end}`（`topo` 下标）；对 `BRCH`：`branchArmEntries` 由 `regionRanges` 直接给出（取代对锚点的推算，原来的 `resolveRuntimeBranchArmEntry`）；对 `LOOP`：`LoopPlan{ condBegin, condEnd, bodyBegin, bodyEnd, 形参槽位（元素、k 个状态）、condResultSlot、bodyResultSlot(s)、收集槽位数、状态落点槽位 }`，按 `LOOP` 的 topo 下标索引。
- 区域内的槽位仍是函数帧里的槽位：前端/编码为区域内每个节点分配槽位（今天每张子图有自己的帧布局，现在并入父图帧；`allocateRuntimeSlot` 已是 32 位）。区域内临时值不能被区域外读，所以槽位可以在互斥的臂之间**复用**——第一版不做，先求正确。
- 尾调用判定（`directCallTailEligible`、`tailValueIsJoin`）改为：函数结果＝根区域的 `result`；若它是 `JOIN`，则各臂区域的 `result` 的 `FUNC` 且其后无非平凡后继者是尾调用；用区域的 `result` 取代 `collectRuntimeTailJoinArmValues`/`runtimeArmEndsWithValue`。循环区域内**不是**尾位置。

## 5. 各后端

| 后端 | 分支 | 循环 |
|---|---|---|
| **nvm** (`NodeVMSchedPass`) | 计划里的臂入口/出口范围；不再 `collectRuntimeBranchArmRegion` | `evalLoop` 重写为在**同一帧**里：写初值到形参槽 → 循环 { 跑条件段 → 读 bool → 取元素 → 跑循环体段 → 把 `result` 复制到状态槽，追加收集值 }；段是计划里的 topo 范围，不再 `callGraph`。`driveLoopRounds` 保留状态/收集逻辑，服务接口改成"跑一个区域段"。 |
| **fvm** (`compile.cpp`、`gotovm/casevm`) | 与今天内联形态的字节码一致（`BRCH`＋跳转表＋`JUMP`），由区域范围生成；`brchTargetMap/joinTargetMap` 的推算删除 | 新增字节码 `LOOPHEAD/LOOPTEST/LOOPBACK`（或复用 `BRCH`＋`JUMP`）：状态在帧槽，条件段、循环体段是普通指令，**向后跳转**；不再降低成尾递归，`native_loop` 开关删除，原生 `SCHD` 循环标记删除。收集用 `:loop/collect` 同款内联指令。 |
| **jit** (x64 后端) | 同今天 | 循环体段编成原生向后跳转；解释器与 JIT 共享区域范围。JIT 的"块起点"集合（`blockStarts`）加入循环头。 |
| **taskflow** (`tf/prl`) | 臂成员作为"被跳过集合"（`skipNodes`）的来源，取代锚点推算 | `LOOP` 作为**一个任务**，在该任务里按 nvm 的方式顺序执行区域段（数据流并行只在循环外）；这与今天"先降低成尾递归"在并行度上等价，但没有每轮建帧。 |
| **宏求值器** (`macro/eval.cpp`) | 臂区域 | 同 nvm，按轮上限 `--max-macro-rounds`。 |
| **std::cpp** (`function_emitter`) | 臂区域生成 `if/else`、`switch` | 直接生成 `while`/`for` 循环（今天靠尾调用被编译器优化成循环）；状态是局部变量。 |
| **ONNX** (`modules/onnx`) | 导出器是抽象求值：`evalBranch` 把臂区域当作"在同一环境里求值一段"，取代 `collectRuntimeBranchArmRegion` | `LOOP` 节点不再被拒绝：循环体区域→ONNX `Loop` 的 body 图，条件→`cond`，收集槽位→scan output（形状天然对应，设计稿 3.3）；第一阶段仍可保留拒绝，等 R3 稳定后再做。 |

## 6. 其它消费者

- **内联器** (`draft_inline.cpp`、`engine.cpp`)：被内联图的节点进入调用点所在区域，外加一个 `Inline` 标注区域（R1）；`insideBranchArm(draft, funcNodeId)` 改为 `regionOf` 的祖先链是否含 `Arm`；臂不再是 `FUNC`，**所有 `FUNC` 都是真正的函数调用**，`CallBook`/`draftRefs` 不再混有"臂调用"；"内联后才是臂"的一整类特例（`arm_tail_gate` 之类历史缺陷）消失。
- **改写遍**：`gates.cpp`（`dissolveUnorderedGates` 的"臂锚点不能删"）、`screen.cpp`（BRCH/JOIN id 表）、`rewrite.cpp`（`rewriteReachableGraphs`）、`control.cpp`（控制依赖约简）改读区域：控制依赖约简在区域边界处停（区域根节点到所有者的依赖是隐含的）；CSE/折叠跨区域时按设计稿"移出控制区域仅当纯且不出错"。
- **autodiff**：不重写 3000 行求导，而用**区域抽取/嵌入**两个工具函数：`extractRegionAsGraph(loopBody)` 把区域＋其外部输入打成一张临时图（形参＝区域形参＋外部输入的捕获端口），现有的"循环体当函数求导、残差端口、磁带"原样运行；结果用 `embedGraphAsRegion`（实质是内联，带一个 `Block` 标注区域）放回新的 `LOOP` 区域。反向循环是第二个 `LOOP`。分支同理。这两个函数也是 Tiles 与 ONNX 过渡期的垫片（`LoopView::bodyGraph()` 暂时返回抽取出的缓存图，让 Tiles 等先不改）。
- **Tiles** (`compiler.cpp`、`region_outline.cpp`)：过渡期读垫片；之后改为直接读区域。`region_extract.cpp` 已有"取一组节点成新图"的逻辑，可与 `extractRegionAsGraph` 合并。
- **事实/资源/效应** (`core_facts`、`resources.cpp`、`effects.cpp`、`fact_persist`)：效应与资源足迹原来按"臂图/循环图"分别算再汇总，现在按区域算：区域的足迹是其节点足迹之并，所有者节点的足迹是各区域之并。事实的锚点（图，节点）不变；增加（图，区域）锚点种类用于循环域事实。
- **调试器**：臂/循环体不再是独立帧；帧内按区域折叠显示（今天"折叠进其函数帧"是默认，原生循环后不再需要特殊 `wantsLoopFrames`）。`std::rgir/gir` 把区域画成可展开的框（cluster）；`jgir` 增 `regions` 数组与每节点 `region`。

## 7. 消失的东西

臂子图与 `FUNC` 臂调用；`GCBranchArm`、`DraftBrchPayload` 的臂数组与 `defaultArm`、`retargetBranchArmAnchors`、`isBranchArmAnchor`；`branch_arms.cpp`（换成 2.5 的区域校验器）；`collectRuntimeBranchArmRegion`、`resolveRuntimeBranchArmEntry`、`runtimeArmEndsWithValue`、`collectRuntimeTailJoinArmValues`、`hasOnlyTrivialRuntimeTailSuffixAfter` 的臂部分；GATE 作为臂锚点的角色（GATE 仍用于转发/顺序，不再为分支存在）；`loop_lowering.cpp`（≈615 行）与 `lowerLoopsToTailRecursion`、`Program::original` 里"私有降低"的用途、`:graph/original` 为降低而设的转换；`GCLoopRef` 的两个图引用与 `GCLoopBody`；前端捕获表中为臂/循环服务的部分、`captureTwins_`；`LoopView` 的图指针（改为区域号与形参节点）；`native_loop`/`CAMEL_FVM_NATIVE_LOOP`、`SCHD` 的 LoopWhile/LoopFor/LoopForWhile 标记；`driveLoopRounds` 里"调用图"的服务。

## 8. 提交顺序（每个提交都能构建，smoke＋相关计划通过）

- **R0 区域骨架**（无行为变化）：`GCNodeChunk::regions` 列、图级区域表与 `regionPorts`、`GraphDraft` 的区域 API 与 `members_` 索引、编码/解码、`sameContent`/记忆哈希/块共享比较、`jgir`/`rgir` 输出区域、校验器骨架（只检查树）。所有图只有根区域。
- **R1 标注区域**：内联器产出 `Inline` 区域（内联来源）；`#[]` 块留到标签落地；调试器与 `rgir` 显示。可独立合入。
- **R2a 分支：区域与旧形态并存**：在编码前由旧推算（`collectRuntimeBranchArmRegion`）为已内联的臂**填充**区域，校验器断言"推算结果＝记录结果"（过渡期双重检查，覆盖全部现有测试）。
- **R2b 后端改读区域**：`ExecPlan` 的臂入口、尾调用判定、fvm/nvm/taskflow/std::cpp/ONNX/宏求值器改读区域；推算函数仍在，仅作断言。
- **R2c 前端直接发射臂区域**：删臂子图与捕获；内联器去掉臂特例；删推算函数、`branch_arms.cpp`、锚点负载；`BRCH` 负载只留 `join`、`armCount`。这一步最大（`builder_control` 约 600 行、`captures.cpp`、诊断），单独一个提交序列（约 4 个）。
- **R3a 循环：区域 API 与垫片**：`LoopView` 增区域访问器，`bodyGraph()/condGraph()` 返回抽取出的缓存图（旧负载仍在）；`extractRegionAsGraph`/`embedGraphAsRegion`。
- **R3b 前端直接发射循环区域**＋`GCLoopRef` 改为区域号；nvm 与宏求值器原生区域循环；其余后端暂用**新的**降低（从区域生成 rec/next/done，`loop_lowering` 的输入从两图改为两区域抽取）。
- **R3c fvm 字节码循环＋jit**；**R3d taskflow、std::cpp**；删 `native_loop`。
- **R3e autodiff**（经抽取/嵌入）；**R3f Tiles/ONNX** 读区域；删垫片、`loop_lowering.cpp`、`lowerLoopsToTailRecursion`、`Program::original` 的降低用途。
- **R4 区域内槽位复用**（可选，性能）。
- **R5 合并 `JOIN` 进 `BRCH`**（可选）：`if` 成为单个带区域的节点，区域的 `result` 即值；设计稿的最终形态。放最后，因为它触到所有 JOIN 消费者（34 个文件）。

## 9. 测试

- 现有：`linear/loops`（59 条，含降低/原生/宏/调试/诊断）、`linear/loop_collect`、`opt/*`（内联、`arm_tail_gate`、`screens`）、`trans`（gir/rgir/agir/jgir 黄金文件会变：区域框与 `regions`；逐条核对）、`modules/autodiff`（循环求导）、`modules/tiles`（`loop_structure_*`）、`modules/onnx`（分支导出）、`consistency/*`、`tools.debugger`（`debugger.loop_frames`）、`test/tools/cppexec` 的循环程序、progen 全矩阵（`loop` 与分支特性）。
- 新增：区域校验器的反例（区域外节点读区域内节点；有副作用节点被移出区域；`JOIN` 臂结果不在臂内；循环形参个数不符）；`test::regions`（草稿级：移动/嵌套/删除区域的 API）；循环**不是调用**：`--max-recursion-depth 1` 跑百万轮在所有后端通过，且帧池无分配（nvm/fvm/jit 的帧计数）；`if` 在不跑 `std::opt` 时不建帧；区域内 `var` 写顺序；嵌套区域（循环里的 `if` 里的循环）；区域与稳定 id（改写后区域号不变）。

## 10. 性能预期（待测量，红线用交错 A/B＋噪声底）

- **循环**：现状（一千万轮，两个状态值）nvm 1.71 s、fvm 1.33 s、jit 1.50 s，手写尾递归 fvm 0.49 s。帧内向后跳转后目标是 **fvm/jit 不慢于手写尾递归的一半**（≤ 0.25 s 量级；jit 可编成真正的原生循环更快），nvm 去掉每轮两次 `callGraph` 后目标 ≤ 0.7 s。收集循环去掉每轮元组分配的部分取决于 R4。
- **分支**：已内联形态不变；未内联（默认不跑 `std::opt`）的 `if` 去掉每次进入的建帧，递归程序预计明显变快（量级待测，fib 类）。
- **图大小**：每个臂/循环少一张图头（今天一张小图约 1–2 KB 含引用表、帧布局），多一个区域表项（约 40 字节）与每节点区域列（4 字节，根全 0 时为空）；程序里 `if`/循环多的函数内存下降。
- 编译时间：前端不再为每个臂建 `DraftGraphBuilder`＋捕获表，预计下降；`ExecPlan` 构建多一遍区域分段，线性。

## 11. 风险

1. **前端面最大**：`builder_control.cpp`（2000 行）、`captures.cpp`、`builder_diagnostics.cpp`（`isEffectNode` 经臂图）、`builder_operators.cpp`（变量写入、`written` 判定）。缓解：R2a/R2b 先让后端依赖区域、推算做断言，R2c 单独推进，每个提交跑全套编译/线性计划。
2. **帧大小与槽位**：臂并入父帧后帧变大（深递归的每层更重）。互斥臂复用槽位（R4）可缓解；先测 fib 类的峰值内存。
3. **autodiff**：抽取/嵌入垫片必须保持"残差端口＝磁带"的形状；`loop_collect`、`grad_through_loop*` 是主要保险。
4. **JIT**：向后跳转与块起点、寄存器分配的活跃区间跨循环；jit 覆盖范围（`jitcov`）要先回归。
5. **并行**：taskflow 把 `LOOP` 当单任务，循环体内的数据并行丢失（今天降低成尾递归同样串行，无回退）。
6. **与 `std::opt`/screens 的交互**：scans 的 `check_screens` 假定 BRCH/JOIN id 表与臂锚点；R2c 要改它们的不变量。
7. **黄金文件与调试器协议**：`rgir/agir/jgir`、调试器的帧/断点 API 暴露区域，是外部可见变化，需同步 `tools/debugger/docs/API.md`。
8. **稳定 id/分块**：区域列属于分块，`sameContent`/块共享比较/记忆哈希/`keepsNodeIds` 都要带上区域列与区域表（R0 做）。

## 12. 与其它包的关系

- identity：区域上的 `Inline.inlineOf` 用 `GraphId`；图引用已是 `GraphId`，抽取/嵌入的临时图不进 `Program`（缓存在版本上，不登记）。无冲突。
- scans/plan：`ExecPlan` 是 R2b/R3 的主要改动点（`exec_plan.cpp` 162 行，BRCH 入口、尾调用、新增 `LoopPlan`）；需要与 plan 作者约定数据结构，建议他们的 `ExecNode` 先留 `region` 字段。screens/inliner 工作表假设见风险 6。
- 本包（gs/wide）：区域列、区域表、块共享比较、`keepsNodeIds` 由我做（R0）。

## 13. 需要你裁定的问题

- **Q1 JOIN 合并（R5）**要不要做？它是设计稿的最终形态（`if` 一个节点），但触及 34 个文件；建议放到循环完成之后，作为独立步骤。
- **Q2 垫片**：`LoopView::bodyGraph()` 在过渡期返回抽取出的缓存图——是否接受这种"版本上的派生图"（不登记进 `Program`）？替代是 Tiles/ONNX/autodiff 与前端同时改，风险更集中。
- **Q3 循环里是否允许 `return`/函数级控制流跳出区域？**现在循环体是图，`return` 只结束该图；区域化后源码里的 `return` 在循环体里的含义要定（建议：语言现状没有跳出循环的 `return`，保持"循环体的 `return` 是区域结果"）。
- **Q4 区域内槽位复用（R4）**放不放进本步，还是留给执行计划包？
- **Q5 ONNX `Loop`**：R3 稳定后做，还是本步就做？建议下一步，本步只保证"不比今天差"。
- **Q6 `std::fvm(native_loop)` 与 `CAMEL_FVM_NATIVE_LOOP` 删除**可以吗（内测期不考虑兼容）？
