# 泛型实例化与重载解析代码评审

范围（只读源码，没有构建与运行；标"待验证"的是我没有确认的前提）：
- `src/compile/gir/builder_generics.cpp`：`instantiateGeneric`（1845–2191）、`chooseGenericOverload`（1034–1126）、`instantiateForCall`（1136–1548）、`instantiateGenericAs`（427–551）、`registerGenericTemplates`（257–356）。
- `src/compile/gir/builder_operators.cpp`：调用点与非泛型重载选择（2405–2640）；`builder.cpp` 的 `rejectRedeclaredFunction`、`isPassDeclaration`；`builder_when.cpp` 里与实例键有关的部分（上一份评审已覆盖其余）。
- `src/core/type/typevar.cpp`：`bindTypeVars`、`unifyTypeVars`、收集与代换函数。
- 运行期的特化：`src/runtime/draft_inline.cpp`（特化键、不动点保护）、`draft_session.cpp`（键的哈希与缓存），因为"实例的键"在编译期与运行期各有一份。

按严重程度排序：A 会静默给出错误选择或把拒绝变成崩溃；B 缓存键、重复与性能；C 函数过长与结构；D 注释。

---

## A. 错误的选择、静默的失败

### A1. 泛型重载选择完全不看 `when`，也不看 `with` 参数的类型
`builder_generics.cpp:1034-1126`（`chooseGenericOverload`）。`binds` 只比较 `with` 参数的**个数**（1044），从不绑定、检查它们的类型与名字；整个选择里没有 `when`。非泛型的重载选择（`builder_operators.cpp:2487-2640`）则做了：两遍（先原样、再字面量进入）、在选择时求 `when`（2551-2574）、记下 `unmetConditions` 在没有候选时报"条件不成立"。同一个设计原则（4.4）两套实现，泛型这一套漏掉了最主要的一块。
- **场景**：`registerGenericTemplates` 用 `rejectRedeclaredFunction`（`builder.cpp:300-355`）接受了两个签名相同、`when` 互斥的泛型函数（`sum::<T>(xs: T[..]) when is_integer::<T>()` 与 `… when is_float::<T>()`，也是 `math.cml`、`array.cml` 草稿的写法）。调用 `sum([1, 2])` 时 `binds` 对两条都为真，`fits.size() == 2`，报 `AmbiguousCall`——声明处允许、调用处拒绝。只有一个候选时（1095-1097）直接返回它，再由 `instantiateForCall` 的末尾（1521-1544）报"条件不成立"，所以**写出一个 `when` 能用，写出两个互斥的就不能用**。
- 另一个同类场景：两个只在 `with` 参数类型上不同的泛型重载（`f<w: int>(x: T)` 与 `f<w: string>(x: T)`）同样两条都 fit。
- **修**：把泛型候选并入同一个解析引擎（见 B4）：对每个候选先绑定类型变量（`with` 与普通参数一起），代入后求 `when`（带 `withValues`，传 `tmpl` 作 `declaring`），`Fails` 的记入 `unmetConditions`，最后才数 `fits`。

### A2. 一组重载的泛型名字取第一个：泛型函数作实参
`builder_generics.cpp:1243-1245`：`waiting[i] = generic->second.one()`；`generic_ref_t::one()` 对一组重载返回 `group_->front()`（`builder.h:100-103`）。`instantiateGenericAs`（1392）因此只拿第一个重载去试，后面的从不被看到。
- **场景**：`xs->map<show>`，`show` 有 `show::<T>(x: T[])` 与 `show::<T>(x: T)` 两个泛型重载；期望的函数类型 `(x: int) => string` 只有第二个能满足，第一个因 `unifyTypeVars` 失败返回"does not have the form"，调用报 `GenericArgumentsMismatch`，而第二个存在且合适。
- `builder_when.cpp` 的同类用法上一份评审已列（A2）。
- **修**：`waiting` 存整组；步骤 5 对组内每个候选各试一次 `instantiateGenericAs`，恰好一个成功就取它，多个成功报歧义，都失败则合并各自的 `why`。

### A3. 失败的实例被缓存，但它的诊断可能从未显示过
`builder_generics.cpp:1904-1912`、`2071`、`2172-2175`。实例体建失败时 `instance.failed = true`；之后再要同一个实例就 `throw BuildAbortException()`——不报任何东西，理由是"第一次已经报过了"（1905-1907 的注释）。但 `BodyDiagnostics bodyDiags(*this)`（2071）在 `speculative_ != nullptr` 时把体里的诊断**扣在 sink 里**，`speculative_` 为真时又 `return nullptr`（2172）。
- **场景**：重载选择（或泛型实参的推断，步骤 5）在一个 `SpeculativeInstantiation` 里试了候选 `g::<int>`，体里有错，诊断进 sink 被丢弃，实例标 `failed`；随后程序里一处普通调用 `g(1)` 选中同一个实例，走到 1911：`BuildAbortException`，**零条诊断**，编译失败却没有错误信息。
- **修**：`GenericInstance` 存下它建失败时的诊断（`diags_->` 里 `diagsBefore` 之后的那一段）；非推测地再次用到它时把这些诊断重放一次，再抛。至少在 1911 前断言"已报过"，别静默。

### A4. 选择阶段与实例化阶段对字面量的处理不同
`chooseGenericOverload::binds`（1056-1090）对所有实参一视同仁地 `bindTypeVars`，再对每一个用 `entersImplicitly(wanted, actual)` 判断（`actual` 是字面量的**默认类型**，如 `int`）。`instantiateForCall` 则明确把字面量放在最后（1285-1304、1323-1367）：位置的类型先说话，再让字面量进入。
- **场景**：`keep::<T>(x: T, y: T)`，调用 `keep(a, 1)`，`a: f32`。只有一个候选时走 `instantiateForCall`，`T = f32`，`1` 进入 `f32`，通过；再多声明一个同名泛型重载，`binds` 里 `T = f32` 之后 `entersImplicitly(f32, int)` 为假（`actual` 是 `int` 的类型，不知道它是字面量），候选被拒，`NoMatchingFunction`。**加一个重载会让原来能编译的调用失败**。`entersImplicitly` 对 `int → f32` 的行为待验证，但 `binds` 不传字面量这件事是确定的（`normInputs[at].expr` 只用于复合字面量，1085-1088）。
- **修**：同 A1：一个解析引擎，字面量规则只写一处。

### A5. 优化失败变成崩溃
`src/runtime/draft_inline.cpp:1038-1056`。类型特化走进调用环、实例数超过 `kTypeSpecializationFixedPointGuard` 时 `throw std::runtime_error(...)`。特化是优化，不特化程序仍然正确；这里把"优化没做完"变成一个未处理的 `std::runtime_error`（pass 的调用方不一定接得住），对应的值特化超出预算（`engine.cpp:850`）则是安静地停止。同一类情况两种处理，更糟的那种在更常见的类型特化上。
- **修**：到上限就保持当前被调用者（不特化），并用 `CAMEL_LOG` 或遍的报告记一笔；计数用的是整个根的特化总数而不是这一条环，也需要重新考虑（一个在多处被用到的泛型函数会耗尽整个预算）。

### A6. 凭 `when` 的**文本**判定一个声明是遍
`builder.cpp:562-585`（`isPassDeclaration`）：去掉空白后用 `rfind("is_function::<", 0)` 与结尾 `>()` 比较 `when->text`。
- **场景**：一个 C++ 模块的无函数体声明恰好写了 `when is_function::<F>()`（比如要求一个回调参数的算子，这是 `when` 的合法写法），被当成遍的声明，`unit_->passDeclarations.insert(...)`，不登记算子——调用时是"没有这个名字"，原因一个字都看不出来。判断的是书写，不是语义，`when is_function::<F>() && …` 或加括号都逃过去。
- **修**：遍的声明要有显式标记（它的返回类型形状 `(f: F): F!` 才是遍的本质；或者一个 `#pass` 标签），不要从条件文本里猜。

### A7. `instantiateForCall` 里一个会抛 `std::out_of_range` 的 `.at()`
`builder_generics.cpp:1494`：`ConstasValue &value = constas.at(name)`。`takeConstasValues`（893）是否对每个 `constas` 形参都放入了条目、在失败路径上是否会提前返回，我没有读（待验证）；如果有任何分支漏填，这里是裸的 C++ 异常，不是诊断。**修**：`find` 加 `ASSERT`/诊断。

---

## B. 缓存键、重复与性能

### B1. 实例键取决于调用者有没有把 `outerBindings` 一起传进来
`instantiateGeneric` 的 `sameBindings` 比较**大小**再逐名比（1860-1871）。`instantiateForCall` 与 `instantiateGenericAs` 传的是只含本函数类型参数的绑定（1279、470-），`builder_when.cpp:1303-1306` 与 `1672-1693` 传的是 `tmpl->outerBindings` 加上类型参数。体里又把 `outerBindings` 再拼上 `bindings`（2099-2100）。
- **场景**：定义在泛型函数体里的局部泛型函数（`outerBindings` 非空）：从体里普通调用得到一个实例，从另一个函数的 `when` 里求谓词又得到第二个同样类型的实例——两个图，两份诊断，名字一样。顶层泛型的 `outerBindings` 为空，所以日常看不出来。
- **修**：键只由模板自己的类型参数名决定（取 `typeParameterNames(tmpl)` 对应的绑定），不比较大小；调用者传什么都规范化成这个。

### B2. 两处线性扫描
- `instantiateGeneric`：每次调用线性扫描 `tmpl->instances`（1901），每个元素还做 `sameBindings` + `extents ==` + `sameConstas`。一个被很多类型用到的泛型函数（`map`、`sum`）是 O(实例数) 每调用。用一个规范键字符串（类型的规范 id + constas 的 `constasKeyOf`）做哈希表即可。
- `draft_inline.cpp:520-540`（`onCallCycle`）对被调用者做整个可达图的深度优先，每处 `specializeDirectFuncInDraft` 调用两次（`:669`、`:1034`），每个调用节点各一遍。N 个调用节点对 M 个图是 O(N·M)。按图缓存一次强连通分量即可。

### B3. 运行期特化键里放的是 GC 对象的指针位
`draft_inline.cpp:708-722`（`makeRuntimeSpecializationKey`）：值绑定的键是 `binding.staticValue.value`，一个原始 `slot_t`。`draft_session.cpp:1165-1177` 对 GC 管理的值已经承认它不能进程序级缓存（`specializationLivesInProgram`），但会话内的 `specializationCache_` 还是用同一个键。
- **场景**：字符串/数组常量作特化值，两个相等的字符串在不同地址 → 两个键，同一内容特化两份（浪费预算）；更危险的是，对象被移动、地址被另一个对象复用，会话内缓存得到**别的值**的特化（会话是否在 GC 点重定位键，`relocateStaticSlot` 只重定位了草稿的静态槽，我没有看到缓存键被改写，待验证）。
- **修**：值绑定只对不含 GC 对象的类型（数字、布尔）做键；其余按内容规范化（字符串内容的散列加相等比较）。

### B4. 三个重载解析引擎，各写一遍
| 引擎 | 位置 | 特点 |
|---|---|---|
| 非泛型函数重载 | `builder_operators.cpp:2487-2640` | 两遍、`when`、`unmetConditions`、歧义 |
| 泛型函数重载 | `builder_generics.cpp:1034-1126` | 一遍、没有 `when`、没有 `with` 类型 |
| 运算符重载 | `chooseOperatorOverload`、`operatorTemplates_`（`builder_operators.cpp`，我没有逐行读） | 各层合并 |
还有 `partialApplicationOf`（注释里说"同样的歧义报告"）。设计原则 4.4/5.1 只有一套规则；四处实现的差别（A1、A4）全是缺陷。建议一个 `OverloadResolver`：输入候选（带 `FunctionType`、可选的模板与 `when`）、实参（类型、字面量表达式、`with` 值），输出选中者或统一格式的失败（歧义、无匹配、条件不成立）。

### B5. `bindTypeVars` 与 `unifyTypeVars` 是同一个递归的两份拷贝
`typevar.cpp:641-757` 与 `810-933`：结构（数组、流、元组、结构体、函数、模块类型）一模一样，差别只在三处：`bind` 只绑 `binder()` 变量而 `unify` 绑任何变量（810-816 与 641-648）；`unify` 会用资源子句更弱的类型**原地改写**已有绑定（820-829）；`unify` 不短路在"变量都已解"（`anySolutionWritesResourceClause`）。再加 `collectTypeVars`、`collectBindableTypeVars`、`substituteTypeVars`、`substituteExtentValues`、`findRepeatedMark`、`containsTypeVar`——至少七个函数各自重复这一个 switch。
- **风险**：`unify` 不看 `binder()`，会替**不是本函数头声明**的变量绑定（外层泛型体里引用的外层变量名）；`bind`/`unify` 对"同一个变量遇到两个不同类型"都静默返回 `true`（第一个赢），冲突要等 `entersImplicitly` 才发现，错误信息因此是"需要那种形式"而不是"T 同时是 X 和 Y"。`unify` 的改写（820-829）让结果依赖实参顺序之外的东西：步骤 1 用 `bindEntering`（`bind`，不改写）、步骤 2 才 `unify`（会改写），步骤 1 里已按旧绑定做过的判断不会重做（待验证）。
- 对未处理的类型种类（`EnumType` 是 `OtherType`，需要 `typeArguments()` 为真且 `paramCount() > 0` 才下降，其余落到 `return true`）静默不绑定，后果是"类型变量无法确定"，原因在这里。
- **修**：一个"同构遍历"辅助（`forEachChildPair(pattern, actual, f)`），`bind`、`unify` 只写叶子处的差别。

### B6. 内部作用域名里的指针
`builder_generics.cpp:2155`：`std::format("{}@{:p}", instanceName, static_cast<void *>(tmpl.get()))`。名字作作用域键，每次运行不同；这个名字是否会出现在诊断、`std::gir` 输出、模块缓存里待验证——只要出现一次就破坏"重复运行比对"（红线 6）。**修**：用模板在其作用域里的序号。

### B7. 失败的实例留下半成品图
`instantiateGeneric` 先 `createCompileGraph(..., currGraph_, instanceName)`（2111-2113，把图挂在父图下）并 `tmpl->instances.push_back`（2125），之后体失败：`markFailed` 只设标志（2078-2086），图不删除、不摘下。之后 `std::gir`、导出、遍会看到一个没有返回节点的实例图（`bodyStates_` 里也留着 `Open`——`BuildState` 的析构不改它；待验证）。**修**：失败时把图从父图的依赖里摘掉，`bodyStates_` 置 `Failed`；或者在导出入口显式跳过 `failed` 的实例。

---

## C. 函数过长与结构

- `instantiateForCall`（1136–1548）413 行，一个函数里依次：参数个数校验与友好提示、`constas` 取值、五步类型变量求解、泛型实参的递归实例化、未确定变量报告、逐个实参的字面量进入、`constas` 类型检查、`when`、实例化。内部有 12 个 lambda。建议拆为：`checkArity`、`solveTypeVariables`（步骤 1-4）、`resolveGenericArguments`（步骤 5）、`enterArguments`、`checkWhen`、`finishInstance`。
- `instantiateGeneric`（1845–2191）347 行，里面还定义了一个 35 行的 `struct BuildState`、两个比较 lambda、两个名字拼接块。`BuildState` 应是类级别的 RAII（`InstanceBuildScope`），比较与命名各出一个函数。
- `resolveGenericCall`（1128-1134）是 `instantiateForCall` 的纯转发，删掉一层。
- 同文件里还混着规则模板（`instantiateRuleTemplates`、`ruleConditionVerdict`、`settleGenericRuleMaker`），文件 2235 行；规则与函数的泛型实例化是两件事。

---

## D. 注释

- 叙述历史或引用评审条目而不是写现在的规则：`builder_generics.cpp:1321`"as they always have"，`1343-1344`"(…the rule on `expected-type-through-literals-and-calls`)"，`2171`"as it always did"；`builder_operators.cpp` 约 2465-2470 的"(\n on `generic-rule-instantiation-set-and-predicate`, §3)"，括号没有闭合，引用了一份评审页而不是说明行为；`draft_inline.cpp:1025-1031`"the eight-copy count this replaces used to keep the current callee instead, which silently truncated the shape"。这些应改成"现在的规则是什么、为什么"，历史在提交说明里。
- `builder_generics.cpp:1905-1907` 的注释断言"第一次已经报过了"，而这在 A3 里并不成立；改代码之前先改注释，别让它继续替错误担保。
- `bindTypeVars` 的 `Nothing to bind below: whether the argument fits is the ordinary check's question.`（`typevar.cpp:651-652`）：这是一个隐含的契约（调用方必须再检查一次），在 `chooseGenericOverload::binds` 里靠 `entersImplicitly` 满足、在 `instantiateGenericAs` 里靠 `equals`——应在函数文档里写明。

---

## 建议的处理顺序

1. A3（静默的零诊断失败）、A1（互斥 `when` 的泛型重载在调用处被拒）、A5（优化变崩溃）：各一个负例用例，修改都不大。
2. A2、A4、A6、A7。
3. B1、B3（键）与 B2（性能）。
4. B4/B5 的合并是一次重构，建议先有 A1-A4 的用例把当前行为钉住，再合并。
5. B6、B7 与 C、D 随手清理。
