# 内置优化遍与图存储代码评审

范围（只读源码，没有构建与运行；标"待验证"的是读代码推出、没有跑过的结论）：
- `src/passes/opt/generic/`（`generic.cpp` 折叠、分支裁剪、CSE、DCE 与 `std::opt` 驱动；`rewrite.cpp`、`gates.cpp`、`screen.cpp`）。
- `src/passes/opt/inline/engine.cpp`（内联、特化、去虚拟化的调度）与 `src/runtime/draft_inline.cpp`（特化、拼接）。
- 图与草稿存储：`src/runtime/draft.cpp`、`draft_construct.cpp`（`eraseNode`、`unlinkInput`）、`control.cpp`、`graph_edges.cpp`（不变量检查）、`graph.cpp` 的校验与发射。
- `writings.cpp` 已有 `code-review-rewrite.md`，这里只引用，不重复。
- 对照：设计原则 3.4、5.11、9；`docs/design/effects/edges.cn.md` 的 4.6、5.3。

按严重程度排序：A 是会静默丢掉故障或顺序的问题；B 是没有上界的编译期开销；C 是二次复杂度；D 是静默截断；E 是不变量检查；F 是重复、巨型函数与注释；G 是缺的测试。

---

## A. 会静默出错的问题

### A1. DCE 删得掉"可能出故障"的纯节点（与重写评审的 A1 同一个根）
原则 3.4：可能出故障的计算与效应一样留在书写的位置，只有"纯且不会出故障"的才能自由删除或挪动。遍里"能不能丢"的判断全部落在 `OperatorTable::mayBeRecomputed`，它的定义是 `effect == Pure && foldable`（`operator_table.cpp:170-173`），不含故障；而共享分类 `nodeIsFreelySchedulable`（`graph.cpp:68-75`）本来就是"纯 ∧ 不会出故障"，遍没有用它。
- `isValueOnly`（`rewrite.cpp:60-77`）把纯 `Oper`（含 `op/div_i` 这类 `mayFault` 的）、`Cast`、`Copy`、`Accs`、`Fill` 无条件算"只有值"。
- `dceDraft`（`generic.cpp:889-925`，条件在 915-919）：`isValueOnly ∧ 没有 norm/with 用户 ∧ isReplaceable` 就 `replaceNode(.., kInvalidNodeRef)`。控制用户、同步用户都不拦（`replaceNode` 把前驱转给用户，同步链由 `eraseNode` 缝合）。
- `nodeIsFreelySchedulable` 对非 `Oper` 的节点种类直接说"不产生任何故障"（`graph.cpp:68-70` 的注释），但 `Cast` 会出故障：`as_shape_fault.cml` 就是测这个，`foldStaticCast`（`generic.cpp:542`）自己的注释也说"检查型转换失败要带位置报错"。这一条同时让 `splicedBodyNeedsOrder`（`draft_inline.cpp:1300`）与构建器的同步链判定（`builder_helpers.h:485`）对 `Cast` 不给顺序边。
- **场景**：`func f(z: int): int sync { let r = 5 / z; println('after'); return 0 }`，`z == 0`：不开遍时故障、不打印 `after`；开 `std::opt::dce` 或 `std::opt` 后 `r` 没有值用户，被删，打印 `after`。无关 `z` 的变体：`let b = shaped(3, 2) as f32[2, 3]` 之后不再使用 `b`，转换失败的故障同样消失。（待验证：我没有运行；`test/cases/consistency/faults/` 的 40 多个用例全部不带遍。）
- **修**：`isValueOnly` 改成"`nodeIsFreelySchedulable`，并让 `Cast` 按 `castSafetyFrom` 判（Safe 的基本类型转换不会失败，其余算可能失败）"；DCE、`mayRemoveDead`（`screen.cpp:176-232`）和 `isFoldable` 用同一个。`Accs` 的按下标/按键读取同理按其自身事实判。
- **折叠为什么没事**：折叠只替换"能算出常量"的，算不出常量时会警告并保留（`foldDraft`，`generic.cpp:700-726`），这条做对了；CSE 合并两个相同的可能出故障的节点也不改变故障，可以不动。

### A2. 内联时"被调用者有没有效应"也漏了故障
`inlineCallInOrder`（`engine.cpp:1098`）用 `mayHaveEffects`（`engine.cpp:136-181`）决定要不要把参数的有序来源挂到调用上，注释写"纯被调用者不加边"。`mayHaveEffects` 只问"有非 `mayBeRecomputed` 的算子或调用"，纯但可能出故障的被调用者（如 `func f(a: int, b: int): int { return 1 / b }`）判为无效应。
- **场景**：实参 `a` 的来源是一次有效应的调用，被调用者不读 `a`，内联后 `1 / b` 与 `a` 的来源之间没有顺序，故障可以先于那个效应发生。拼接侧（`splicedBodyNeedsOrder`）给函数体内的顺序边，这一半没有。（待验证。）
- **修**：同 A1，用"含有不自由的节点"代替"含有效应"；`splicedBodyNeedsOrder` 与它共用一个函数。顺带：`mayHaveEffects` 每次内联都把被调用者子树全走一遍（C4）。

### A3. 边的种类在至少八处手写枚举，同步边（和顺序边）时有时无
同步边是 5.3 的链，GATE 的"等待"可以是控制输入也可以是同步前驱。下面这些遍历各自列了一遍要走的边，口径不一：

| 位置 | 走哪些边 | 与 `isFoldable`（`generic.cpp:166-170`，控制或同步都算"在等"）的差别 |
|---|---|---|
| `reaches`（`rewrite.cpp:86-106`），CSE 用它防环（`generic.cpp:791、835`） | norm、with、ctrl | 没有同步、顺序边 |
| `findCycle`（`graph_edges.cpp:266`，分组 0 到 3） | norm、with、ctrl、order | 没有同步边；经同步边的环过得了封存检查 |
| `upstreamOf`（`engine.cpp:233-251`） | norm、with、ctrl | 注释写"每一种边"，实际没有同步、顺序 |
| `gateInputsSurvive`（`generic.cpp:865-887`） | norm、with、ctrl | 没有同步前驱，而 5.3 说带同步前驱的 GATE"不得删除" |
| 特化里绑定实参后的"改为等这个 GATE"（`draft_inline.cpp:1090-1110`） | 只看 `ctrlInputsOf` | 同步 GATE 包着的常量被绑定后，GATE 无用户，等待丢失 |
| `insideBranchArm`（`draft_inline.cpp:543`） | 三种用户边 | 同上 |
| `readsNoWrittenSlot`、`orderedSources`（`rewrite.cpp:133、159`） | 四种都走 | 对的 |

- **场景（待验证）**：一个同步函数的值是常量 `GATE(0, 同步←println)`，被内联进非同步调用者且结果被特化绑进另一个调用：那个 GATE 失去用户，`isDeadGate`（`generic.cpp:912-914`）里 `gateInputsSurvive` 不看同步前驱，GATE 被删，`println` 失去通往出口的路。`dissolveUnorderedGates`（`draft.cpp:1887` 起）的注释把这条规则引了原文，自己是守的。
- **修**：草稿上提供 `forEachInput(id, kinds, f)`、`isWaiting(id)`、`reachesForward/Backward(set, kinds)` 各一份，遍和草稿都从那里取，枚举边种类的地方只剩一处；`findCycle` 加上同步边（5.3 说同步边与控制边同写，环检查应对两者都成立）。

---

## B. 没有上界的编译期开销

### B1. 折叠没有结果大小与时间的上限
`foldDraft`（`generic.cpp:632-752`）对"所有实参都是常量的纯算子"在编译期直接调内核，结果冻成常量写进图。没有结果字节数上限，没有时间预算，也没有"这个算子便宜"的声明（`foldable` 默认 `true`，只有 `rewrite` 模块调过 `declareUnfoldable`，`operator_table.cpp:138`）。
- **场景**：`'ab'->repeat<1000000000>` 在编译期造出 2 GB 字符串并随程序保存；`zeros([100000, 100000])`、`range(0, 10**9)` 同理，编译进程耗尽内存而不是运行时。`!foldable`（锚点节点）的情况下还会先把内核跑一遍只为了发警告（`generic.cpp:690-715`），然后丢弃。
- **修**：折叠前用类型估计结果字节数（数组、字符串、张量的静态形状），超过阈值（建议 1 MiB，可由遍参数调）不折叠；锚点节点不跑内核，发警告只在确定会折叠时；`catch (...)`（`generic.cpp:726`）至少放过 `bad_alloc` 之外的运行期故障之外的异常时记日志。

---

## C. 二次复杂度

1. **CSE 的 `reaches`**（`rewrite.cpp:86`）每次调用分配 `vector<bool>(节点槽数)` 并向下游走到底。第一个循环对每个重复常量调一次（`generic.cpp:791`），第二个循环对每个候选调一次（`:835`）。N 个相同字面量的图是 O(重复数 × N)。修：常量合并不需要 `reaches`（常量没有输入，不可能在任何节点的下游），去掉；表达式合并用拓扑序号比较，或用带时间戳的访问数组复用。
2. **DCE 的不动点**（`generic.cpp:889-925`）按节点号升序扫，生产者号小于消费者：一条长度 L 的死链每一轮只能删最末一个，L 轮 × 每轮 O(N)，每轮还对每个 GATE 调 `gateInputsSurvive`。修：删除一个节点后把它的输入压进工作表，只重查这些。
3. **折叠的不动点**：每轮全图扫一遍加整图 `reinferDraftTypes`（`generic.cpp:748`）；`pruneBranch` 每个 BRCH 调 `branchArmNodes`（`:245`），后者扫全图与全部区域，O(BRCH 数 × N)。修：区域到节点的索引一次建好；类型重推只对改动节点的下游。
4. **内联**：`inlineCallInOrder` 对调用的每个用户调一次 `upstreamOf`（`engine.cpp:1130`，O(用户数 × N)），`callUsers` 用线性 `find` 去重（O(U²)）；每个内联成功后 `break` 重新收集可达闭包（`engine.cpp:1014`），闭包 G 个图、内联 K 次就是 O(K × G)；`mayHaveEffects` 每次内联走被调用者整棵子树；`analyzeRuntimeCallGraphScc`（`:399`）里用递归 `std::function` 做 DFS，调用链很深（生成的程序、长递归链）时栈溢出，`std::find(component...)`（`:501`）是 O(分量²)。修：SCC 用迭代 Tarjan；`mayHaveEffects` 按图缓存（以图的版本号作键）；用户去重用集合。
5. **特化**：`insideBranchArm`（`draft_inline.cpp:543-572`）对每个候选调用扫过全部 BRCH，每个 BRCH 再向前走到 JOIN；`onCallCycle`（`:520`）对每个候选调用遍历被调用者可达的整棵图。每轮每个 FUNC 都付一次。修：按图缓存"每个节点是否在某臂内"，按 SCC 分析（`engine.cpp` 里已经算了 `recursiveGraphs`）回答 `onCallCycle`。
6. **CSE 的键**（`generic.cpp:756-850`）把 URI 字符串和两个 `vector` 拷进 `std::map` 的键；`operIdOf` 已有整数。改用 `OperId` 加哈希表。

---

## D. 静默截断（与 5.11"不设按计数截断"不合）

- `kRuntimeRewriteApplyBudget = 256`（`engine.cpp:47、1032-1043`）：一次会话内联到 256 次就停，只有一条 `WARN` 日志；大程序因此是"部分内联"，结果随调用点数量变。`kMaxSessions = 32`（`:1236`）、`simplifyGraph` 的 `maxRounds = 16`（"still changing" 只是日志，`generic.cpp:1046`）同类。只有增长预算有面向用户的警告。修：三处都走 `raisePassWarning`（折叠已经在用），说明被截断的是哪个预算、剩多少调用没处理。
- 特化的不动点保护 `kTypeSpecializationFixedPointGuard = 4096`（`draft_inline.h:46`，`draft_inline.cpp:1042-1054`）：5.11 要求在**第一次出现增长的调用处**立即报专门的诊断，深度上限只作兜底；这里要先造出 4096 个实例（每个是整张图的解码加编码），然后抛一个没有位置的 `std::runtime_error`。修：增长判定提前到键的结构比较（新键的类型实参包含环上已打开的那个）时就报，带上调用节点的 `nodeOrigin`。
- `RewriteMemo`（`rewrite.cpp:279-368`）只凭 64 位内容哈希判断"这遍对这张图没有可改的"，碰撞时静默跳过（只丢优化，不出错）。概率极小，但 `RewriteMemo` 的注释里没写接受了这个风险，请写明；`checkScreens` 模式已绕过备忘，这点是对的。

---

## E. 不变量检查

做得对的：封存检查 `EdgeChecker`（`graph_edges.cpp`）在发行版里也运行并 `throw`，不依赖 `ASSERT`；`commit` 对每个新图都跑 `validateRuntimeGraphPayload` 与 `validateRuntimeGraphCalls`（`draft_session.cpp:1696-1704`）；`checkScreens` 能发现屏过严（`screen.cpp` 头注释的约定有对应的机制）。要改的：
1. **`ASSERT` 在 `NDEBUG` 下是 `((void)0)`**（`assert.h:70`），而草稿到图的发射把"死节点引用"与"写出规划之外"只交给它：`graph.cpp:795`（`runtimeRef != kInvalidNodeRef`）发行版里把 `kInvalidNodeRef` 写进输入表，靠随后的 `validateRef`（`graph.cpp:459-465`）接住；`graph.cpp:976-988` 的 `at < count` 是**缓冲区写越界**，发行版无检查，顺序边与同步边的发射也不在 `validateRef` 覆盖内（只有 `EdgeChecker` 的"names a node the graph does not have"，要在写完之后）。修：这几处改成不依赖 `ASSERT` 的 `throw`（它们每图一次，不在热路径上）。`eraseNode`、`unlinkInput` 等 `ASSERT(node != nullptr)` 之后立即解引用，同样在发行版是空指针解引用，应改为 `throw`。
2. **没有语义层面的不变量检查**：所有检查都是结构的（边、类型、环、区域）。"遍没有丢掉效应与故障"没有任何地方检查，A1 到 A3 因此只能靠测试撞到。最便宜的办法：`fold`、`cse`、`dce` 提交前后各数一遍每张图里"不自由的节点"（`!nodeIsFreelySchedulable`，含内联后的拼接节点）的个数，只允许减少且减少量等于该遍自己报告的删除数；在测试计划里用 `checkScreens` 同一个开关打开。这条检查就能当场抓住 A1。
3. `findCycle` 不走同步边（A3）。

---

## F. 重复、巨型函数、注释

**重复**
- "这个节点能不能丢/挪/并"有至少六个说法：`isPureOper`（`rewrite.cpp:51`）、`isPureOperNode`（`screen.cpp:80`）、`isValueOnly`、`carriesOrdering`（两份，已在重写评审 D 列出）、`mayHaveEffects`、`nodeIsPure`/`nodeIsFreelySchedulable`（`graph.cpp`）。它们彼此不一致正是 A1、A2 的成因。留一个，放 `camel/runtime`。
- "透过 GATE 看常量"有三份，看穿的集合各不相同：`staticValueOf`（`generic.cpp:96`，GATE 与同值 JOIN）、`constantValue`（`screen.cpp:46`，GATE，JOIN 记为"也许"）、`tryResolveStaticValue`（`draft_inline.cpp:442`，GATE 取最后一个输入、同类型 CAST、COPY、DREF）。折叠看不穿 COPY，特化看不穿 JOIN；屏必须手工跟着遍走（头注释自己写了这个约束）。
- `readsVariable`：`generic.cpp:802` 的 lambda 与 `writings.cpp:433` 的模板，同一规则两份。
- 图遍历：`reaches` 在 `rewrite.cpp:86` 与 `resources.cpp:673` 各一份；另有 `controlPath`（`control.cpp:20`）、`readsNoWrittenSlot`、`upstreamOf`、`orderedSources`、`insideBranchArm`、`onCallCycle`、`branchArmNodes`、`armRegions`，全部是各自手写的栈加 `vector<bool>`。
- "保持常量活到提交"的 `roots.emplace_back(...)` 在 `generic.cpp` 里四份（`:521`、`:586`、`:625`、`:735`），`++folded; changed = true` 七份。收成 `keepAlive(roots, slot, type)` 与一个返回 `bool` 的分派。
- `simplifyGraph` 里 `finish` lambda 与 `OrderReductionPass::apply` 是同一段（`generic.cpp:966-975`、`:984-993`）；五处 `rewriteReachableGraphs(..., "std::opt::xxx", ...)` 的样板。

**巨型函数**：`applyRuntimeOptimizeRewrite`（`engine.cpp:710-1070`，约 360 行，含候选筛选、预算、重启）；`pruneBranch`（`generic.cpp:279-460`，约 180 行）；`foldDraft`（约 120 行，六个分支同一套尾部）；`inlineCallInOrder`（`engine.cpp:1073-1215`，约 140 行，分四段：补边、记用户、记 JOIN 臂、内联后补边）。候选筛选（`engine.cpp:910-1000`）应是一个返回 `Decision{inline, declineReason}` 的纯函数，现在的"declined"表与实际决策是两份代码。

**注释**
- 叙述历史的：`isFoldable` 的文档（`generic.cpp:147-165`）讲"为什么曾经一直留在图里"，`dceDraft` 里"Keeping such a GATE ... kept its value alive for nothing: a closure ..."（`:897-905`），`replaceNode` 里几段"曾经"。规则留下、故事删掉。
- 与代码不符的：`generic.h` 头注释与 `rewrite.h:52` 的 `isReplaceable` 文档都说锚点包括"分支臂的头与尾"，实现只检查出口、输出、返回（`rewrite.cpp:83`）；`foldDraft` 里"A node that cannot be replaced (the head of a branch arm)"（`generic.cpp:683`）；`upstreamOf` 的"每一种边"（A3）；`generic.h` 说 DCE 只删"仅用于控制的 GATE"，实际还会旁路带值的 GATE（`gates.cpp`）。
- 命名：`mayBeRecomputed` 实际是"纯且可折叠"，读起来像"可以重算"（重算还要求不出故障）；换成 `isFoldablePure` 一类。

---

## G. 缺的测试

1. 故障在遍之下保留（A1）：同步块里 `let r = 5 / z` 不再使用、不再使用的检查型转换、下标越界各一个，跑 `std::opt::dce`、`std::opt::cse`、`std::opt` 与三个调度器，期望故障与不开遍时一致。`test/cases/opt/generic/` 只有 `fold_constant_fault*.cml`（折叠）。
2. 纯但会出故障的被调用者被内联（A2）；带同步前驱的 GATE 在 DCE、特化之后仍通往出口（A3）。
3. 折叠结果过大不折叠（B1）；DCE 死链、CSE 大量相同字面量的规模用例，放性能基线里防二次回退（C1、C2）。
4. 预算被截断时用户看得到的警告（D）；特化不动点保护的诊断位置。
5. 幂等：`std::opt` 的结果再跑一遍 `std::opt` 图不变（驱动的"不动点"目前只被日志声明）。
6. `ASSERT` 改 `throw` 之后的负例：构造悬空边的草稿，提交时应抛带图名的异常（现在只有结构检查的正例）。

---

## 建议的处理顺序

1. A1 与 A2（一个谓词，三个调用点），同时补 G1、G2 的用例；加 E2 的"不自由节点计数"检查，它会把今后同类问题挡在测试里。
2. A3 的统一遍历接口，`findCycle` 加同步边。
3. B1（折叠上界）与 D（截断警告），都是小改动。
4. E1（`ASSERT` 改 `throw`）。
5. C1 到 C6 与 F 的拆分，在 1 到 4 之后做；拆 `applyRuntimeOptimizeRewrite` 时先把候选决策抽成纯函数，再谈性能。
