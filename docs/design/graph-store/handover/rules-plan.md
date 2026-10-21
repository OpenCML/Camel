# 第 8 步方案：`rule` 声明、策略、分析、随机检验与标准规则库

只做设计，不含代码。基于 fsh-dev de045203 之上的 gs/identity d6e2c1f0 / 59d87708。依据：`docs/design/rewrite/README.cn.md`（已定稿，16 条已确认）、图存储第二版 §5.1–5.4、§7 第 8 步、决定 5。

## 0. 结论先行

1. **引擎已经有了，第 8 步是"把 Camel 写的东西接到它上面"。** 已有：原生 `Rule`/`Pattern`、e-matching、`DraftWritings::saturate`/`extract`、`classFact`、驱动的提取闸门。第 8 步新增四块：**前端**（`rule`/`analysis` 声明 → 运行时的 `Rule`/`Analysis` 值）、**贪心引擎**、**策略库与 `graph` 模块里的入口**、**入库前的随机检验**；最后用 Camel 写的**标准规则库**取代 `std::opt::algebra` 的 C++ 实现（达到行为一致后）。
2. **规则是值。** `rule` 声明得到一个 `Rule` 类型的值（和函数一样可导出、导入、放进列表）。运行时对象 `RuleObject`：名字、方向（`=>`/`<=>`）、所需许可（掩码）、左右两边各一张小图、形参表（含 `const` 标记与类型变量）、`when` 条件（一个 Camel 函数值）。引擎在使用时把两张小图翻译成已有的 `Pattern`（见 §3）。
3. **`when` 里形参是 `graph::Node`**（图存储 §5.3：读值上的标签与事实用 `graph` 模块的查询函数），在左右两边形参是被匹配的**值**。同一个名字在两处类型不同，见问题 1。
4. **`rule` 与 scans 的 Camel 图 API 是上下两层**：`rule` 是主要写法（声明式，不接触事务）；引擎入口 `rewrite<rules>(p)`、`saturate<rules, budget>(p)`、`extract<cost>(p)` 是 `(p: Program) => Program!` 的普通函数，内部在 `graph::open` 同类的事务上跑原生引擎。需要全局判断的变换继续用底层节点 API 写（`neutral.cml` 那一类）。
5. **随机检验是一个工具加一个测试计划**，不是语言特性：枚举全部 `rule`，对每条规则**真的把左边写成函数、用引擎改写、再与原函数比较**，所以 `when`、许可、引擎路径一起被检验。

## 1. 现状（已读代码）

| 位置 | 事实 |
|---|---|
| 运行时 | `include/camel/runtime/writings.h`：`Pattern`（Var/Constant/Op/Compute）、`Rule{name,id,needs,lhs,rhs,when}`、`Budget`、`saturate`、`extract`、成本模型注册、`classFact`；`std::opt::extract(cost)` 已是流水线的遍 |
| scans 的 `graph` 模块 | `modules/graph/{handle,read,txn,module,nodekind}`：`Program/Graph/Node/Operator/TypeRef` 句柄；`open/replace/commit/abort`；读：`graphs,nodes,inputs,with_inputs,op,op_name,constant,type_of,type_name,is_anchor`。Camel 写的遍是导出的 `func(p: Program): Program!`，`src/execute/camel_pass.cpp` 把它作为遍运行（`std::opt::algebra(neutral_only)`、`dce(values_only)` 已用）。算子仍按 URI 文本比较（`':op/add_l'`），这正是 §5.3 要去掉的 |
| 前端 | 词法 → CST（`include/camel/parse/syntax/node_kinds.def`，`src/parse/syntax/parser*.cpp`）→ AST（`src/parse/ast`）→ GCT（`src/compile/gct/builder.cpp`，4394 行）→ GIR（`src/compile/gir/builder*.cpp`）→ 运行时程序。词法已有 `#`（`TokenKind::Hash`），`=>` 已用于 `match` 分支；`<=>` 不存在。声明有 `FuncDecl`（前缀 `FuncAnno` = `@装饰器`）、`ExportDecl` |
| 标签 | 设计 6.2 的 `tag` 声明与 `#名字<参数>` 是 **H2（队列第 7）**，还没做。数值许可今天是 `@numeric(reassociate, finite_math)` |
| 枚举 | H1 已完成：节点种类、许可可以是 `enum` |
| 泛型 | `typeas T` 已有；`where` 约束的定稿在裁定 2.18（规则稿 2.5 依赖它） |

## 2. 语法与前端

### 2.1 文法（新增产生式）

```
ruleDecl     := funcAnno* 'export'? 'rule' IDENTIFIER angledParams? parentParams '{' ruleBody '}' ('when' dataExpr)?
ruleBody     := dataExpr ('=>' | '<=>') dataExpr
analysisDecl := 'export'? 'analysis' IDENTIFIER ':' typeExpr '{' analysisClause* '}'
analysisClause := 'initial' '=>' dataExpr
                | ('join' | 'widen') '(' identDef ',' identDef ')' '=>' dataExpr
                | 'on' '(' dataExpr ')' '=>' dataExpr
```

- `rule`、`analysis` 与 `tag` 一样是**顶层的上下文关键字**（只在声明位置读作关键字，不占用普通名字）。`initial/join/widen/on` 只在 `analysis` 大括号内是关键字。
- `<=>` 是新的词法记号。形参里 `const x: float`（`const` 修饰，规则稿 2.2）：`KeyParamPair` 加可选的 `const` 前缀。
- `when` 写在右大括号之后（设计稿的位置）。`#requires<reassociate>` 见问题 2。
- 右边可以带块标签 `#fused { ... }`（规则稿 2.3），依赖 H2 的标签语法；先不做，规则造出的节点只继承被匹配根的标签。

### 2.2 要碰的文件（请前端车道确认）

| 层 | 文件 | 改动 |
|---|---|---|
| 词法 | `src/parse/syntax/lexer.cpp` | 记号 `<=>` |
| CST | `include/camel/parse/syntax/node_kinds.def`、`src/parse/syntax/parser.cpp`、`parser_impl.h`，**新文件** `src/parse/syntax/parser_rule.cpp` | `RuleDecl`、`RuleBody`、`AnalysisDecl`、`AnalysisClause`；`KeyParamPair` 的 `const` |
| 文法文档与检查 | `docs/language/08_top_level_and_grammar.md`、`scripts/check-grammar.mjs` | 产生式（三处必须一致，现有检查） |
| AST | `include/camel/parse/ast/stmt.h`、`src/parse/ast/stmt.cpp`、`builder.cpp` | `RuleDeclAst`、`AnalysisDeclAst` |
| 格式化 | `src/service/codegen/source/generator.cpp` | 打印两种声明（`camel-format` 保持 AST 不变的测试在 `frontend.format_keeps_ast`） |
| 语义/LSP | `src/service/semantic/{analyze,queries,syntax_index}.cpp` | 名字解析、悬停、跳转；诊断 |
| GCT | `src/compile/gct/builder.cpp` | 形参、左边、右边、`when` 的类型检查（左右两边是**普通表达式**，类型检查与函数体相同） |
| GIR | **新文件** `src/compile/gir/builder_rules.cpp`（+ `builder.cpp` 的一个入口） | 生成 `RuleObject` |

wide 的区域工作（R2/R3）在 `builder_control.cpp` 与 GCT 控制流；我的改动放在新文件里，只在入口处各加一行，冲突面小。请协调者告诉我哪些文件现在是别的车道在改。

### 2.3 编译

- **左边**：把左边表达式按函数体同样的规则建图，但在一个**私有的小程序**里（不进用户的程序）；形参成为 `Port` 节点（形参序号 = 模式变量序号）。`const` 形参标记在 `RuleObject` 的形参表里。
- **右边**：同样建图，形参同一批端口。**只由常量与 `const` 形参组成的子图**在编译时识别为 `Compute`（规则稿 2.3：改写时求值）。
- **许可**：`needs` = 许可掩码（标签 `#requires<...>`；H2 之前见问题 2）。
- **诊断**（目录里新增项，各一条测试）：右边用了左边没有的形参；左边是单个变量（匹配一切）；`<=>` 两边变量集不同；`<=>` 用于贪心引擎；`const` 形参在左边没有出现在常量位置；规则的两边类型不同。

## 3. 运行时：`RuleObject` → 原生 `Pattern`

`RuleObject` 是 GC 管理的运行时对象（和函数值同一族：持有私有程序的 `GraphId` 与那个程序，函数值式的 `updateRefs`）。引擎使用时 `compilePattern(RuleObject) → Rule`，缓存在对象上：

| 小图节点 | `Pattern` |
|---|---|
| `Port`（形参 i） | `patVar(i)`，`const` 形参 `patConst(i)` |
| `Data` 常量 | `patLiteral(位, 类型)` |
| `Oper` | `patOp(算子编号, 子项, 类型)` |
| 右边仅含常量与 `const` 形参的子图 | `patCompute(类型, 求值函数, 参数)`：求值函数调用该算子的实现（和 `std::opt::fold` 同一个求值路径，故障不折叠） |
| 其他种类（BRCH、LOOP、FUNC 调用用户函数） | 第 1 版不支持，编译诊断"此处不能出现在规则里"；§7 列为后续（规则稿 2.2 末几条） |

**泛型规则**（`typeas T` + `where`）：小图里的算子在 T 未定时无法解析为 `:op/add_l` 之类。做法：模式的 `Op` 节点可以带**算子族**（"加法"，由 `OperatorSemantics::binary` 的声明选择，和 `algebra.cpp` 的 `Families` 同一机制），匹配时绑定 T := 被匹配节点的类型再解析。这需要引擎小改（`Pattern::Op` 增加 `family` 与 `typeVar`）。见问题 3。

`when`：`RuleObject` 带一个 Camel 函数值 `(Node, ...) -> bool`，在原生 `Rule::when` 回调里用解释器执行，参数是各形参对应的 `graph::Node` 句柄（指向正在改写的事务里的值）。重入与 GC：与 `camel_pass.cpp` 运行 Camel 遍同一机制（`running_` 计数、`RunScope`）；每次匹配一次调用约微秒级。常见条件（是否常量、类型、纯度）以后可以编译成原生谓词，第 1 版不做。

## 4. 引擎与策略

### 4.1 引擎（原生）

- `saturate`：已有。入口加：取 `Budget{rounds, writings, unions}`（只有计数，决定 7）。
- **贪心 `rewrite`（新）**：对只有单写法的图。用同一套 `Pattern` 匹配（类全是单元素，免费复用）；按规则稿 §3：节点编号顺序、规则在列表中的顺序，各用一遍；匹配则 `build(rhs)`（`lookupOrAdd`，自带 CSE）后 `replaceAllValueUses(根, 新)` 并擦除变成无用的旧节点，把根的控制输入交给新根（规则稿 2.4，已在饱和里实现过同一规则）；受影响的使用者进工作队列；`fixpoint` 的轮数受参数限制。`<=>` 规则交给它是错误（诊断在引擎入口检查，见问题 7）。

### 4.2 策略库（Camel 写，模块 `rewrite`）

策略是 `(p: Program) => Program!` 的普通函数（规则稿 §3）：

```camel
export func seq<ts: ...>(p: Program): Program!        // 依次，任一失败则失败
export func fixpoint<t, n>(p: Program): Program!      // 反复直到图不再变，至多 n 轮
export func try<t>(p: Program): Program!
export func first<t1, t2>(p: Program): Program!
export func within<pred, t>(p: Program): Program!     // 只作用于满足条件的区域/节点
```

- `rewrite<rules>`、`saturate<rules, budget>`、`extract<cost>` 是**原生内核**，声明在 `modules/rewrite`（新模块，import `graph`），内部对 scans 的事务取 `GraphDraft`、构造 `DraftWritings`；这需要 scans 的 `Txn` 暴露"某张图的草稿"（读写之外）。问题 5。
- `within<pred, t>`：谓词在节点上求值（`graph::Node -> bool`），限制引擎的候选集合（`byOper` 的过滤）。
- 命令行：`camel prog.cml mymod::simplify` 沿用 `camel_pass.cpp`；`std::opt::rewrite(rules="mymod::simplify")` 这类内建遍不做，用户写一个导出函数即可。

## 5. 分析

`analysis range: Interval { initial => ..., join(a, b) => ..., widen(a, b) => ..., on (x + y) => range(x) + range(y) }`：

- 编译为 `AnalysisObject`：值类型 T、`initial` 函数、`join`、`widen`、若干 `on` 子句（每条是一个与规则左边同样编译的小图加一个函数 `(各输入的事实) -> T`）。
- 声明同时定义**查询函数** `range(n: Node): Interval`，在 `when` 与别的分析里使用。
- 引擎：沿数据依赖传播（拓扑序），分支汇合处 `join`，循环处 `widen`；多写法的值用已有的 `classFact` 对各写法的 `on` 结果取 `join`（规则稿 4.2）。结果挂在**事实库**（`facts.h`）：新增事实种类 `user.<模块>.<名字>`，印章含图的 `revision()`；增量与提前截断依赖节点级印章（已知债务，第 1 版整图作废）。
- **限制（第 1 版）**：T 是平凡数据类型（标量、枚举、元素为标量的结构/元组）。含 GC 对象的事实涉及事实库对根的管理，另议（问题 4）。
- 内建分析（常量、类型、形状、纯度）保留原生实现，接口与用户分析相同。
- 检验（规则稿 4.3）：`join` 的交换/结合/幂等律，`on` 的单调性，随机程序上事实的正确性——同一个检验工具（§6）。

## 6. 入库前的随机检验（红线）

工具 `camel check-rules <模块>`（`tools/camel-cli` 的子命令，用 `test/tools` 里的计划驱动）：

1. 枚举模块导出的全部 `Rule`（以及 §5 的 `Analysis`）。
2. 对每条规则、每个类型实例（泛型规则取其 `where` 允许的类型）：按形参类型生成**随机输入**（整数含边界、浮点含 ±0、无穷、NaN、非规格数——被规则声明的许可排除的特殊值除外，如 `finite_math` 排除 NaN/无穷；`const` 形参取随机常量；张量取小形状）。
3. **把左边写成一个函数**（`func lhs_fn(形参...) { return 左边 }`），用 `rewrite<[这条规则]>` 改写，若规则没有触发（`when` 不满足或形状不对）则换一组常量再试；至少触发 N 次（默认 30）否则报"规则从未触发"（防止空检验）。
4. 对随机输入分别运行原函数与改写后的函数（`std::nvm`，另抽样 `std::fvm`），**按规则声明的许可比较**：无许可 = 逐位相同（NaN 之间只比类别）；`reassociate` 等 = 数值契约 7.3 的误差界（界的表由 op-semantics 车道提供，问题 6）。
5. 种子固定、可复现；失败时打印最小反例（形参值、两边结果、规则名、位置）。
6. 测试计划 `rules.library.check`（fast 层，每条规则 30 次触发；slow 层每条 2000 次）。**标准规则库不通过不能合并**：库里每个模块一个测试，新模块自动被枚举（计划枚举 `stdlib/rules/*.cml`）。

## 7. 标准规则库与迁移

- 位置：`stdlib/rules/algebra.cml`（及后续 `tensor.cml`、`control.cml`）。第一批 = 现在 C++ 的语料规则：交换、整数中性元、`x*c+x*c`、`x*c1+x*c2`、`(x*c1)*c2`，以及浮点许可版本。泛型写法依赖问题 3，否则先按类型各写一份（宏生成）。
- 迁移（两步，中间态可并存）：① 规则库通过检验，且 `saturate<algebra, budget> + extract<nodes>` 与 `std::opt::algebra` 在语料与 `scale` 程序上算子数、输出一致（现有 `saturate.*` 测试的 Camel 版）；② `std::opt` 的代数阶段改为调用规则库，`algebra.cpp` 删除（不留兼容层）。`std::opt::fold` 的常量折叠留作原生求值，规则的 `Compute` 复用它的求值函数。
- 之后的规则：`x - x`、`x / 1`、分配律、`map` 融合等，由各车道以 PR 形式加入，检验自动覆盖。

## 8. 提交顺序（每步可构建、相关测试通过）

| # | 内容 | 测试 |
|---|---|---|
| 8.0 | `RuleObject` 与 `compilePattern`（小图 → `Pattern`），用测试遍手工建小图 | 编出的 `Pattern` 与现在手写的语料规则饱和结果一致（复用 `saturate.default/safe`） |
| 8.1 | 贪心 `rewrite` 引擎（原生）+ `fixpoint`；根的控制输入转移 | 语料用贪心规则子集得到同样算子数；`<=>` 交给贪心被拒 |
| 8.2 | 前端：词法 `<=>`、CST/AST、格式化、文法检查（只解析，不编译） | `frontend.*` 解析/格式化往返；文法三处一致 |
| 8.3 | GCT/GIR：`rule` → `RuleObject`，诊断 6 条 | 声明的规则饱和/贪心得到 8.0 的结果；诊断各一条 |
| 8.4 | `when`：Camel 函数值回调（`graph::Node` 形参）、`is_pure`/`constant` 等查询 | 条件规则（`map` 融合、系数检查）；重入与 GC 压力下的稳定性 |
| 8.5 | `modules/rewrite`：`rewrite/saturate/extract` 内核，策略库（`seq` 等），`within` | 用户写的 `simplify` 遍；命令行 `mod::simplify`；与 `neutral.cml` 同输出 |
| 8.6 | `camel check-rules` 与 `rules.library.check` 计划 | 故意写错的规则被检出（负例）；种子可复现；"从未触发"被报告 |
| 8.7 | `analysis`：语法、编译、传播、事实库种类、`range` 查询、`join/on` 检验 | 区间分析在 `when` 里使用；多写法值取 `join` |
| 8.8 | 标准规则库 `stdlib/rules/algebra.cml`；与 `std::opt::algebra` 对拍；切换并删除 `algebra.cpp` | `opt.algebra` 计划全绿（行为一致）；性能不劣于现在 |
| 8.9 | 文档（`docs/technical/45`、语言参考、诊断目录）、LSP 悬停 | 目录检查、文法检查 |

## 9. 性能与风险

- 规则声明只在加载时编译（一次）；匹配走原生。`when` 回调是主要成本（解释器调用），只有在 `when` 存在且前面的原生匹配成功时才调用；9 的"表"：语料 3 条 `when` 规则 × 匹配数 ≈ 100 次调用，可忽略；大图（`wide` 80000 项）上 120 万次匹配若全部要调 `when` 会明显变慢 → 常见条件编成原生谓词（§3 末），列为 8.8 的前置测量。
- **风险**：①前端是共享文件，冲突面大（缓解：新文件 + 入口各一行，8.2 先于一切语义工作合入）；②`when` 回调的重入与 GC（沿用 Camel 遍的运行机制，8.4 专项压力测试）；③泛型规则需要引擎小改（问题 3）；④H2 标签未到（问题 2）；⑤事实库里存用户类型的值（问题 4）；⑥随机检验的误差界表属于 op-semantics（问题 6）。
- 时间无关的确定性：规则列表顺序、节点编号决定结果；`saturate` 无时间预算（已定）。

## 10. 需要你决定的问题（含建议）

1. **`when` 里的形参类型**：左右两边 `x: float` 是值；`when` 里需要 `graph::Node` 才能问常量、算子、事实。同名不同类型会让读者困惑。建议：`when` 里 `x` 就是 `Node`（显式），值类型用 `type_of(x)` 取；以后可加语法糖 `x.constant::<float>()`。备选：`when` 的形参另写（`when (nx: Node) => ...`），更啰嗦。
2. **`#requires<...>` 依赖 H2**。建议：8.3 之前如果 H2 未到，临时用现有 `@requires(reassociate)`（`FuncAnno` 的位置，已能解析），H2 合入后改写法（不留兼容）；规则稿 2.1 的写法是最终形态。
3. **泛型规则**（`typeas T` + `where`）：建议做算子族 + 类型变量（§3），库里一份规则覆盖 4 个数值类型；否则库里要 4 份几乎相同的规则。依赖裁定 2.18 的 `where`。
4. **分析的事实类型**：第 1 版限平凡数据类型？建议同意；含对象的事实（形状向量等）等事实库的根管理。
5. **引擎入口放哪里**：建议新模块 `modules/rewrite`（import `graph`），原生内核在那里；需要 scans 在 `Txn` 上提供"取某张图草稿"的内部接口（不暴露给 Camel）。请你和 scans 确认接口形状。
6. **随机检验的误差界**：`reassociate`、`no_infs` 等许可对应的数值界由谁出表？建议 op-semantics 车道给一张表（许可 → 相对/ULP 界，随元素个数），检验读它。
7. **`<=>` 用于贪心**：规则列表是值，"编译期报错"只能在 `rewrite<rules>` 的 `rules` 是静态可求值时做。建议：静态可求值时编译错误，否则在引擎入口运行时错误（都有诊断）。
8. **规则产生的来源标签**（规则稿 2.3）：需要在 `node_attrs.h` 注册一个节点属性键 `rule`（值 = 规则名），合并规则 = 保留被匹配节点的。同意？
9. **前端文件的归属**：8.2 动词法/文法/CST/AST/格式化——请你确认实现车道现在不在这些文件里大改，或指定合入顺序。
10. **旧实现的退役**：8.8 完成后删除 `src/passes/opt/generic/algebra.cpp` 与 `std::opt::algebra` 参数；`std::opt::algebra(neutral_only)` 的 Camel 遍同时被规则取代（scans 的 `neutral.cml`）。同意？
