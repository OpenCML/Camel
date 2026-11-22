# 重写引擎代码评审

范围（只读源码，没有构建与运行；标"待验证"的是我没有确认的前提）：
- `src/runtime/writings.cpp`（等价类存储、写法索引、可交换/可结合匹配、抽取、`rewrite`、`saturate`、`whyNot`）、`include/camel/runtime/writings.h`。
- `src/runtime/rules.cpp`（规则两侧读成模式）、`src/compile/gir/rule_templates.cpp` 与 `builder_generics.cpp:553-730`（泛型规则实例化）。
- `modules/rewrite/module.cpp`（4179 行：规则对象、`callWhen`、`engineRules`、`rewrite`/`run`/`saturate`/`extract`/`why_not`/`explain`/`check` 的核）、`fact.cpp`。
- `src/passes/opt/generic/rewrite.cpp`、`gates.cpp`（遍侧的 `carriesOrdering`、`replaceNode`、`RewriteMemo`）。
- 对照设计：`docs/design/rewrite/v2.cn.md`、设计原则 9.7 与 9.11。

按严重程度排序：A 是会静默给出错误结果的正确性问题；B 是引擎状态与终止；C 是性能；D 是重复；E 是结构；F 是注释与文档；G 是缺的测试。

---

## A. 会静默出错的问题

### A1. "丢掉的部分必须纯**且不会出故障**"没有被实现，只检查了"纯"
设计原则 9.7、`v2.cn.md:90` 明确写着 `x * 0 => 0` 不作用于 `(a / b) * 0`，否则故障消失；9.11 又说这由引擎统一检查。引擎里没有任何一处读算子的故障事实（`faults`、`mayFault`）：
- `DraftWritings::pureComputation`（`writings.cpp:1864-1888`）只问 `carriesOrdering`、控制输入、`writtenInPlace`。
- `carriesOrdering`（`writings.cpp:459-486`）对 `Oper` 用 `OperatorTable::mayBeRecomputed`，它的定义是 `effect == Pure && foldable`（`operator_table.cpp:170-173`），不含故障。
- `shapeTakesPart`（`writings.cpp:492-518`）把 `Cast` 与 `Accs` 无条件算作"参与"，而转换（`int(1e30)`）与下标（越界）都会出故障。
- **场景**：整数 `(a / b) * 0` 在 `b == 0` 时原本是故障，`mul_zero` 之后常量 `0`，故障消失；`xs[i] - xs[i] => 0`（若库里有"同类项相消"一类的规则）丢掉一次可能越界的读取。两处都经 `dropsOrdered`（`writings.cpp:1891-1922`）放行。`saturate` 同路径（`:2465`）。
- 抽取同样：它把"没被选中的纯节点"删掉（`:1170-1175`），其中包括可能出故障的；9.6 允许删除"结果没被用到的会出故障的纯计算"，但 9.7 说抽取"不得选一个丢掉可能出故障部分的写法"，两条的边界没有写成代码也没有写进文档（见 F4）。
- **修**：增一个谓词 `freeToDrop(node)` = 纯 ∧ 不会出故障（读 `OperatorSemantics::faults`，`nullopt` 按"可能出故障"；`Cast`、`Accs` 按其自身事实判），3.4 说的"可自由安排的计算"本来就应是一个谓词处处同用。`dropsOrdered` 用它；`takesPart`（匹配与合并同一写法）可以仍用现在的，因为合并两个相同的故障计算不改变故障。
- 测试里只有 `x * 0 => 0` 作用于整数的正例（`explain_pass.cml:23`），没有这条的负例（G1）。

### A2. `fact` 的 `meet`、`widen`、`narrow` 声明了、存了、从不调用；环上的事实被缓存成 `initial`
`fact.cpp:606-610` 把 `join`、`meet`、`widen`、`narrow` 四个函数都登记成根，求值只用 `join`（`Frame::joinValues`，`:245`）。`evaluate`（`:358-420`）遇到正在求值的节点（`active.contains`）就返回 `initialValue()`（`:365-369`），**随后把依赖它的节点的结果写进 `memo`**（`:411`、`:417`）：没有不动点迭代，也没有加宽，环内每个节点的事实按"环上其他成员是 `initial`"算一次并永久缓存。`v2.cn.md:130-131` 的语义（"迭代到上限退回 `case _` 的事实"、`widen` 后再 `narrow`）一条都没有实现。
- **场景**（前提：图里 `normInputs` 上能出现环——循环的状态边如果表示成数据边就会；是否会，待验证。若不会，这段分支与三个函数是死代码）：`fact range` 对循环计数器 `i = i + 1` 得到"`initial` 加一"那个有限区间，被一条 `when range(i) < 100` 的规则当成上界用，规则在 `i` 实际可以达到 100 以上时触发。
- 红线 4 要求分析满足合并律与单调性并由测试工具枚举检查；这里连"迭代到不动点"都没有，更谈不上检查。
- **修**：先确定环是否出现。出现：按设计做带加宽的迭代（节点按 SCC 处理，SCC 内迭代到 `join` 不再增长，超过 `widen_after` 轮后调用 `widen`，收敛后 `narrow`），`memo` 只在 SCC 收敛后写。不出现：删掉环分支和 `meet`/`widen`/`narrow` 的登记，并在 fact 声明里把这三个子句改为"当前不支持"的编译期错误，不要接受了再忽略。

### A3. `fact` 的描述缓存键是名字文本，既不含声明身份也不含值
`fact.cpp:716-791`：`descriptions[key]` 与 `factKindIds[key]` 是进程级静态表，键是 `name` 加 `<with 实参文本>`。
- **跨模块撞名**：模块 `a` 与模块 `b` 各声明 `fact range`，第二个模块的查询用到第一个模块的 `Definition`（它的 `initial`/`join`/各个 case 函数）——静默错误的语义，且 `kind` 名 `user.fact.range` 也撞（`:832-835` 的 `catch` 还把注册冲突当"仍回答，只是不存"吞掉了）。
- **键里 `with` 值的文本会撞**：`withArgText`（`:694-712`）只覆盖整数、布尔、浮点与字符串，其余（`f16`、`bf16`、数组、结构体）写成 `<类型名>`，同类型不同值得到同一个键，注释却说"两个不同类型的实参仍是两个事实"——同类型不同值没提。字符串实参未转义，`"a, b"` 与 `"a"`、`"b"` 两个实参写出同一文本。
- **永不失效**：同一进程里重新编译同一个模块（LSP、REPL、测试驱动重复运行）改了 `fact` 的体，旧的 `Definition` 继续被用，且它根住的函数永远不释放。
- **修**：键用声明身份（模块 + 名字 + 声明的内容哈希）加规范化的 `with` 值（用 `constasKeyOf` 那一套，规则实例的键已经这样做了，`rule_templates.cpp:60-66`）；描述挂在 `Context` 上，随模块重载失效。

### A4. 同一次运行里普通规则与泛型规则实例的编号相撞
`engineRules`（`module.cpp:664-718`）：普通规则编号 `next++`（从 1 起，`:712`），泛型规则的每个实例编号 `index + 1`（`:652`），两种编号放进同一个 `out`，`WritingUnion.rule` 与 `RewriteStep.ruleId` 是 `uint16_t`。注释（`:647-650`）自己承认"编号只在每个规则内部唯一"。
- **场景**：规则数组里有一条普通规则（编号 1）和一条泛型规则的第一个实例（编号 1）；`saturate` 的日志里 `rule = 1` 既指前者也指后者，`explain` 把一个并集记到错误的规则上。超过 65535 个实例时 `static_cast<uint16_t>` 静默回绕。
- **修**：编号由 `engineRules` 一处分配，覆盖普通规则与每个实例；`uint16_t` 改 `uint32_t`，或超限时报错。

### A5. 泛型规则不是"在匹配处实例化"，而是按程序的全部类型事先枚举，并静默截到 64 个
设计原则 9.11："泛型规则在匹配处实例化：被匹配的节点的类型解出类型参数，以类型元组为键缓存"。实现（`module.cpp:532-569`，`tuplesOf`）：取程序里**所有**值的类型（`programValueTypes`，`:490-513`），对单参数规则逐个、对多参数规则取乘积，条件通过的收进列表，到 `kCheckTupleCap = 64`（`:485`）就停，再为每个元组建一个实例。
- **场景 1**：程序里有 70 种不同的类型（几十个层的结构体、各种张量形状）。单类型参数的规则对第 65 种之后的类型**永远不被实例化**，规则对这些类型的值不触发，没有任何诊断（`why_not` 才会说）。
- **场景 2**：两个类型参数的规则，9 种类型就有 81 个元组，截断取的是"图的书写顺序里先出现的"，哪些元组被留下取决于图的顺序。
- **场景 3**：为从未被匹配的类型建实例（编译函数体、编码图），白费功夫，实例化失败也只是安静地丢掉。
- `check` 用另一套类型全集（`ruleInstanceTypes()` 加上给定程序的类型，`:3670-3704`）和另一套截断（洗牌后取 64，`:3706-3739`），所以 `check` 通过的元组与引擎实际实例化的元组可能完全不同。
- `programValueTypes` 的去重是对已有类型线性扫描（`:506-509`），O(节点数 × 类型数)。
- **修**：实例化放到匹配处：`matchFrom` 在 `Op` 模式遇到泛型规则时，用节点的类型绑定类型参数，查 `(规则, 元组, with 值)` 的缓存，缺则建；这样也不需要 64 的上限。`check` 与引擎共用同一个枚举函数。

### A6. `check` 的几处会让"过了"不代表什么
- **`std::shuffle` 不可移植**（`module.cpp:3710-3715`）：`std::mt19937_64` 是确定的，但 `std::shuffle` 的算法由标准库决定，libstdc++、libc++、MSVC 给出不同的排列。注释写"同一个种子取同样的元组"，只在同一个标准库里成立；金标跨平台会不同（红线 6）。**修**：自己写 Fisher-Yates，或用键式随机数（`random` 的 Philox）。
- **乘积被截成前缀**（`:3686-3689`）：`product` 只在 `product <= 4096` 时才继续乘，类型参数 ≥ 4 时 `product` 小于真正的乘积，后面的参数只取到全集的前几个类型，**不报告**（只有 `total > kCheckTupleCap` 才有一行说明）。
- **带条件的规则不比较两侧的值**（`:3345` 的 `!conditional`），只走引擎路径，而引擎路径里没满足条件的随机常量根本不触发；行里写的理由是"没有比较任何采样值"，但引擎路径其实比较了 `before` 与 `after`（`:3489-3559`），文字与代码不符。
- **一条坏规则让整个报告失败**：`checkKernel`（`:3807-3861`）循环里没有 `try`，`checkOneRule` 的 `RuleCompileError`、取样器的 `RuntimeFault`、引擎的 `std::logic_error` 都会穿出去，其余规则的行丢掉。**修**：每条规则一个 `try`，失败写成该规则的一行（`sampled`，理由是异常文字）。
- **`proved` 永远不产生**（`kVerdictProved` 被导入但没用，`:90`）。原则 9.11 说判决有三种，实际只有两种；至少在文档里写明。

### A7. 异常类型与可恢复性
- `rewriteKernel`/`runKernel`（`module.cpp:955-962`、`1028-1034`）把**任何** `std::invalid_argument` 报告成 `RuleTwoWay`。引擎外的代码（`std::stoi`、容器、任何库）抛出的 `invalid_argument` 都会被改写成"规则是双向的"，原因被换掉（注释 `:937-941` 说只转本模块自己的错误，实现没有这样）。**修**：引擎抛自己的类型 `RuleTwoWayError`。
- 引擎的不变量用 `std::logic_error`：层数超过 4（`writings.cpp:290`）、`assertEqualDeferred` 的类型/区域/权限（`:805`）、抽取"没有不读自己的写法"（`:1091`、`:1102`、`:1107`）、`Compute` 项读到非常量（`:1831`）、代价模型给出非正数（`:959-961`）。其中**代价模型**来自用户（`extract` 的代价是 Camel 函数），返回 0、负数或 NaN 就让整个遍以 C++ 异常结束；层数上限则是一个合法程序能碰到的实现限制（测量过，见 F1）。这些在 `saturateKernel`/`extractKernel` 里没有 `catch`。**修**：用户可触发的变成 `T!` 的错误值，真正的不变量用 `ASSERT`。
- **条件抛异常**：`whyNot` 把 `rule.when` 包在 `try` 里（`:2352-2380`），`rewrite` 与 `saturate` 没有（`:2078`、`:2462`）。条件是 Camel 代码，可能故障（下标、除零）、可能抛 `RuleCompileError`（`module.cpp:307`、`:339-352`）；穿出去时 `greedy_` 仍是 `true`（`:1941` 设置、`:2185` 才复位，不是 RAII）、`matchPerms_` 停在上一个值、`pending_` 留着。只有 `FactQueryNoValue` 被转成"条件为假"（`module.cpp:395-401`）。**修**：条件异常统一转成"这条规则在此不适用"并计数/记录（与事实查询找不到一致），或者让遍报一条带规则名的诊断；`greedy_` 用 RAII。

### A8. 常量的比较用指针相等，别处用规范 id
`matchFrom` 的常量模式（`writings.cpp:1458`）、`const` 变量的重复（`:1443`）、乘集合的 `reads`（`:1657`）、`whyNot` 的 `walk`（`:2253`）都写 `found->second != p.type`，比较的是 `Type *` 的地址；同一个文件的 `NodeKey` 和 `assertEqualDeferred` 却特意用规范 id（`:402-408`，注释说"在不同地方建出来的类型"是常态）。对标量类型是单例，碰不上；对数组/结构体/命名类型的常量会静默匹配不上，规则在这些常量上不触发，没有诊断。
- `fact.cpp:318` 的 `value == pattern.value` 比较原始槽，而 `writings.cpp:169-182` 的 `canonicalConstant` 正是为 `f32`/`i32`/`bool` 的高位字节不是值的一部分而写的（注释：Debug 与 Release 里同一个 `f32` 常量会比较不相等，规则只在一个构建里触发）。事实的模式没有用它。
- **修**：一个 `sameConstant(a, b)`，规范 id + `canonicalConstant`，三处共用。

### A9. 右侧的共享在 `rewrite` 里丢失，同一子表达式被算两遍
`SideReader::term`（`rules.cpp:100-148`）把右侧图展成树，没有按 `ref` 记忆；`rewrite` 以 `greedy_ = true` 运行，`lookupOrAdd` 在 greedy 时不做合并（`writings.cpp:817`）。
- **场景**：规则 `x ^ 4 => let s = x * x; s * s`（右侧共享 `s`）。`saturate` 里 `lookupOrAdd` 把两份 `x * x` 合成一个；`rewrite` 里建出两份，`x * x` 被算两次。规则的作者以为省了乘法，结果多了一个。原则 9.11 把"会不会多算子表达式"归给代价函数，而 `rewrite` 没有代价函数。
- **修**：`rewrite` 在**一次应用内**也按键合并（同一个 `build` 里的相同子项），不影响它不跨应用合并这一点；或 `build` 按模式节点身份记忆。

### A10. 只试第一个匹配，被 `dropsOrdered` 拒绝后不再试别的
`rewrite` 里（`writings.cpp:2073-2097`）`chosen` 是第一个条件为真的匹配；`dropsOrdered` 拒绝它之后 `continue` 去试**下一条规则**，同一条规则的其他匹配（可交换算子的另一个顺序、类的其他成员）不会被试。不是不安全，是漏改写；`saturate` 则收集全部。乘集合路径（`:1770`）遇到第一个完整放置就停，也与普通路径不一致。**修**：把 `dropsOrdered` 的检查放进 `emit` 里（`saturate` 已经这样做，`:2465`），让搜索继续。

### A11. 抽取转移等待边时，同步边与控制边不对称
`writings.cpp:1085-1098` 保留了类的每个成员的控制输入**和同步输入**所等的节点；`:1132-1145` 把其他成员的**控制输入**挪到被选中的写法上，同步输入没有挪。若被选中的写法与别的成员在同步链上的位置不同，保留下来的等待者与被选写法之间没有边了。现实中参与类的节点（`Data`/`Cast`/`Accs`/纯 `Oper`）很少带同步边，所以平时看不到；要么删掉 `:1087` 对 `syncInputsOf` 的处理（承认不会出现，并 `ASSERT`），要么在 `:1137` 一并挪。

### A12. 抽取：代价按树算、不看共享，数据结构开销大，且不带数值策略
- **树代价**（`choiceByCost`，`:984-993`）：一个写法的代价是自己加各输入类的代价之和，被多处使用的子表达式每被用一次就重复计一次，选择会偏向"把共享子表达式展开成各自重算"的写法（节点更多、运算更多，但树代价更低或相等）。9.11 说收益归代价函数，代价函数接口 `nodeCost(context, node)` 看不见共享。**修**：按 DAG 的代价（每个被选中的节点算一次，用贪心或局部搜索），或把共享度放进 `CostContext`。
- **代价模型靠 URI 子串**（`WeightedModel`，`:892-905`）：`has("sub")` 命中 `:op/slice_sub`（下标节点，代价 1.0，与一次减法同价），`has("mod")` 命中任何含 `mod` 的 URI，`has("add")` 命中含 `add` 的……应读算子声明的事实（`OperatorSemantics`），而不是 URI 文本。
- **复杂度**：`choiceByCost` 每轮对每个节点 `inputsOf` 分配一个 `std::vector`（`:969-976`），最多 `slots + 1` 轮，O(轮数 × 节点 × 输入) 加每次一次堆分配；`extract` 里再来一遍（`:1026-1033`）。用输入的 span 直接遍历，按拓扑序一遍扫完（类内无环时一遍就够，有环时才迭代）。
- **没有数值策略**：`extractWritings` 里 `DraftWritings writings(draft)`（`:1205`）用默认策略；`saturate` 核按 `strict` 建（`module.cpp:1511-1514`）。流水线先宽松饱和、后以 `safe_fp=true` 抽取时，抽取读到的是默认策略的层，`safe_fp` 到不了抽取（`std::opt::extract` 只有 `cost` 参数）。若这是有意的，文档写明；否则抽取要带策略。

### A13. 层数上限与匹配时的权限相等
- `kMaxLayers = 4` 越界抛 `std::logic_error`（`writings.cpp:290-293`）。不同的数值权限组合一旦超过四种（`reassociate`、`contract`、`refine_accuracy`、`change_precision` 组合并不少）就让整个遍异常结束。上面的注释（`:275-279`）说明这个上限曾被一个 bug 触发过，它是实现限制而不是不变量。
- 匹配要求节点的权限**等于** `matchPerms_`（`:1482`、`:1545`、`:1667`），而 `needs` 的检查是子集（`rule.needs & ~context`）。权限更多的邻居节点不被匹配，是保守的；但 `whyNot` 的说法是"跨权限类"，与两者不对称，应在文档里说一次。

---

## B. 终止与状态

- **`rewrite` 核不报告是否到不动点**（`module.cpp:942-962`）：固定 16 轮（`:952`），`fixpoint` 被丢弃；规则 `a => b` 与 `b => a` 同时存在时程序停在第 16 轮的状态，调用者不知道。`run` 核（`:977`）会报，两个核的行为不一致。**修**：`rewrite` 未到不动点时返回错误值（或和 `run` 合并成一个）。
- `saturate` 超预算时 `return stats`（`writings.cpp:2481-2483`）：没有 `rebuild()`、没有把 `saturated` 置假之外的任何标记，`applySeconds` 也不记。调用者（`saturateKernel`，`module.cpp:1516`）忽略 `stats`，所以**预算用完的饱和和饱和完成的结果在程序层面不可区分**。**修**：`saturate` 核把 `saturated`/预算耗尽写进返回值或给一条警告。
- 预算只在**每个匹配之前**检查（`:2481`），一次 `build` 可以建任意多个节点（RHS 很大、`remainder` 链很长），`newWritings` 会超出预算很多。
- 抽取在类里没有无环的写法时抛 `logic_error`（`:1091-1108`），而不是报告哪条规则造出了这个环。
- `RewriteMemo`（`rewrite.cpp:359-369`）按**遍名**记"这张图这个遍没改动过"，键里没有遍的参数（`safe_fp`、`neutral_only`、规则集），也不是内容的完整比较，只是 64 位哈希。同一个 memo 里用不同参数再运行同名遍，上次"没改动"的图会被跳过。遍名是否总带参数，待验证；`checkScreens` 只在测试里开。

---

## C. 性能

- `pureComputation`（`writings.cpp:1864-1888`）的 `seen` 是 `std::vector`，每个节点 `std::find` 线性查，O(n²)；`dropsOrdered` 对每个未被右侧读到的变量调用它，`saturate` 每轮对每个匹配调用，`rewrite` 对每次选中调用。**修**：`std::vector<uint8_t>`（按节点号）。
- `reaches`（`rewrite.cpp:86-104`）每次分配 `nodeSlotCount` 大小的 `vector<bool>`。
- **事实查询**：`factKernel`（`fact.cpp:802-824`）每次查询新建一个 `Frame` 和一个 `NodeVMSchedPass`，draft 模式下什么也不缓存（`:826` 的 `if (!fromDraft)` 才存）。一轮重写里每个匹配的条件里的每次查询都从头对整张图求值。**修**：draft 上的事实缓存按"draft 版本号 + (类型, 节点)"，图变化时失效；VM 复用。
- **条件调用**：`callWhen`（`module.cpp:251-404`）每个匹配为每个参数建一个 `Handle` 和一个 `RootHandle`，加几个 `vector`。
- `matchFrom` 在每个 `Op` 模式处复制 `candidates` 向量（`:1466-1475`）。
- 类的成员与 `NodeKey`：每个节点索引一次 `std::string payload` 加两个 `vector`，哈希表键是整个 `NodeKey`（含 `std::string`），大图上内存和分配多。

---

## D. 重复的逻辑

| 重复的东西 | 位置 | 做法 |
|---|---|---|
| `carriesOrdering` 两份，各自的注释都说"这是同一条规则" | `writings.cpp:459-486`、`rewrite.cpp:111-131`（`Oper` 的判断一个用 `mayBeRecomputed`，一个用 `isPureOper`，对长度不足 `GCOperBody` 的负载处理也不一样） | 一份，放 `camel/runtime`，遍侧引用 |
| `declaredCommutative` 两份 | `writings.cpp:526-548`、`fact.cpp:427-454` | 一份；`declaredAssociative`（`:555-577`）是前者复制后加一个字段，可合并成一个"取这个类型上这个算子的 `BinaryLaw`"的函数 |
| 三个匹配器 | `matchFrom`（`writings.cpp:1418-1791`）、`whyNot` 的 `walk`（`:2263-2380`）、`fact.cpp` 的 `matchNode`（`:292-340`，且 `sameFamily` 按 URI 末位下划线前的文本判定"同族"，`:257-273`，与规则按精确算子 id 匹配的口径不同，`:tensor/sum_axis` 与 `:tensor/sum_all` 会被当成同族） | 一个匹配器加不同的"回调"；至少 `whyNot` 应复用 `matchFrom` 的失败位置，而不是另写一遍 |
| `whyNot` 的 `walk` 只看节点自己的输入，不看类的其他成员 | `:2263` 起 | 引擎能匹配、`whyNot` 说"不匹配"（饱和后的图上） |
| `countVars` lambda 三份 | `writings.cpp:1970`、`2233`、`2416` | 放进 `Rule`（编译规则时算一次） |
| `matchPerms_` 是成员变量，当作匹配的隐含参数，三处设置 | `writings.cpp:2049`、`2230`、`2456` | 作为参数传 |
| `WhenCall` 五处构造 | `module.cpp:638`、`704`、`1458`、`3768`、`3849` | 一个工厂 |
| 默认轮数 16 在三处（核两处、`.cml` 一处） | `module.cpp:952`、`979` | 一处 |
| 布尔→"at (...)"的文字拼装在 `checkOneRule` 里四份 | `module.cpp:3382`、`3419`、`3499`、`3539` | 一个函数 |

---

## E. 结构与运行期类型分派

- `modules/rewrite/module.cpp` 4179 行，混着：规则对象、泛型实例引擎、`Proof`/`CheckReport` 两个类型、`explain`/`why_not`/`run`/`saturate`/`extract` 的核、`check` 的取样器（`Sampler`，`:2360-2720`）、参考求值器（`RefEvaluator`，`:1955-2225`）、各种 dtype 工具。按功能拆成 `rule_object.cpp`、`instances.cpp`、`proof.cpp`（已有一部分）、`check.cpp`、`sampler.cpp`、`reference.cpp`。
- 过长的函数：`matchFrom` 375 行（其中乘集合的 lambda 约 240 行）、`checkOneRule` 约 390 行、`DraftWritings::rewrite` 约 250 行（含四个 lambda 与整段日志）、`whyNotKernel` 约 215 行、`DraftWritings::extract` 约 170 行、`whyNot` 约 200 行。
- **运行期类型分派**：`dataOfRuleValue`（`module.cpp:125-150`）按 `TypeCode` 转 `with` 值，未知类型返回 `nullptr` 然后被 `push_back` 进 `withValues`（`:189-191`）——注释说调用处已经拒绝，但没有断言，后面的 `withArgumentsKey` 会拿 `nullptr` 当值；`tensorElementType`/`elementBytesOf`/`elementOfBytes`（`:1569-1650`）三个按 dtype 的 `switch`；`RefEvaluator::operate` 与 `Sampler` 里按 URI 字符串/名字的判断共 34 处（`name == "…"`、`uri.find`）。参考求值器是算子语义的第二份实现，算子表的数值事实（`NumericSemantics`）应是它的来源。
- `isRuleSide` 按**图名后缀**判断一张图是不是规则的一侧（`module.cpp:438-443`，`__lhs`/`__rhs`/`__when`）：用户的函数叫 `foo__lhs` 就被排除在重写之外，静默。
- 规则实例表 `GenericRuleEntry.owner` 是 `void *` 存的 `Builder *`（`rule_templates.cpp:138`、`:156`）。`Builder` 现在由 `context->graphBuildSession()` 持有（`program.cpp:200-206`），生命周期与上下文一致，所以不悬挂；但没有任何类型保护，`owner == nullptr` 的分支（"the builder that declared it is gone"）永远不会进入。用 `Builder *` 或弱引用。

---

## F. 注释与文档

- **叙述历史**：`writings.cpp:275-279`（"measured 2026-10-24: … failed with layers `{515, 3, 515, 3}`"）；`module.cpp:497-501`（"measured 2026-10-24: `float_combine__when<var float>` passes …"）；`:1296-1308` 讲一次崩溃的修复经过（提交号 `6e0eb52e4`、"SIGSEGV with nameId 0xf3900000"、日期）；`fact.cpp:12-13`、`:366-367` 的"the first version has no widening"；`fact.h` 的"is `fact` now"；`rule_templates.cpp` 多处"(the rule on …)"、`builder_generics.cpp:553-` 同。这些应只写现在的规则；历史在提交说明里。
- **残缺或乱码的注释**：`module.cpp:677-679` "(the ruleeneric-rule-instantiation assumes one type per value, and asking …"、`:690` 一句话从中间断开、`:1374` 缩进错位；`fact.cpp:33-35` 英文注释里夹着一整句带引号的中文。
- **注释与代码不符**：`module.cpp:881-883`（"each graph advancing in the same round"）而 `applyRewrite` 是逐图串行；`:3602-3608` 见 A6。
- **设计文档内部不一致**：原则 9.7 写"丢掉的部分必须纯且不会出故障"，`v2.cn.md:90` 同，但同文件 `:140` 仍写"丢弃的部分必须是纯的"；原则 9.6 又允许化简遍删除"结果没被用到的会出故障的纯计算"。引擎怎么办要先定：规则应用时不丢故障（A1），抽取/死代码删除是否允许删故障，要一句明确的话。
- `v2.cn.md:130-132` 描述了 `widen`/`narrow` 的行为，实现里没有（A2）；文档应标明哪些是目标、哪些已实现——或者让实现跟上。

---

## G. 缺的测试

`test/cases/modules/rules`（116 个文件）里我没有找到：
1. **故障不被丢弃**：`(a / b) * 0`、`xs[i] - xs[i]`、`int(x) * 0` 等在 `rewrite`/`saturate`/`extract` 下故障仍保留（A1）。
2. **两个模块各声明同名 `fact`**，各自用自己的定义；`with` 值是 `bf16`/数组时两个事实分开（A3）。
3. **普通规则与泛型规则混在一个数组里的 `explain`**，编号不串（A4）。
4. **超过 64 种类型的程序**上单参数泛型规则仍对每种类型触发（A5）；两参数泛型规则在大类型集上不因图的书写顺序而不同。
5. **`check` 的确定性**：同一个种子在不同标准库里得到相同的元组（A6）；一条坏规则只让自己的行失败。
6. **互相抵消的规则（`a => b`、`b => a`）** 在 `rewrite` 下报告未到不动点（B）。
7. **预算耗尽的 `saturate`** 在结果里可见（B）。
8. **条件抛异常**（条件里下标越界、除零）：规则不适用而不是遍异常，`greedy_` 复位（A7）。
9. **环上的事实**与 `widen`/`narrow`（A2，需要先确定环是否存在）。
10. **`extract` 带共享子表达式**：共享的写法不比重算的差（A12）。
11. **常量的规范比较**：`f32` 常量高位字节不同仍匹配（A8，Debug/Release 同一结果）。
12. **用户代价函数**返回 0/NaN 给出错误值而不是 `logic_error`（A7）。

---

## 建议的处理顺序

1. A1（故障谓词）与它的负例测试：最小的改动，补上设计原则里已写明、引擎里缺的那条检查。
2. A3、A4（键与编号）：几行改动，防止静默张冠李戴。
3. A7（异常类型与条件异常）、B（终止的可见性）：让失败成为可见的值。
4. A2：先回答"数据边上有没有环"，再决定实现还是删除。
5. A5（匹配处实例化）：较大的重构，同时解决 `check` 与引擎的口径分歧。
6. A8–A13、C、D、E、F 随后清理；E 的拆文件放在最后，在测试把行为钉住之后。
