# H7 计划：资源与效应（`uses`、`pure`、资源推断、句柄类型）

- 状态：**计划，未实现**
- 分支：`handover/H226-effects-plan`（基于 `origin/develop` 的 `f54822a5`）
- 任务来源：`docs/handover/README.cn.md` 5.00 第 4 条（H7）；设计原则 3.3；`reviews/H141-H213.md` D2
- 交付：本文件。不写实现，不改语义，不建新工作树，不加看板行。
- 一句话结论：H7 的四项（`uses`、`pure`、资源推断、句柄类型）都能落到现有模型上，**不需要任何 3.3 之外的新原语**；图上新增的东西只有**控制边**，调度器不用改。3.3 的其余条目（`var`、无序资源、并发、取消）各自的归属见 1.1 与第 9 节。

---

## 0. 结论摘要

### 0.1 步骤顺序

| 步骤 | 内容 | 依赖 | 可独立落地 | 锁住它的测试 |
|---|---|---|---|---|
| 1 | 内部模型：`Handle` 资源类、图足迹的返回来源（无语法） | — | 是 | `modules/resources` 新用例 |
| 2 | 函数类型带资源签名；原生声明的类型带上 C++ 声明（无语法） | 1 | 是 | `inspect --ops` 的类型打印 |
| 3 | 语法：`resource`/`unordered`、`uses`、`pure`；AST、类型、诊断、format、语法文档 | 2 | 是 | `parse`、`format`、`compile/diag_*` |
| 4 | 标准库无体声明逐个写 `uses`/`pure`；`--verify-ops` 双向核对 | 3 | 逐模块 | `modules/stdlib_registry`、`--verify-ops` |
| 5 | 推断与承诺检查：不动点、`pure`、导出变化警告、展开文档 | 4 | 是 | 新 `compile/effects` |
| 6 | 建图排序：同一资源生成控制边（6a 成对、6b 按资源链稀疏化） | 5 | 6a/6b 分开 | `modules/resources` 的 `ORDER`、新 GIR 快照 |
| 7 | 句柄类型：`#resource`、逐值键、`Stream`/`next`、别名发现 | 3、6 | 是 | 新 `compile/effects` + `para/handles` |
| 8 | 无序资源：`resource … unordered`，冲突不连边 | 6 | 是（与 7 并行） | 新 `para/unordered` |
| 9 | 退役 `wait` 与旧诊断（`IgnoredSideEffect` 等）、文档 | 6 | 是 | `diag_json`、`linear/captured_writes` 改写 |
| 10 | 第三方漏声明的提示；验收矩阵；生成 `docs/diagnostics.md` | 4 | 是（与 9 并行） | `diagnostics-catalog --coverage` |

步骤 1、2 是纯内部改动，不用户可见；3 是唯一一次语法变更；4 是机械迁移，按模块提交；5、6 是语义核心；7、8 是句柄与无序；9 是清账；10 是收尾。每步先让本步测试变绿，不把树留红。

### 0.2 三件最危险的事

1. **步骤 5 + 2 的类型表示。** 未写 `uses` 的函数，其资源集由函数体推出后再决定；函数类型在函数体建好之前就已被调用者引用。必须沿用 `OpenFunctionType` 的"先开后定"（`func.h:249-265 # "A new object of class T (FunctionType or OpenFunctionType) holding"`，返回类型已经这样定），并在不动点结束后**重新校验一遍函数值转换**；否则先检查的调用点拿到的是空集。
2. **步骤 6 动的是所有现有图。** 今天 `sync` 块之外的环境效应没有控制边，很多 golden 是照这个写的；加边会改 `std::gir` 快照、`std::opt` 的折叠与 `std::prl` 的并行度。只对**真正冲突**的对加边，并逐个复核 golden，不能为了让测试变绿而削弱规则。
3. **步骤 7 的"可能同一对象"会把流串行化。** 句柄从形参进来时 `mayAlias` 一律为真（`resources.cpp:601-610`：两个 Port 可能是一个对象），所以 `pump(s, t)` 内部必然串行。这是正确的保守；要并发必须在**同一个图里**能证明两个句柄不同（各自 Fresh）。计划把这条写进文档与测试，不让实现"聪明地"假设不同。

### 0.3 需要作者裁定

见第 8 节。最关键的是 Q1（`#resource` 只许名词性类型）、Q2（导出资源变化警告拿什么当基线）、Q4（返回的句柄是 Fresh 还是随函数体推断）。

---

## 1. 现状（测量）

### 1.1 规则表

| 3.3 的规则 | 今天 | 判据文件 | 锁住它的测试 |
|---|---|---|---|
| 资源由库用 `resource` 声明（`export resource fs`），可分组 `resource io = [stdout, fs]`，共用命名空间、可导入、标准资源默认可见 | **缺**（语言层）。资源名只是 C++ `ResourceSpec` 里的字符串；`stdout`/`fs`/`rng` 由 `externalAmbient` 写死 | `resource_semantics.h:168-171 # "bool operator==(const ResourceSpec &) const = default;"`；parser 无 `resource` 记号（`src/parse/syntax/` 全文无） | `modules/resources` 的 `resource_names` 钉的是 C++ 名字表，不是语言声明 |
| 每个 `var` 自动是资源；`var` 形参是资源参数；闭包捕获 `var`，函数类型就带着它 | **部分**。`var` 单元、`var` 形参、捕获在运行期足迹里都正确；函数**类型**里没有资源。捕获只体现在足迹的 `Capture` 绑定，不进类型 | `resources.cpp:212-278`（Port→Norm/With、捕获→Capture、Fresh 丢弃）；`builder_expr.cpp:433-439` | `modules/resources` 的 `core`（`bump`、`countdown`、`__g_5__` 捕获）；`linear/captured_writes` |
| 资源是函数类型的一部分；原生算子在签名里写 `uses`；没写的按"全局"资源，与一切副作用保持顺序 | **部分/缺**。`FunctionType` 只有修饰符、参数、返回类型（`func.h:104-190 # "The interned signature equal to this one in everything but its"`），没有资源；原生算子的语义在 C++ `ResourceSpec`，没写的按 `conservativeResources`（Unknown 类、碰每个操作数、external） | `func.cpp:129-147 # "FunctionType itself is constructed through this specialization only, by"`（intern key 无资源）；`resources.cpp:333-352`；`resource_semantics.h:178-179` | `modules/resources` 的 `declared_resources`；`stdlib_registry.purity_declared` |
| Camel 函数的资源由编译器推出（所调用者之和，递归求不动点），悬停与文档以限定名显示；显式 `uses` 是承诺，超出是编译错误；导出函数推出资源变化给警告 | **部分**。运行期 `footprintOf` 就是那个不动点（含递归、捕获、局部 Fresh 丢弃），但只在需要时算，不存进类型、不在编译期检查、无悬停、无变化警告 | `resources.cpp:142-279 # "if (!recursed_"`（`FootprintWalk`，`recursed_`/`grew_` 的循环在 `:158-166`） | `modules/resources` 的 `countdown`（不动点）；承诺与警告**没有测试** |
| 效应以资源声明为准；签名里的 `var` 由资源声明检查，不一致是声明错误 | **已实现**（C++ 声明层） | `operator_table.cpp:182 # "if (auto problem = sem::checkVarParameters(resources, with, norm)) {"` 调 `checkVarParameters`（`resource_semantics.h:193-199`） | `modules/resources` 的 14 条拒绝（`var_param_not_written`、`written_without_var` 等） |
| `pure` 是承诺，由编译器检查，可写在函数声明与函数类型上；函数类型参数默认对资源泛型；资源少的函数可用在要求资源多的地方 | **缺**（语言层）。C++ 侧有 `OperatorPurity`（`pure`/`effectful`/`state`/`undeclared`）与 `hasExternalEffect`；函数类型没有 `pure`，没有资源子类型 | `operator_table.h:307-308`、`:335-336`；`func.h` 无资源字段 | `stdlib_registry.purity_declared`（C++ 侧）；`native_declarations`（声明是模块的签名） |
| 同一资源自动有序：同一函数体内真正执行的、用到同一资源的副作用按书写顺序（编译器生成控制边）；调用是调用处的"一个副作用"；不跨函数重排 | **部分**。`sync` 块内语句串成一条 ctrl 链；**块外**：读总是排在最后写者之后，写只在自己被 `wait` 时排在之前使用者之后；环境资源与环境效应在 `sync` 外没有**为保证副作用顺序而加**的边（只剩数据依赖碰巧给出的顺序） | `builder.cpp:820-954 # "be analysed further (its dropped effects, its result)."`（sync 链与返回 GATE）；`builder_expr.cpp:370-426`；`resources.cpp:706-734`（只查询，不建边） | `values.sync_order`、`compile/gir` 的 `sync.ctrl_dedup_snapshot`；`modules/resources` 的 `ORDER` 全在一个 `sync` 函数里（`test/cases/resources/effects.cml:20`），**没测过块外按资源排序** |
| 算子纯度必须声明；没声明的按有副作用对待（最多少优化）；仓库标准库逐个声明；第三方漏了给提示 | **部分**。没声明→`pure=false`、`hasExternalEffect=true`、保守资源；仓库由 `purity_declared` 与启动日志守住；第三方只有一行日志，不是可过滤的诊断 | `src/core/resource_semantics.cpp:91-101 # "conservativeResources("`；`introspect.cpp:211-236`；`context.cpp:525-530` | `stdlib_registry.purity_declared`（`ops.length > 300`，且 `undeclared` 为空） |
| 可能故障不强制声明；没声明按可能故障 | **已实现** | `effects.cpp` 的 `mayFault` 保守合并；`include/camel/runtime/effects.h` | 无专门用例（随 `EffectSummary`） |
| 无序资源 `resource net unordered`：其上的副作用不保持书写顺序；要顺序就写 `sync` 块或用句柄 | **缺**。`ResourceSpec` 没有无序标志，语言层没有这个词 | parser；`resource_semantics.h:146-160 # "struct ResourceSpec {"` | 无 |
| 句柄类型：`#resource` 类型，每个值一个独立资源；签名写 `uses 形参名`；同一句柄按书写顺序，不同句柄互不约束；调用处推出是哪个值 | **缺**。没有 `#resource` 标签（`tags.cpp` 的标准标签表里没有它），函数类型没有资源，`ResourceClass::Foreign` 只是"模块包的对象" | `tags.cpp:159-166 # "on a function: its elements do not depend on each other (design"` 是标准标签的写法；`struct.h:118-205 # "(design effects plan 3.1): a value of the type is"` 是名词性类型的落点 | `tags.plan` 的 `independent.*` 是"声明→图属性"的现成范式；句柄无用例 |
| 并发不需要 `async`/`await`；独立计算与不同资源可同时进行；运行时在一个副作用等待时执行其他就绪节点 | **部分**。`std::prl` 就是 `std::taskflow`，任务图只由 norm/with/ctrl 边决定；不在同一条链上的两个节点可以并发。`wait` 旧写法仍在 | `src/execute/pass.cpp:847`；`src/passes/sched/taskflow/` | `para` 各计划；没有"两个句柄并发"的用例 |
| 取消只对 `cancellable` 资源/算子 | **缺** | — | — |

### 1.2 一个便宜处：类型声明已经是名词性的

`StructDecl` 是"每个 `type` 语句一个"的声明对象（`struct.h:118-150 # "A dense process-local identity, part of the mangled name of an instance."`），实例按"声明 id + 实参 mangle"驻留（`struct.cpp:101-107 # "key += arg->mangle();"`），`StructType::decl()` 可取回声明（`struct.h:205`）。所以：

- 两个字段完全相同的 `type` 声明**不会**被合并，`Stream` 和匿名同形结构体是两个类型；
- 声明标签已经按声明名存放并可查询（`module::Module::declarationTags`，`module.cpp:654-687`；`tags.cpp:381-408`）；
- `#resource` 只需要挂到声明上，再加一个谓词 `isHandleType(Type*)`，**不需要新的类型种类**。通用实例 `Stream<int>`/`Stream<float>` 是不同类型、同一个声明，类名取"声明限定名"。

### 1.3 与文档/代码不一致处（记为缺陷/待办，不在本计划顺手改）

1. `docs/language/04_execution_model.md:150-194` 仍写 `wait` 与前缀 `sync func`，而 3.3/§14.2 已退役；代码也仍接受（`builder_diagnostics.cpp:171-200` 的注释还按旧写法解释）。步骤 9 一并改。
2. `IgnoredSideEffect`（"既没 wait 也不在 sync 块"）、`VarParamInAsyncFunction`、`CapturedWriteInAsyncFunction` 三条警告的前提是"块外不自动排序"。步骤 6 之后前提消失；步骤 9 删除或重写，不能留一条已经不成立的理由。
3. `EffectNotExecuted` 的判定是布尔"有没有外部效应"（`builder_diagnostics.cpp:202-294`），不是资源摘要；步骤 6 以后仍用它，但"没被依赖的效应"与"资源冲突"要分清：前者仍警告，后者由控制边解决，不再需要 `wait`。
4. 运行期 `requiredOrderings` 已经能列出"应该有序但没连边"的对（`resources.cpp:706-734`），但只有 `test::resources` 用；没有任何门禁。步骤 6 把它变成建图的判据与验收。

---

## 2. `uses`

### 2.1 语法

```
resource fs                                  // 环境资源，按书写顺序
resource net unordered                       // 无序资源
resource io = [stdout, fs]                   // 组：只是两个名字的简写，不是新资源
export resource myres                        // 导入规则与其他名字相同
```

- `resource`、`unordered`、`uses`、`pure` 都按**上下文关键字**处理，不进全局保留字表：`uses`/`pure` 只在函数签名（声明或函数类型）的返回类型之后被识别；`resource` 只在顶层/命名空间项的开头、后随标识符时识别；`unordered` 只在 `resource` 声明末尾。这样现有叫 `pure`/`resource` 的变量不破。
- 组的语义：`uses io` 展开为 `uses stdout, uses fs`；组不参与"同一个组"这种匹配。`unordered` 写在组上表示组内每个成员都无序。
- 标准资源 `stdout`、`stderr`、`stdin`、`fs`、`clock`、`env` 由核心预声明（`resource_names` 已经钉住这组名字），不需要导入。

函数声明：

```
func next(s: Stream<T>): T? uses s { ... }
func save(p: Params): unit uses fs { ... }
func twice(x: int): int pure { ... }
func pump(s: Stream<T>, t: Stream<T>): unit uses s, t { ... }
```

函数类型：

```
f: (x: T) => U pure
g: (s: Stream<T>) => T? uses s
h: (x: int) => int                 // 不写：对资源泛型
```

放置与解析：`uses`/`pure` 紧跟在返回类型之后、函数体的 `sync? {` 之前。`parser.cpp:941-942` 现在用"下一个记号是不是 `{` 或 `sync {`"判断有没有函数体；扩展成"`uses`/`pure` 也算函数体前的内容"，读完子句再判 `sync? {`。函数体是**无体声明**时，子句就是声明的结尾。`parseFuncType`（`parser_type.cpp:194-215`）在返回类型之后读同一个子句。

名字解析顺序：本函数的形参名优先（句柄或 `var`），否则查资源声明（含组展开），都没有报 `UnknownResource`。形参与资源同名时形参胜出，按词法作用域解释。`uses` 列表不许有括号、不许重复（重复报既有 `Redeclaration` 语义的诊断，不新增码）。`pure` 与 `uses` 同时写报 `PureAndUses`。

### 2.2 类型里的表示

一个函数类型带一个 `ResourceSignature`：

```
mode    : Generic | Concrete
params  : 位集，第 i 位 = 用到第 i 个形参（with 在前、norm 在后，和 varMarks 同一编号）
captures: 位集，第 i 个闭包捕获（只在推断出的闭包类型上出现，源码写不出来）
ambient : 排序过的资源名（SymId），来自 resource 声明或标准资源
anyOf   : 资源类列表；非空表示"说不出是哪个值"，即保守的全局
```

- `Generic`：函数类型上**没写**资源子句。它匹配任何资源集；调用点按实际值实例化。
- `Concrete`：写了 `uses`/`pure`，或由函数体推出。`pure` 就是 `params`/`captures`/`ambient`/`anyOf` 全空的 `Concrete`。`pure` 的函数有 `var` 形参（`var` 形参本身就是资源）是 `UsesPromiseViolated`；`uses` 里写 `var` 形参是正常的（同一个资源）。
- `assignableFrom`：目标是 `Generic` 时接受任何源；目标是 `Concrete S` 时源必须是 `Concrete T ⊆ S`（资源少的能用在要求多的地方）；源是 `Generic`（一个还没实例化的回调形参）时**不能证明**，拒绝——除非它就是目标位置同一个泛型形参。`pure`（空集）因此可用在任何位置，而"任意资源"的函数值不能当纯函数用。
- 与 6.6 的关系：6.6 说算子的纯度、资源等事实是标准标签；`pure`/`uses` 是**表面子句**，降级后仍然是声明上的事实（存储可以就是属性/标签），不产生第二套写法。这一条写进文档，避免实现时另造一份真值。
- 驻留与打印：`Generic`/`Concrete` 与 `params`/`ambient` 进 `FunctionType` 的 intern key（`func.cpp:129-147 # "const size_t bytes = sizeof(FunctionType) +"`）与 `mangle`，`toString` 打印 `uses s, fs` / `pure`；没有形参名时打印 `uses #0`。`captures` 与 `anyOf` 只出现在推断出的类型上，同样进 key。
- 实现落点：`FunctionType::Parts`（`func.h:195-203`）加一个 `ResourceSignature`，尾部字节与 `Parts` 一起复制；`OpenFunctionType` 加"先开后定"的 `setResources`（与 `setExitType` 同一模式，`func.h:252-265 # "Written by the constructor, and for an open signature by"`）。**不改** `TypeCode`，不加新的 `Type` 子类。

### 2.3 调用处怎么推

关键句是 3.3 的"调用另一个函数，在调用处是'一个用到其资源的副作用'"。所以调用点只看**被调用者的类型**，不看它的函数体：

1. 取被调用者的 `Concrete` 签名；`Generic` 时取实际函数值的具体类型（静态函数常量、已知闭包、实参的声明类型），取不到按 `anyOf` 全局处理。
2. 对 `params` 的每一位 i：把第 i 个实参节点作为"命名该资源的那个值"，生成一条 `NodeAccess{ class=该形参类型的类, value=实参节点, mode=ReadModifyWrite }`。类由**形参类型**决定：`#resource` 类型→`Handle`，`var` 形参→`Variable`，普通对象→`Object`。
3. 对 `ambient` 的每个名字：生成 `NodeAccess{ Ambient, name, RMW }`，没有值。
4. 对 `captures`：闭包直接调用时绑定到闭包的捕获槽；否则 `anyValue`。
5. `anyOf`：生成 `anyValue` 的访问，与同类的一切冲突（今天 `conservativeResources` 的效果）。

这几步与运行期 `Resolver::node` 对 `FUNC` 的映射完全同构（`resources.cpp:302-320` 的 `bind` lambda 就是第 2 步），差别只在运行期是事后查询，H7 要在建图时就知道。`uses s` 与 `var` 形参因此共用同一条路径：**都是"用第 i 个实参的名义"**，只是类不同。

调用点的结果摘要只需要存成三条事实：这个节点碰哪些类/名字、其中哪些绑到哪个输入值、哪些完全说不清（`anyValue`）。图上**不加节点、不加边**：句柄/变量本来就是该调用的普通输入边，那条边就是身份。

### 2.4 两个句柄指向同一个对象

调用者写 `pump(s, s)` 时，被调用者里两个形参都是 Port，`mayAlias` 为真（`resources.cpp:601-610`），于是被调用者内部两次 `next` 之间连控制边——**正确**：它们不是"不同句柄"。

调用者写 `pump(s, open("b"))` 时，被调用者内部仍然只能说"两个 Port 可能是一个对象"，仍然串行。这是 3.3 允许的保守（"最多少优化，不会算错"）。要并发，必须在**同一个函数体内**写两个可证明不同的句柄（各自 Fresh，见 3.3），或者把操作展开在调用者里。计划不为 H7 引入调用点特化/别名分派。

反向的发现（"两个 handle 是不是一个对象"）不需要新分析：`originOf`/`mayAlias` 已经区分 `Fresh`（两次 `open` 的结果不同）、`Port`（可能同一个）、`Unknown`（一律可能），`FootprintWalk` 已经用它在函数边界上丢弃/保留（`resources.cpp:245-252`）。

---

## 3. 句柄类型

### 3.1 声明

```
#resource
export type Stream<T> = {
    source: Source,
    ...
}
```

- `#resource` 是一个**标准标签**（与 `#independent`、`#macro` 同一机制：在 `declareStandardTags` 里声明，位置含 `Type`，见 `tags.cpp:159-166 # "on a function: its elements do not depend on each other (design"`），不是关键字，也不需要导入。
- 挂在**名词性类型声明**上（`type X = { ... }` / `type X = enum ...`）。别名（`type X = int[]`）与内建类型不允许：别名与目标结构同一类型，标签会落到别的名字上；内建类型没有身份。拒绝报 `HandleTypeNotNominal`。
- 标签让类型获得"句柄"谓词：`isHandleType(Type*)` = 名词性且声明带标签。类名取声明的限定名（`module:Type`），与 `ResourceClass::Handle` 一起构成资源身份的名字部分。
- 句柄类型的值**永远按引用**（不参与纯数据布局；10.5 的纯数据谓词对它返回假），这是"每个值有身份"的前提。

### 3.2 每个值一个资源

- 声明只决定**类**；具体是哪个对象由**值**决定。访问的键是 `(Handle, 声明限定名, 值节点)`。
- 两个值是同一个资源，当且仅当 `mayAlias` 为真：同一个节点、`let` 绑定（同一节点）、经 GATE 透传、`Same`/`View` 结果；两个不同 `Fresh` 结果是不同资源；两个 `Port` 可能是一个（保守）。
- 副本（`Copy`）是 Fresh，新对象，新资源。
- 键不写进图。图上句柄就是一条普通的值边：谁的值节点是它，谁的资源就是它。这正是"编译器在调用处推出用到的是哪个值"的图上的样子。

### 3.3 返回的句柄

`GraphFootprint` 今天把调用结果的来源硬写成 `Unknown`（`resources.cpp:286-287`："What a graph returns may be anything it received: not known here"）。这对句柄是致命的：`open` 返回的新句柄会被当成"可能别名任何输入"，于是所有 `next` 串行。

步骤 1 补上 `GraphFootprint.result`（`ResultAlias`）：走图时看返回节点（`setGraphReturnFromResult` 记的那个）经 `originOf` 的来源——

- 返回的是本图的形参 → `Same`（第 i 个）或 `View`；
- 返回的是本图新造的值 → `Fresh`；
- 说不清 → `Unknown`。

对 Camel 函数这是**推断**，不写语法；对无体原生声明沿用 C++ 的 `ResultAlias`（`resource_semantics.h:125-140 # "struct ResultAlias {"`），写不出新语法。这样 `func identity(s: S): S { return s }` 是 `Same`，`open` 是 `Fresh`，不会再一律 `Unknown`。这是 Q4 的建议答案。

### 3.4 `Stream` 的第一个落地

`docs/design/io-concurrency-streams/README.cn.md` 第 3.1 节已经定了 `Stream<T>` 与 `next(s)` 的写法（`next` 写 `uses s`），第 7 节定了句柄决议闭环。H7 只做语言与图这一层：`Stream` 声明加 `#resource`，`next` 写 `uses s`；运行期内核（文件/网络）按该设计稿另行落地。测试用一个最小的内存流（`open`/`next`/`close` 三个算子或一个 C++ 测试模块）就能锁住顺序与并发，不依赖真实 I/O。

---

## 4. `pure` 与推断

### 4.1 未知算子的默认

今天已经是 3.3 要的默认：没有 `declare` 的算子 `isPure=false`、`hasExternalEffect=true`（`src/core/resource_semantics.cpp:91-101 # "conservativeResources("` 给 Unknown 类、碰每个操作数、external），因此它与一切副作用冲突。缺的不是模型，是**建图时没用它**（步骤 6）。`uses`/`pure` 只是把这个默认变成可声明的：写了 `pure` 的算子不再保守，写了 `uses fs` 的只碰 `fs`。第三方漏写仍是"全局资源"。

### 4.2 推断能得出什么、不能得出什么

**能**（全部由 `FootprintWalk` 已有的规则得出，`resources.cpp:212-278 # "GraphFootprint FootprintWalk::of(const GCGraph *graph) {"`）：

- 被调用者资源的并集；递归的最小不动点（`:174-182`）；
- 形参 → `Norm`/`With` 位，捕获 → `Capture` 位，环境 → 名字；
- 本图新造、只在本图用的对象**丢弃**（`:262-263`，"made by this activation: invisible to its caller"）：一个自己开流自己用的函数对调用者没有句柄资源，但它的环境效应（打开文件用的 `fs`）仍在；
- 来源说不清 → `anyOf`，即"全局"。

**不能**：

- 不能推断算子的纯度。算子的效应只能由它的声明（C++ 语义记录或 `.cml` 的 `uses`/`pure`）给出；没声明就是有副作用。写 `pure` 的函数体内出现这种算子，`pure` 检查失败，而不是"猜它纯"。
- 不能证明两个 `Port` 句柄不同（见 2.4）。
- 不能跨函数重排：调用点就是一个副作用，函数体内的顺序不因调用者而变。
- 不能把"导出的资源集"在两次编译之间自动比较（没有基线，见 Q2）。

### 4.3 `pure` 怎么查

- 带函数体的声明：推断集为空才通过；`anyOf` 非空、任一环境名、任一形参位、任一捕获位都算"用了资源"。
- `var` 形参 = 资源，所以 `pure` 与 `var` 形参同时出现必然失败（`UsesPromiseViolated`，附注指向那个形参）。
- 无体原生声明：`pure` 就是它的声明；`--verify-ops` 检查它与 C++ 的 `OperatorPurity`/`ResourceSpec` 一致。
- 函数类型上的 `pure`：是**要求**。把非纯函数值传给要求 `pure` 的位置，按 2.2 的子类型规则报错。

### 4.4 第三方漏声明

`warnUndeclaredOperatorTraits`（`introspect.cpp:211-236 # "size_t warnUndeclaredOperatorTraits(const Module &module) {"`）现在是启动日志，不是诊断。步骤 10 改成一条 **Hint**（`UndeclaredOperatorEffect`，`0x3001A0`，无源位置，模块级），内容列出模块名与前 8 个 URI，说明"按使用全局资源对待；在模块的声明里写 `pure` 或 `uses`"。它可被 `-W` 过滤，`--explain` 能查。不是错误，符合 3.3 的"给提示"。

仓库自己的 300 余个算子由 `purity_declared` 与 `--verify-ops` 守住，不出提示。

---

## 5. 图上的后果

### 5.1 今天

- `sync` 块：语句串成一条控制链（`builder.cpp:820-954`），块的值经 `GATE` 交出去（`:945-951`）；纯计算也在链上。
- `sync` 之外：读排在最后写者之后（`orderAfterLastWriter`，`builder_expr.cpp:433-439 # "void Builder::orderAfterLastWriter(node_handle_t variable,"`）；写只在自己被 `wait` 时排在之前使用者之后（`:381-402`）；环境资源与外部效应之间没有边，不同变量的副作用之间也没有保证。
- `core.effects` 与 `core.resources` 是按需计算的**事实**（`core_facts.cpp:306 # "k.effects = registerKind(KindSpec<EffectSummary>{"`、`:343`），不是存的边或属性；`std::agir` 把非纯 OPER 标成 `role=effect`（`node_roles.cpp:23-47 # "effect (ordered, never folded or removed)"`）。
- `std::prl` 的任务图只由 norm/with/ctrl 边生成（`src/execute/pass.cpp:847` 起；`src/passes/sched/taskflow/`）。**没有 ctrl 路径的两个节点就是可并发的任务。**

所以"3.3 的执行顺序是图的属性"这句话今天只对 `sync` 成立；H7 要让它对**每个资源**成立。

按规则看图上到底变了什么（没有新节点种类、没有新边种类、没有新属性种类）：

| 规则 | 节点 | 边 | 属性/事实 |
|---|---|---|---|
| `uses` 写在签名上 | 不变（还是 CALL/FUNC/OPER） | 句柄/变量本来就是普通输入边；**新增**同资源的 ctrl | `core.resources` 的事实里多出"类 + 名字 + 绑定到哪个输入" |
| `pure` | 不变 | **不加** ctrl；无 access | 该节点不碰资源，后续优化可 CSE/折叠（H91 的限制不变） |
| 句柄类型 | 不变 | 句柄的值边（已有）就是身份；ctrl 按值键 | 资源类 `Handle` + 声明限定名；无资源 id 属性 |
| 无序资源 | 不变 | **不因它加** ctrl（`sync` 块内仍由块链加） | 声明上的 `unordered` 标志 |
| `sync` 块 | 块的值经 GATE（已有） | 既有的块内 ctrl 链，一行不改 | 无 |
| 未声明算子 | 不变 | 与一切资源冲突，因此和一切副作用连 ctrl | `anyOf`（保守全局） |

图里始终没有"资源 id"这种属性：身份由**值节点**（句柄/变量）与**名字**（环境资源）给出，控制边是唯一的图变更。这样重写、内联、常量折叠都不会让资源 id 失效。

### 5.2 草图一：`uses` 与同一环境资源

```
func save(p: Stream, x: int): unit uses fs, p { ... }
func f(): unit {
    save(a)          // n1 CALL：uses fs 与句柄 p（绑定到 a）
    println("x")     // n2 CALL：uses stdout（external）
    save(b)          // n3 CALL：uses fs 与句柄 p（绑定到 b）
}
```

```
今天：n1 n2 n3 之间只有各自的数据边，没有 ctrl。
H7：  n1 ──ctrl──▶ n3            （同一个 fs，按书写顺序）
      n1    n2    n3 之间不再有别的 ctrl（fs ≠ stdout；p 若 a,b 不同则无冲突）
```

`std::prl` 可以并发 `n2` 与 `n1`/`n3`；`n1` 与 `n3` 串行。若 `a`、`b` 是两个不同 `Fresh` 句柄，`p` 这一路也不连边。若 `a`、`b` 是同一个变量或同一句柄，`p` 这一路也连边（同一个键）。

### 5.3 草图二：两个句柄，一个串行一个并发

```
n1 CALL open("a")  → s : Stream   （result Fresh，资源类 Handle、名字 Stream）
n2 OPER next(s)    access: Handle/Stream → n1
n3 OPER next(s)    access: Handle/Stream → n1
n4 CALL open("b")  → t : Stream   （result Fresh）
n5 OPER next(t)    access: Handle/Stream → n4
```

```
今天：n2 n3 n5 之间没有 ctrl（只有 n1/n4 到它们的数据边）。
H7：  n2 ──ctrl──▶ n3            （同一个值节点：n1 或透传它的 GATE）
      n5 与 n2/n3 无 ctrl        （n4 是另一个 Fresh，不别名）
```

`std::prl` 在 `n3` 与 `n5` 之间没有路径，二者并发；`n3` 必须等 `n2`。图里没有"资源 id"这种属性：`n1` 这条普通的值边就是身份，`originOf` 判 Fresh/Port/Unknown。

### 5.4 草图三：形参可能同一对象 + 纯调用

```
func pump(s: Stream, t: Stream): unit { next(s); next(t) }
    PORT s ─▶ n1 OPER next     access: Handle/Stream → PORT s
    PORT t ─▶ n2 OPER next     access: Handle/Stream → PORT t
    mayAlias(PORT s, PORT t) = true  →  n1 ──ctrl──▶ n2

func twice(x: int): int pure { return x + x }
    n3 CALL twice   （无 access、无 ctrl；可被后续优化 CSE/折叠——
                      但 H91 规定 build 阶段不折叠全常量纯节点，H7 不改它）
```

```
pump(s, s)           调用者图里一个 CALL；被调用者内部已串行，正确。
pump(s, open("b"))   同上：被调用者不知道 s 与新句柄不同，仍串行（少优化，不算错）。
```

这条是 H7 必须写进文档的"保守边界"：**跨函数不重排**（3.3 原文），所以被调用者内部的串行不能被调用者撤销。

### 5.5 控制边怎么加、调度器为什么不用改

- 建图时对每个"碰资源"的节点，按 2.3 得到键集合；与前一个同键的节点连 ctrl（6a 先按成对冲突、6b 再按资源链稀疏化）。已有的 `shouldAddSyncCtrlLink`（`builder_helpers.h:425-434 # "inline bool shouldAddSyncCtrlLink(node_handle_t from, node_handle_t to)"`）与 `hasControlDependency` 负责去重与避免已有数据依赖时再加边；分支两臂的节点不互连（`exclusive` 的规则，`resources.cpp:680-702`）。
- `ordered by data` 的对不重复加 ctrl（数据边已经给了顺序）；`ordered by control` 的对已经有边；只有"冲突且无路径"的对才加。加边方向是书写顺序（节点创建顺序），通常不会成环；仍用 `reaches` 兜底。
- 判据的落点：`mayAlias`/`originOf` 现在只接受 `GCGraph`（`resources.cpp:566-610 # "bool Resolver::mayAlias(const GCGraph &graph, gc_node_ref_t a,"`）。建图时在草稿节点上维护同一张来源表（Fresh/Port/Same/Unknown、由哪个节点来），逐条对齐那张判决表；`requiredOrderings` 需要 `Program`，只在验收时调用，不在建图时调用。
- 调度器读的还是同三种边，**`std::taskflow`/`std::prl` 一行不改**。这是"执行顺序是图的属性"的直接结果：图的边变了，调度自然变。
- `requiredOrderings`（`resources.cpp:706-734`）继续作为独立验收：步骤 6 之后，除 `unordered` 资源上的对外，所有 `ResourceConflict` 的对都必须是 `ordered`。

---

## 6. 诊断

诊断目录的下一个空块是低 20 位的 `0x1A0`（最后一处错误是 `semantic.h:195` 的 `0x000190`，最后一处警告是 `:231` 的 `0x100160`；全目录无 `0x1A0`）。约定：错误 `0x0001A0` 起，警告 `0x1001A0` 起，提示 `0x3001A0` 起。下表写的是**枚举值**（低 24 位）；JSON 与 `--explain` 里的完整码在前面再加类型 `0x03`（例如 `0x033001A0`）。先占 6 个：

| 码 | 名字 | 严重性 | 位置 | 何时 |
|---|---|---|---|---|
| `0x0001A0` | `UnknownResource` | Error | `uses` 里的名字 | 名字既不是本函数形参、也不是可见资源（附注列出形参与资源） |
| `0x0001A1` | `UsesPromiseViolated` | Error | 声明（`uses`/`pure`） | 推断集超出 `uses`；`pure` 却非空（含 `var` 形参与捕获）；附注指向第一个超出的使用点 |
| `0x0001A2` | `HandleTypeNotNominal` | Error | `#resource` 标签 | 标在别名或内建类型上 |
| `0x0001A3` | `PureAndUses` | Error | 第二个子句 | `pure` 与 `uses` 同时写（帮助：`pure` 是 `uses` 为空的特例） |
| `0x0001A4` | `ResourceGroupCycle` | Error | 组声明 | 组展开成环（`a = [b]; b = [a]`） |
| `0x3001A0` | `UndeclaredOperatorEffect` | Hint | 模块级（无源位置） | 模块有算子既无 `pure` 也无 `uses`；按全局资源对待 |

另有一条**条件**警告，取决于 Q2：

| `0x1001A0` | `ExportedResourcesChanged` | Warn | 导出函数声明 | 导出函数推出的资源集相对基线变化；基线是什么见 Q2 |

落地：

- `semantic.h` 加枚举、`semantic.cpp` 加消息与帮助；`docs/diagnostics.md` 由 `node scripts/diagnostics-catalog.mjs --write` 生成（不要手改），`--verify-doc` 与 `--coverage` 进相应门禁。
- 每条诊断至少一个 plan 用例（`expect_diagnostic_name` + 码 + 位置），生成器才会把它排进文档；`--coverage` 报告没被任何测试提到的码。
- `--explain <name>` 自动可用（`config.cpp:476-523 # "--explain: prints the catalog entry named by"`），不需要单独接线。

---

## 7. 步骤

### 步骤 1：内部模型（无语法）

- `ResourceClass` 加 `Handle`（`resource_semantics.h:52-65 # "enum class ResourceClass : uint8_t {"`）；`checkResources` 要求它有名字；`compatible` 仍按"同类同名"。
- `GraphFootprint` 加 `result`（`ResultAlias`），`FootprintWalk` 从返回节点算（见本计划 3.3）；`Resolver::fromFootprint` 不再硬写 `Unknown`（`resources.cpp:286-287`）。
- `ResourceSignature` 的投影函数：足迹 → (params, captures, ambient, anyOf, result)。
- 测试：`modules/resources` 加一个测试模块的句柄算子与一条"返回形参/返回新值"的足迹断言；不改任何用户可见行为。

### 步骤 2：函数类型带资源（无语法）

- `FunctionType::Parts` 加签名，进 intern key/mangle/toString；`OpenFunctionType::setResources`；`equals`/`assignableFrom` 按 2.2。
- 原生声明的函数类型从 C++ 的 `ResourceSpec` 生成签名（`uses fs` 等），`inspect --ops` 的类型一栏因此可打印；`OperatorTable` 暴露"这个 URI 的签名"。
- 测试：现有 `fs:exists`、`rand:seed` 的类型打印；`func.cpp` 的 intern 相等（同一签名同一对象、`pure` 与 `Generic` 不同）。

### 步骤 3：语法

- 词法：上下文关键字；AST 节点（`ResourceDecl`、`UsesClause`）；GCT/GIR 传递；类型构造。
- `resource` 声明进模块符号表，导出/导入走既有规则；标准资源预声明。
- format 支持新子句；`docs/language/08_top_level_and_grammar.md` 由 `check-grammar.mjs --write` 重生成；`docs/Camel.asdl` 同步（`check-source-artifacts.mjs`）。
- 诊断 `UnknownResource`、`PureAndUses`、`ResourceGroupCycle`（`HandleTypeNotNominal` 随步骤 7 的标签一起）。
- 测试：parse/AST 快照、format、负例；`resource` 与同名变量的遮蔽规则各一条。

### 步骤 4：标准库迁移（逐模块）

- 每个无体声明补 `uses` 或 `pure`；`uses` 的名字用该模块的资源（`fs`、`stdout`、`rng`…）。
- `--verify-ops` 升级为双向：声明的 `uses`/`pure` 必须与 C++ 的 `ResourceSpec` 一致（不多不少；`pure` ⇔ `spec.none()` 且 traits 纯）。
- `registry.purity_declared` 改为读声明；`modules/resources` 的 `declared_resources` 从"C++ 文本"改为"声明文本"。
- 建议顺序：`fs`、`io`、`time`、`random`、`env`、`process` 先（会碰环境），再 `algo`/`str`/`json`/`math` 等纯模块。每模块一个提交，跑一次 `--verify-ops`。

### 步骤 5：推断与承诺

- 把 `FootprintWalk` 的结果在 link 后写入每个函数声明的签名（不动点，`settleEffectfulGraphs` 的位置，`builder_diagnostics.cpp:296-309 # "void Builder::settleEffectfulGraphs() {"`）；函数值转换在不动点后再校验一遍（0.2 第 1 条）。
- `uses` 是承诺：推断 ⊆ 声明，超出报 `UsesPromiseViolated`；`pure` 要求空集。
- 导出函数：文档/`inspect` 显示推出的资源；变化警告取决于 Q2。
- 测试：新 `compile/effects`：跨函数求和、递归不动点、高阶泛型回调（`map` 的 `f` 用 `fs` → `map` 的调用点带 `fs`）、`pure` 违反、`uses` 超出、未知算子导致 `anyOf`。

### 步骤 6：建图排序

- 6a：在资源不动点收敛之后、任何 pass 之前（link 的 settle 阶段），对"碰资源"的节点按节点创建顺序扫描冲突对（复用 `compatible`/`mayAlias`/`exclusive` 的判据），对冲突且无路径的对加 ctrl；`unordered` 资源跳过（步骤 8 之前没有这种资源，所以 6a 可以先行）。
- 6b：换成"每个资源键保留最后一个访问节点"的链，边数从 O(冲突对) 降到 O(访问数)；语义必须与 6a 等价，用同一组快照比对。
- `sync` 链保持不变；`wait` 仍可用（步骤 9 才删）。
- 测试：`modules/resources` 新增"去掉 `sync` 的同一程序"，断言不再有 `unordered`；GIR 快照：同 `fs` 有边、`fs` 与 `stdout` 无边、同一个 `var` 有边、不同 `var` 无边；`values.sync_order` 与 `gir.sync.ctrl_dedup_snapshot` 不变；`--jobs` 下 `std::prl` 的执行结果与逐序执行一致。

### 步骤 7：句柄类型

- `#resource` 标准标签；`isHandleType`；`Handle` 访问；`uses param` 的类由形参类型决定；逐值键（3.2）；`GraphFootprint.result` 接上（步骤 1）；`HandleTypeNotNominal`。
- `Stream` 加标签，`next` 写 `uses s`；最小内存流的三个算子作为测试模块，或直接进 stdlib 的 `stream` 模块（按 `io-concurrency-streams` 设计稿）。
- 测试：两个 Fresh 句柄并发（GIR 无边 + `std::prl` 的 rendezvous 证明并发）；同一句柄串行（golden 顺序）；`pump(s,s)` 串行、`pump(s,open())` 保守串行；同一句柄经 `let`/GATE 仍是同一键。

### 步骤 8：无序资源

- `unordered` 标志进资源声明与 `ResourceSpec`；`ResourceConflict` 仍需报告（供诊断/显示），但**建图**不为无序资源连边；`sync` 块仍按块内一切排序（3.3 原文）。
- 测试：两个 `net` 上的调用无边、可并发；放进 `sync` 后有边、按书写顺序；`uses net` 的组展开。

### 步骤 9：退役旧写法与旧诊断

- 删 `wait` 的解析与 GCT/GIR 支持；`sync` 块保留（3.4 不变）。
- 删/重写 `IgnoredSideEffect`：块外不再需要"既没 wait 也不在 sync"这条理由；`VarParamInAsyncFunction`、`CapturedWriteInAsyncFunction` 与 3.3 的"声明纯度"合并（一个函数是否有效应，由 `uses`/`pure`/`var` 形参决定，不再靠前缀 `sync`）。
- 更新 `docs/language/04_execution_model.md`；`linear/captured_writes` 的用例改为依赖自动排序；`diag_json` 的相应断言改写。
- 测试：`wait` 的负例（语法错误）；旧诊断不再出现；原来靠 `wait` 的程序不写 `wait` 也按书写顺序执行。

### 步骤 10：第三方提示与验收

- `UndeclaredOperatorEffect` Hint；`--verify-ops` 已在发布门禁（`release-gate.mjs:137 # "{ name: 'stdlib reference: docs/stdlib-reference.md is up to date',"`），迁移后必须仍绿；每个新码至少一个用例（`diagnostics-catalog --coverage` 对新码无输出；存量未覆盖的码不在本任务范围）。
- 验收矩阵：1.1 的每一行 → 本计划的步骤 → 测试；全部绿才算完成。
- 文档：本计划、`docs/design/io-concurrency-streams/README.cn.md` 的 H7 接口、`docs/design/op-semantics` 的资源声明一节（把 C++ 声明与 `.cml` 声明的关系写清）。

---

## 8. 作者问题

**Q1（句柄类型的范围）** `#resource` 是否只允许挂在名词性类型声明（`type X = { ... }` / 枚举）上，别名与内建类型一律拒绝？

- 建议：**是**。类型声明已经是名词性的（1.2），标签落在类型声明上（`declarationTags`），`isHandleType` 只查声明；别名的标签会污染目标类型，内建类型没有身份。
- 另一选择的代价：允许别名 → 同一结构类型的两个名字共用一个标签，语义说不清；允许内建 → 需要"装箱的 int"这种新身份概念，还会碰 10.5 的纯数据布局，远超 H7。

**Q2（导出资源变化警告的基线）** 3.3 说"导出函数推出的资源发生变化时给出警告"，但"变化"相对什么？

- 建议：相对**写出来的 `uses`**——推断集超出即错误（已定），推断集比 `uses` 小则警告"声明的资源已不再使用"；"相对上一次编译"的变化警告等模块缓存（`reviews/H141-H213.md` Q12）落地后再加。这样 H7 不依赖缓存也能给作者有用的信号。
- 另一选择的代价：先做缓存才能警告"变化"，H7 被缓存任务挡住；或者永远只做"超出即错误"，丢掉"声明过宽"的信号，调用者被多排序。

**Q3（函数类型对资源泛型的表示）** 未写资源子句的函数类型用"`Generic` 模式 + 调用点按实际值实例化"，还是要求每个函数类型都写资源（Koka 式行）？

- 建议：`Generic`。3.3 明确"不提供 Koka 式的行多态"；`Generic` 在调用点用实际函数值的具体签名实例化，取不到就按全局（保守）。
- 另一选择的代价：每个回调类型都要写资源行，标准库的 `map`/`filter` 等全部要改签名；与 3.3 的"靠推断而非强制标注"相反。

**Q4（返回的句柄是不是新资源）** 一个返回句柄的函数，其结果算 `Fresh`（新资源）还是随函数体/声明推断（`Same`/`View`/`MayAlias`）？

- 建议：**推断**。Camel 函数看 `return` 的值的来源（形参→`Same`，本图新造→`Fresh`，说不清→`Unknown`）；无体原生声明沿用 C++ 的 `ResultAlias`。`identity(s)` 因此是 `Same`，`open` 是 `Fresh`。不新增语法。
- 另一选择的代价：按 131 行"每个值都是一个独立的资源"字面理解成"返回即 Fresh"——`identity(s)` 会被当成新资源，两个 `next` 并发，**算错**；反过来一律 `Unknown`，所有派生流都串行，`std::prl` 的流并发就没了。

**Q5（`uses` 是约束还是发布值）** 写了 `uses` 的 Camel 函数，其类型发布的是**推断集**（`uses` 只是上界约束）还是**写出来的集合**？

- 建议：发布推断集。3.3 的"导出函数推出的资源发生变化时给出警告"只有在发布推断集时才有意义；写出来的 `uses` 是承诺上界，超出报错。
- 另一选择的代价：发布写出来的集合 → 调用者按更宽的集合排序（少优化），但签名稳定、无需变化警告；与 3.3 那句话矛盾。**Q2 与 Q5 绑定**：若 Q5 选"发布写出的集合"，Q2 的"变化警告"就没有对象，3.3 的那句话只能解释成"声明过宽时警告"。

---

## 9. 不做什么，以及完成判据

**不做**：效应处理器；Koka 式行多态；`async`/`await`；`cancellable`/取消（3.3 有，但不属于 H7 的四项，留给取消任务）；调用点别名特化；调度器改动；新的节点或边种类；`#fresh` 之类的新标签。

**完成判据**：

1. 1.1 的每一行都有"已实现"的判据与一个绿测试；没有"部分/缺"。
2. `requiredOrderings` 对每个非 `unordered` 的冲突对都报 `ordered`（`modules/resources` 的 `!out.includes(' unordered')` 在去掉 `sync` 的用例上也成立）。
3. 两个 Fresh 句柄的操作在 `std::prl` 上可并发（rendezvous 通过）；同一句柄严格按书写顺序（golden）。
4. `--verify-ops` 双向绿；`registry.purity_declared` 绿；第三方漏声明只出 Hint，不是错误。
5. `docs/diagnostics.md` 由脚本生成、`--verify-doc` 绿；每个新码至少一个用例；`--explain` 能查到。
6. `wait`、前缀 `sync func`、旧的三条警告从树与文档里干净删除（不标"已退役"）。