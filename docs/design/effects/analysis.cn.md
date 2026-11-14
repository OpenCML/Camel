# R3 的设计：唯一的效应分析

任务书第九节 R3 与第十一节第 3 路的实现依据。语义以设计原则为准（3.1 效应不静默消失、3.3 读是依赖与读写方式、3.4 同步块当值、3.5 别名、3.6 `map`/`filter`、3.9 故障沿依赖传播、9.7）；结构以目标架构为准（`docs/design/architecture/README.cn.md` 2.3 唯一的事实、2.4 唯一的算子定义）。R3 是这两条路线的样板：它做成什么样，后面就照什么样铺开。

## 1. 现状：同一件事的七种算法

| 地方 | 算什么 | 与别处的关系 |
|---|---|---|
| `src/compile/gir/builder_resources.cpp`（`reachOfOper`、`settleEffectfulGraphs`） | 建图时在草稿上测每个函数体的资源 | 注释自称与 `runtime/resources.cpp` 做同样的事，只共用 `conservativeResources`，遍历另写一遍 |
| `src/runtime/resources.cpp`（`Resolver`、`FootprintWalk`） | 冻结图上每个节点、每个函数的资源足迹，最小不动点 | 有自己的缓存（按闭包版本） |
| `src/runtime/effects.cpp`（`effectSummaryOf`） | 图级效应摘要：对外、写程序状态、可能出故障、可能不终止 | 节点的"可能出故障"直接读算子特征，不用 `nodeMayFault` |
| `src/runtime/graph.cpp`（`nodeMayFault`、`nodeIsFreelySchedulable`，以及"是不是语句"） | 节点级判定 | 与 `effects.cpp` 的规则不同，同一个节点可能得出不同答案 |
| `src/execute/pass.cpp`（`nodeHasEffect`） | 遍的边界检查用 | 注释写明抄自 tiles |
| `src/passes/trans/cml/cml.cpp`（`isEffectful`、`isPureOper`） | 打印器判断语句能不能省 | 自己的不动点，外加按 URI 前缀的特例 |
| `modules/tiles/src/{graph_regions,census,region_outline}.cpp` | 能不能放上设备 | 同一个表达式抄了三处 |

另外，"是否纯"由 `registerBuiltinOperatorTraits` 按名字前缀推断，"会不会出故障"在 `kNeverFaults` 与 `builtin_semantics.cpp` 各说一遍。

## 2. 算子定义里的效应（目标架构 2.4 的第一步）

`sem::OperatorSemantics` 是算子效应的唯一来源：
- **访问列表**：每项是（资源，方式）。方式只有读与写；对外部资源也分读写，由算子声明：只观察的是读（读文件、读环境变量），改变外界或读的同时改变外界的是写（打印、写文件、读标准输入、读时钟）（原则 3.3，负责人 2026-11-14）。
- **故障**：会在什么条件下出什么故障；没写的按"可能出故障"。
- 由此**推出**、不再另外声明的：
  - 纯：访问列表为空，或只有对变量的读；
  - 对外：有外部资源；
  - 可能出故障：故障表非空。
- 按名字前缀推断纯度的规则删掉；`kNeverFaults` 并进故障表。没有声明的算子按"写全局资源、可能出故障"处理（原则 3.3 现有规则）。
- 标准库 `.cml` 声明里的 `uses`（读写方式写法见原则 3.3）是 Camel 一侧的写法，`scripts/stdlib-reference.mjs --verify-ops` 改为逐项比对访问列表。

## 3. 唯一的分析

### 3.1 接口

放在 `graph` 层（现在的 `src/runtime`，新文件 `effect_analysis.{h,cpp}`）。输入是一张图的**读接口**：`graph_read.h` 已经让冻结图与草稿回答同样的问题，建图器与提交都经它调用。

```
struct Access { ResourceRef resource; Mode mode; };      // Mode = Read | Write
struct NodeEffect {
    SmallVec<Access> accesses;   // 已做效应遮蔽（3.3）
    bool external;               // 有外部资源上的访问
    bool mayFault;               // 唯一的"可能出故障"
    bool freelySchedulable;      // 纯且不会出故障（原则 3.4 那个谓词）
    bool regionRoot;             // 对外部资源的写，或对活得比函数长的变量的最终写入（原则 3.1）
};
struct Footprint {               // 一个函数（图）对调用者的样子
    SmallVec<Access> accesses;   // 已遮蔽
    bool external, mayFault, mayNotTerminate;
};
NodeEffect effectOf(const GraphReadFace&, NodeRef);
Footprint  footprintOf(const GraphReadFace&);          // 调用图上的最小不动点
```

- 节点级与图级是**同一条规则**的两个粒度：`footprintOf` 是对图里所有节点的 `effectOf` 求并再遮蔽，调用节点的 `effectOf` 取被调者的 `footprintOf`。
- 冻结图上的结果作为事实（`core.effects`、`core.resources`，已有的种类）进事实存储，按闭包版本失效；资源足迹缓存并进事实存储，不再单独存在。草稿上不缓存，每次现算。

### 3.2 调用与函数值

- 直接调用取被调者的足迹。
- 经资源泛型形参的调用，按实例化特化（原则 3.3），特化键带读写方式。
- 闭包的足迹包括它捕获物（被捕获的变量与被捕获的值，一层层往里取）的类型里提到的全部变量：握着记为读，写只经调用进入（自己写，或调用了会写的捕获物）（原则 3.3"握着什么，类型就提到什么"）。造闭包的节点在提交时检查这一包含关系。验收：`let h = () => { let h2 = () => { c }; let k = () => sync { h2; 0 }; k }` 里返回的 `k` 的类型提到 `c`；`k` 仍能传给 `pure` 形参。

### 3.3 效应遮蔽

函数自己的局部变量，若不逃出函数，对它的读写不进这个函数的足迹（原则 3.3）。判定：
- 一个 `:var/new` 的单元**逃出**，当且仅当捕获它的闭包被用于直接调用以外的用途（作为值流出、存进数据结构、交给被调者存起来），或它作为 `var` 实参交给了不在本图内联的调用、而被调者把它存起来。
- 判定是保守的：拿不准就算逃出。
- 例：`func total(x: int): int { var acc = 0; let add = (k: int): int => sync { acc = acc + k; acc }; let a = add(x); let b = add(x); return b }` 的足迹为空，是纯的；把 `add` 返回出去，`acc` 就逃出了。

### 3.4 区域的根（交给第 4 路）

`regionRoot` 标出原则 3.1 的两类：对外部资源的写（外部的读与变量的读一样按需执行），以及对活得比当前函数长的变量的最终写入。图上**不加边**表示"区域用到它们"；执行语义层（目标架构 2.2，第 4 路）把它们和出口一起当作区域的根，算出哪些节点被用到。R3 只提供这个判定，不改调度器。

## 4. 建图器

- **删掉 `builder_resources.cpp` 里的那套遍历**，改调 `effectOf`/`footprintOf`（草稿上经读接口）。`settleEffectfulGraphs` 的不动点改用 `footprintOf` 的不动点，不再另写。
- **顺序边按冲突生成**：同一资源上至少一方是写才连（原则 3.3）；读与读不连；读之后的写连一条（现有，保持）。生成处只问分析，不自己判断纯与不纯。
- **同步链**照旧只连外部访问、对变量的写、可能出故障的计算；另加点名语句（`a;`）：它把那个名字绑定的值接上同步链（原则 3.4），哪怕那个值是纯的。
- **`GATE`** 只用于钉住可能出故障的计算（R1）。
- `EffectNotExecuted`、`IgnoredSideEffect` 两条诊断删掉，注册表里一并删；`builder_diagnostics.cpp` 里用到它们的地方删掉。

## 4.1 提交时的两项检查

用同一个分析，在提交时检查（红线第 1、6 条的强制机制，ECOOP 提案的 R1、R2）：
- **效应包含**：每个调用处，被调的函数值的足迹（按实例化）不超出调用处的类型所声明的；超出则拒绝，诊断点出调用节点、多出来的访问。例：把纯函数值换成会打印的函数值的编辑。
- **顺序完整**：同一资源上的每对冲突访问（至少一方是写）都有先后（顺序边、同步边、数据依赖之一）；缺了则拒绝，诊断点出这两个节点与资源。例：把两个可变变量合并成一个后，两次写之间没有先后。
两项都只读分析的结果，不另写判断。诊断文本收进 `diagnostics/registry.tsv`，ECOOP 论文会照印，措辞定了就不随手改。

## 5. 语言层的改动

1. **`uses` 只列外部资源与句柄形参**：`uses fs` 读、`uses var fs` 写；在 `uses` 里写变量（`var` 形参、捕获的、模块顶层的、局部的）报一条新诊断，提示可变性已在声明处写明、由编译器推出；类型打印与诊断列出推出的变量及其读写方式（负责人 2026-11-14）。
2. **`pure`** 按新定义检查：没有写入（不写活得比调用长的变量、不写外部资源，遮蔽后）；读什么都可以。`UsesPromiseViolated` 的提示点出第一个写。
3. **`map`、`filter` 的声明改为 `pure`**；新增 `para::unordered_map`；传入带效应的函数时，诊断要说出它写了什么（现在只报"找不到匹配的函数"，类型里也不显示 `uses`）。`stdlib/opt.cml` 里在 `map` 中改事务的那处改成 `foreach`。
4. **`console`**：标准库里 `stdout`、`stderr`、`stdin` 合成一个资源 `console`；写标准错误、读标准输入前刷出标准输出（在运行时的 I/O 里做，一处）。不留旧名字（不考虑兼容）。
5. **同步块当值**：`let r = sync { ... }` 的语法；点名语句。
6. **规则引擎的 `protect_effects` 删掉**：外部访问与最终写入由区域用到，丢掉数据用处不会让它们消失（原则 9.7）。`protect_faults` 保留。

## 6. 改用分析、删掉旧的

| 地方 | 改成 |
|---|---|
| `src/runtime/effects.cpp`、`resources.cpp` | 由新分析取代；对外接口（`effectSummaryOf`、`resourcesOf`）若有调用者，改调新分析后删除 |
| `graph.cpp` 的 `nodeMayFault`、`nodeIsFreelySchedulable`、"是不是语句" | 改为读 `NodeEffect` 的字段，三处合一 |
| `pass.cpp` 的 `nodeHasEffect` 与边界检查 | 读分析；比较前后足迹时比访问列表 |
| `cml.cpp` 的 `isEffectful`、`isPureOper` | 读分析；URI 前缀特例删掉 |
| tiles 三处、`builder_diagnostics.cpp`、`modules/transforms/jvp.cpp` 的 `hasExternalEffect` | 读分析 |

目标架构第 7 节"效应只经事实层判定"的检查脚本，在本路合入时对这些文件转为硬门槛。

## 7. 提交的先后

每一步都能单独合入，合入时只留新的一套。

1. **算子定义**（第 2 节）：加访问列表与读写方式，纯度与故障改为推出。行为不变；`camel inspect --ops` 显示读写方式；`--verify-ops` 比对通过。
2. **分析本身**（第 3 节）：新写 `effect_analysis`。合入前在本地把全部用例的每个节点、每张图，用新旧两套各算一遍，逐项比对；差异逐条归为"旧的错了"或"新的错了"，列进合并说明。合入的只有新的一套，旧的在同一次合并里删除（第 6 节）。
3. **建图器改用分析**（第 4 节），顺序边按冲突生成。
4. **语言层**（第 5 节），连同测试迁移。期望输出变了的逐条列出交决策者，不许为了通过而改期望。
5. **把 `regionRoot` 与 `mayFault` 交给第 4 路**，由它做区域的根与故障传播。

## 8. 验收

- **一条规则**：同一个节点上，`effectOf` 的 `mayFault` 与图级 `footprintOf` 的结论一致（随机程序上的性质测试）；仓库里 `isPure(...) || hasExternalEffect(...)` 只在分析里出现。
- **遮蔽**：上面的 `total` 能传给 `pure` 形参；把闭包返回出去的变体不能。
- **读写**：`times(x) = x * scale`（只读模块顶层变量）能传给 `map`；`acc = acc * 10 + x` 的元素函数被拒，诊断点出 `acc`。
- **冲突**：两个只读同一变量的调用之间没有顺序边；读与之后的写之间有。
- **外部的写照常执行**：非同步函数体里的裸 `println(x)`、臂里的打印、循环体里的打印都执行（第 4 路合入区域的根之后）；不再有 `EffectNotExecuted`、`IgnoredSideEffect`。
- **状态**：`func f(var p: int): int { p = 3; return 0 }` 调用后调用者读到 3，没有警告。
- **控制台**：`print("name? "); let n = readline()` 先出提示；`println(1); eprintln(2); println(3)` 在 `2>&1` 下依次是 1、2、3。
- **同步块**：`let q = a / b; sync { q; println("checked") }` 在 `b` 为零时不打印。
- 四个调度器同一组输出（第 4 路的差分测试）。
