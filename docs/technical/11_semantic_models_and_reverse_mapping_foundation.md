# AST / GCT / GIR 语义模型与逆映射基础

本文档是“相对精确逆映射”重构的基础文档，目标不是马上把所有源码区间都切到最细，而是先把 `AST -> GCT -> GIR` 的节点语义、层间 lowering 关系、信息损失点、基础缺口与后续路线一次讲清楚。

与 `10_source_mapping_and_debug_locations.md` 的关系如下：

- `10` 说明统一 `SourceContext + OriginTable + DebugMap` 如何把源码位置贯通到 debugger / runtime。
- 本文档说明“每类节点到底表达什么语义、lowering 时怎么拆、哪些点天然丢信息、现阶段该把哪些语义部件沉淀为 side table”。

## 1. 总体原则

当前工程采用的不是“字符级绝对逆映射”，而是“两层语义定位”：

1. `mainOrigin` 继续提供稳定主责任区间，保证 diagnostics / debugger / runtime 永远能回到一段明确源码。
2. `semanticParts + mergedInputs` 补充 operator、callee、argument、branchCondition、branchTarget、valueProducer 等语义部件，逐步把相对精确逆映射做细。

这样做的原因很直接：

- `AST -> GCT -> GIR` 本身存在大量降糖、拆分、聚合和 synthetic 包装。
- 若直接在 GIR 上强行重建“唯一精确字符区间”，会把 IR 与 debugger 需求绑死。
- 先沉淀统一的语义部件命名层，后续 debugger、runtime、coverage、profile、源码级 stepping 都能复用。

## 2. AST 语义模型

### 2.1 AST 的结构特点

AST 不是“每个节点都有完备字段成员”的面向对象树，而是：

- `Node(load) + ordered children`
- `RepeatedLoad / OptionalLoad` 表达基数与槽位
- 具体语义一部分放在 `Load` 字段中，一部分靠 child 顺序约定

因此 AST 文档化时必须同时写：

- 节点类别
- `load` 上的元信息
- child 槽位顺序
- 哪些槽位能作为稳定语义锚点

### 2.2 AST 主要节点类别

| 类别 | 典型节点 | 主要职责 |
|------|------|------|
| 根/容器 | `ModuleLoad` `RepeatedLoad` `OptionalLoad` | 组织模块、列表、可选槽位 |
| 声明 | `FuncDeclLoad` `DataDeclLoad` `TypeDeclLoad` `NameDeclLoad` | 表达语言级声明与绑定 |
| 控制 | `IfExprLoad` `MatchExprLoad` `MatchCaseLoad` | 表达条件与模式分支 |
| 表达式 | `UnaryExprLoad` `BinaryExprLoad` `ReservedExprLoad` `RefDataLoad` `LiteralLoad` | 表达运算、调用、访问、字面量、引用 |
| 复合值 | `ArrayDataLoad` `StructDataLoad` `TupleDataLoad` `FuncDataLoad` | 表达聚合值与函数值 |
| 类型 | `FuncTypeLoad` `TypeExprLoad` `SpecTypeLoad` `ArrayTypeLoad` `TupleTypeLoad` `StructTypeLoad` `RefTypeLoad` | 表达类型构造和类型运算 |

### 2.3 AST 的稳定语义锚点

本轮实现给 AST 新增了“语义锚点 side table”，锚点继续挂在 `SourceContext` 中，不污染 AST 主结构。

当前重点锚点：

- `operator`
- `keyword`
- `callee`
- `receiver`
- `argument(slot=n)`
- `argList`
- `branchCondition`
- `branchTarget`
- `caseValue`
- `caseBody`
- `memberName`
- `funcName`
- `returnType`
- `parameter`
- `genericParameter`
- `genericArgument`
- `bindingName`

当前已经重点接入的 AST builder 入口：

- 各类二元/一元表达式
- `call / bind / access / link / comp`
- `if / match / case`
- `funcDecl / funcData / dataDecl`

### 2.4 AST 关键节点语义约定

#### 二元/一元表达式

- `BinaryExpr(op)` 的 child 顺序是 `lhs, rhs`；`Index` 则是 `lhs, indices*`
- `UnaryExpr(op)` 的 child 顺序是 `operand`
- 主区间仍由节点自身 origin 承担
- `operator` 锚点单独指向实际运算符 token

#### 调用 / 绑定 / 访问

- 普通调用：`ReservedExpr(Call) := callee, args*, kwargs*`
- 泛型绑定：`ReservedExpr(Bind) := callee, args*, kwargs*`
- 成员访问：`ReservedExpr(Access) := target, memberRef`
- link 调用：`a -> f` 在 AST 中被改写成 `Call(rhs=f, args=[lhs])`
- comp：`a .. b` 被建模为保留运算 `Comp(lhs, rhs)`

这意味着：

- `callee`、`receiver`、`argument` 必须独立命名，不能只靠 child index 暗含。
- link / comp 这类语法糖虽然 lower 到调用风格，但源码展示仍应突出原始 operator。

#### 控制流

- `IfExpr := cond, thenBlock, elseBlock?`
- `MatchExpr := scrutinee-ref, case*`
- `MatchCase := pattern, body`

这类节点的用户语义不是“一个整体值”这么简单，而至少包含：

- 条件或 scrutinee
- 每个分支体入口
- 每个 `case` 的值模式
- 分支结果聚合点

#### 声明

- `FuncDecl` 是具名声明壳，`FuncData` 是函数值本体
- `DataDecl` 的绑定名保存在 `load.refs()`，不是 child
- `FuncType` 的关键槽位是：with params、normal params、return type

所以“绑定名/函数名/返回类型”这种语义部件不能只靠 child 遍历得到，必须显式命名。

## 3. GCT 语义模型

### 3.1 GCT 的定位

GCT 是 AST 到 GIR 之间的过渡层，仍然是树，但节点已经转向“图构造语义”：

- 不是单纯保留语法结构
- 也不是最终执行图
- 而是把“要构造什么图节点/边/子图”先树状表达出来

### 3.2 GCT 主要节点职责

| 节点 | 职责 |
|------|------|
| `EXEC` | 语句块或 synthetic 执行包装 |
| `DECL` | 声明视图 |
| `FUNC` | 函数图构造入口 |
| `TYPE` | 类型与签名承载体 |
| `LINK` | 调用/算子调用/索引调用的统一骨架 |
| `WITH` | 泛型/with 参数绑定 |
| `DREF` | 引用函数、算子、变量、符号 |
| `NREF` | 绑定名字到值 |
| `DATA` | 静态字面量或聚合常量 |
| `ACCS` | 下标/字段访问 |
| `CAST` | 运行时转换 |
| `BRCH` | 条件或 match 分支入口 |
| `CASE` | 分支体 |
| `EXIT` | return / exit |
| `WAIT` | waited 语义包装 |
| `VARI` | `var` 语义包装 |

### 3.3 AST -> GCT 的主要 lowering 模式

| AST | GCT | 类型 |
|------|------|------|
| `Literal` | `DATA` | 近似 1:1 |
| `RefData` | `DREF` | 近似 1:1 |
| `UnaryExpr` | `DREF("__op__") + LINK` | 降糖 |
| `BinaryExpr` | `DREF("__op__") + LINK (+ WAIT)` | 降糖 |
| `Call` | `LINK(callee, args...)` | 重编码 |
| `Bind` | `WITH(callee, args...)` | 重编码 |
| `Access` | `ACCS(target)` | 重编码 |
| `As` | `DATA` 或 `CAST` | 常量折叠 / 分流 |
| `IfExpr` | `BRCH + CASE(True/Else)` | 1:N 拆分 |
| `MatchExpr` | `BRCH + CASE(Value/Else)*` | 1:N 拆分 |
| `Array/Struct/Tuple` | `DATA` 或 `EXEC(temp..., DATA)` | 聚合 + synthetic 包装 |
| `FuncDecl` | `FUNC`，并在模块层额外补 `DECL` | 1:N 拆分 |

### 3.4 GCT 的来源绑定规则

旧规则偏向：

- 在 `visitStmt()` 或 `visitData()` 出口统一 `setOriginFromAst(...)`
- 内部 lowering 产物大多共享一个父级 `primarySpan`

本轮改造后：

- `DREF("__add__")` 这类 operator/callee 节点开始在创建时绑定自己的语义来源
- `LINK / WITH / ACCS / CAST / BRCH / CASE / WAIT / FUNC` 等主产物会在创建处登记 `SemanticBundle`
- `GctSemanticMap` 挂在 `SourceContext`，按 `GCT node origin` 存：
  - `mainOrigin`
  - `semanticParts`
  - `mergedInputs`
  - `syntheticReason`

这意味着 GCT 不再只有“主区间继承”，而是开始具备“主区间 + 命名部件”的来源表达。

## 4. GIR 语义模型

### 4.1 GIR 的定位

GIR 是最终执行图，不再保留树结构。其主要抽象是：

- 图：函数体或匿名子图
- 节点：值生产、调用、控制、聚合
- 边：`Norm / With / Ctrl`

### 4.2 GIR 主要执行节点

| 节点 | 语义 |
|------|------|
| `DATA` | 静态数据 |
| `PORT` | 形参与闭包入口 |
| `CAST` | 值转换 |
| `COPY` | 局部复制 |
| `FILL` | 聚合值或闭包值填充 |
| `ACCS` | 访问 |
| `BRCH` | 分支选择 |
| `JOIN` | 分支结果汇合 |
| `CALL` | 调用函数值 |
| `FUNC` | 可调用图值 |
| `OPER` | 已解析算子 |
| `EXIT` | 图输出 |
| `DREF / NREF / SYNC` | 构图辅助或过渡节点 |

### 4.3 GCT -> GIR 的主要 lowering 模式

| GCT | GIR | 说明 |
|------|------|------|
| `DATA` | `DataNode` 或 `DataNode + FillNode` | 复合值未解析时变成 `FILL` |
| `DREF` | 直接解析或暂时 `DrefNode` | `DREF` 最终不应留在 finalized graph |
| `LINK` | `OPER / FUNC / CALL` 及对应输入边 | 一个调用表达式可能变成一簇节点 |
| `WITH` | 给目标节点补 `With` 输入边 | 不触发调用 |
| `ACCS` | `AccsNode` | 访问目标值 |
| `CAST` | `CastNode` | 单值转换 |
| `BRCH` | `BrchNode + JoinNode + 子图` | 分支体各自成为子图 |
| `FUNC` | `Graph + FuncNode / FillNode / CallNode` | 闭包与函数值场景会分流 |

### 4.4 GIR 中最关键的多源节点簇

#### 调用簇

一个源码级调用在 GIR 中不一定只是 `CALL`：

- 命名函数调用常直接落在 `FUNC`
- 算子调用常落在 `OPER`
- 函数值调用才显式创建 `CALL`
- 闭包值还可能先出现 `FILL`，再被 `CALL`

因此 debugger 不应把“调用语义”只绑定到 `CALL` 一个节点上，而应结合：

- target node 自身来源
- 输入边角色
- `GirSemanticSourceMap` 中的 `semanticParts`

#### 分支簇

`if / match` 最终至少拆成：

- `BRCH`
- `JOIN`
- 每个分支子图的 `FUNC`
- `Norm(cond -> BRCH)`
- `With(caseValue -> BRCH)`
- `With(func -> JOIN)`
- `Norm(brch -> JOIN)`

所以源码级 stepping 最少要区分：

- 条件
- 分支体入口
- 分支结果聚合

#### 聚合簇

`FILL` 既可能来自：

- 复合字面量填充
- 闭包捕获填充

此时 `mergedInputs` 比单一 `originId` 更重要，因为它决定 debugger 能否回答“这个值由哪些源码部件拼出来”。

## 5. 信息损失与当前取舍

### 5.1 不可避免的信息损失

从 AST 进入 GCT/GIR 后，至少有三类信息天然会变粗：

1. 语法糖被降成底层调用或控制流
2. 多个子表达式被聚合成一个执行节点
3. 一个语言级表达式会拆成多个执行节点和多条边

### 5.2 当前选择

当前不追求“恢复出唯一字符级精确区间”，而是：

- 用 `mainOrigin` 保证稳定回源
- 用 `SemanticRole` 命名部件
- 用 `mergedInputs` 记录聚合来源
- 在 debugger JSON 中把这些结构显式暴露出来

## 6. 基础缺口审计

以下是当前链路上最关键的基础缺口，也是后续继续细化时必须优先面对的点。

### 6.1 AST 层缺口

- 仍有部分语法未实现完整 pattern / try-catch / comprehension lowering
- `bindingName` 这类名字在若干节点里仍主要存于 `load` 字段，而不是独立 child
- 少数节点的主区间仍偏向“整个子上下文”而非真正的语义最小包围区间

### 6.2 GCT 层缺口

- 仍有不少 synthetic 包装节点只共享父级主区间
- `ExecLoad` / 聚合常量包装对 debugger 来说仍然偏粗
- `kwargs`、动态 cast、复杂 unpack 等分支尚未进入完善的语义来源模型

### 6.3 GIR 层缺口

- 目前主要记录 node 级语义，尚未引入真正的 edge side table
- `semanticRole` 目前由 debugger 端按节点类型和边位序推断，后续可下沉为显式 builder 产物
- `CALL / FILL / BRCH / JOIN` 已有语义包，但未覆盖全部执行节点

### 6.4 debugger / runtime 缺口

- debugger 目前已能消费 `origin + semantic + semanticRole`，但 stepping 仍主要依赖主区间
- runtime 目前仍优先消费 `mainOrigin`
- “主区间 + 附加来源 + 多区间高亮”的 UI 语义还需要后续实现

## 7. 本轮落地结构

本轮代码已把以下结构引入到 `SourceContext`：

- `AstSemanticMap`
- `GctSemanticMap`
- `Gir graph semantic map`
- `Gir node semantic map`

统一数据模型为：

- `SemanticRole`
- `SemanticPart`
- `SemanticBundle`

其中：

- AST 负责产生细粒度锚点
- GCT 负责把 lowering 产物与这些锚点建立稳定关系
- GIR 负责把执行节点投影成 debugger 可消费的 `origin + semantic`

## 8. debugger JSON 投影约定

当前 debugger JSON 相比旧版本新增：

- `label`
- `sourcePath`
- `origin`
- `semantic`
- edge `semanticRole`

其中：

- `origin` 用于查看单个 graph/node 的派生链
- `semantic.semanticParts` 用于查看 operator/callee/argument/branchCondition 等部件
- `semantic.mergedInputs` 用于查看聚合来源
- edge `semanticRole` 用于解释 `Norm / With / Ctrl` 在当前目标节点上的语义

## 9. 后续路线

在当前基础上，后续优先级应是：

1. 继续补齐 AST 未落锚点的表达式与声明类型。
2. 把更多 synthetic GCT 产物改为“创建时绑定来源”，尤其是聚合包装和临时引用。
3. 把 GIR 的 edge 语义从“debugger 端推断”下沉为“builder 端显式注册”。
4. 让 debugger stepping 从只按 `mainOrigin` 走，逐步升级为“先主区间，再语义部件”。
5. 视需要把 runtime 诊断扩展到消费 `semanticParts`，例如区分 `callee`、`argument(slot=n)`、`branchCondition`。
