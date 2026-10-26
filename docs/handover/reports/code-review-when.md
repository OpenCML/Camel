# `builder_when.cpp` 代码评审：when 条件与编译期求值路径

范围：`src/compile/gir/builder_when.cpp`（2078 行）全文——`GctConstantEvaluator`（按 GCT 求值的纯函数解释器，207–846 行）、`ConditionContext`、谓词名字解析（941–1100）、`evaluateWhenCondition`（1102–1510）、`foldConstantCall` / `evaluateCallAtBuildTime` / `evaluateCallInsideBody`（1539–1960）、两个报告函数。只读源码，没有构建与运行；下面每条的"场景"是按代码推出来的，标注"待验证"的是我没有确认的前提。

按严重程度排序：A 是会静默给错答案的正确性问题，B 是诊断错误与行为缺口，C 是重复与结构，D 是注释与小处。

---

## A. 静默给出错误答案

### A1. `match type` 的被匹配类型仍含类型变量时，被当作叶子，选中 `_` 臂
`builder_when.cpp:392-401`（`evalTypeMatch`）。`subject = bound(...)` 之后只按 `Struct` / `Tuple` 分形状，其余一律 `Leaf`。如果 `subject` 仍是 `TypeVar`（外层绑定缺一个名字）或含类型变量的结构，`Leaf` 位与 `Wildcard` 都会让 `case _ =>` 臂被选中，答案是"不是"，而不是"还不知道"。
- **场景**：谓词 `is_numeric::<T>()` 被一个 `T` 尚未确定的泛型体里的 `when` 求值；外层 `evaluateWhenCondition` 在 1280–1287 只检查了**直接**的类型实参，经 `vars` 嵌套传下来的（`every_field_plain::<T>` → `is_plain::<F>()`，1686 行对嵌套调用做了检查，但 `match type T` 的主体 `T` 是 `bound(node->at(0)->dataType())`，没有检查）会带着类型变量走到这里。结果是条件变成 `false`，声明被判为"不适用"，而不是报 `TypeNotConcrete`。
- **修**：`evalTypeMatch` 开头：`if (containsTypeVar(subject)) fail(EvalProblem::TypeNotConcrete, …)`（新增一个枚举值，映射到 `WhenOutcome::TypeNotConcrete` / `FoldRefusal::NotEvaluable`）。对结构体与元组还要逐字段递归检查，或至少拒绝 `subject` 本身含类型变量。

### A2. 重载的泛型名字静默取第一个重载
`builder_when.cpp:1005`、`1044`、`1669-1670`。`generic_ref_t::one()` 的实现是"一个重载的名字对应的模板，**或一组里的第一个**"（`builder.h:100-103`）。谓词名字解析与折叠调用都直接用它。
- **场景**：一个模块里有两个同名的泛型函数 `f::<T>(x: T[])` 与 `f::<T>(x: T)`；`f::<int>(xs)` 出现在 `when` 里或被折叠为编译期常量，两处都取 `group_->front()`，求的是声明顺序里第一个，可能不是调用选中的那个。折叠还会按它的签名统一类型参数（1705、1714），统一失败只报"type parameters cannot be read"，用户看不出是选错了重载。
- **修**：谓词名字必须唯一，组里多于一个时报 `PredicateAmbiguous`（已有这个枚举值，但只用于跨导入）；折叠路径要么按实参类型走正常的重载选择，要么在 `group().size() > 1` 时 `NotAProgramCall: "several declarations of that name"`（1764 行的非泛型分支已经这样做了，泛型分支漏了）。

### A3. 被调用声明的 `when` 在**调用方**的作用域里求值
`builder_when.cpp:1847-1856`。`evaluateWhenCondition(when->condition, signature, bindings, withValues)` 没有传 `declaring` 和 `contextUnit`，`ConditionContext` 因此什么都不换（871–876 行：`declaring == nullptr` 且 `target == nullptr` 时直接返回）。文件头注释（19–23 行）规定条件要在**声明所在的模块**里解析名字，1328–1334 行的长注释也在讲同一件事——这一处调用恰好没有遵守。
- **场景**：模块 `a` 有 `func g::<T>(x: T): bool when is_numeric::<T>() { … }`，`a` 导入了 `types`；程序 `main` 没有导入 `types` 却写了常量调用 `g::<int>(1)` 要折叠。`is_numeric` 在 `main` 的 `templateScope_` 里找不到，条件求值报"未导入"或，更糟，如果 `main` 里恰好有一个同名的本地 `is_numeric`，用了错的那一个。
- **修**：模板分支里传 `tmpl`（`evaluateWhenCondition(…, tmpl, nullptr)`）；非模板分支传该函数所在的 `ModuleUnit`（`unitOfFunction(graph)` 已经有了）。
- **同一处还有**：条件不成立时只留下 `"…not shown to hold here"`（1853），把 `condition.detail`、`condition.subject` 丢掉了；用户拿不到"为什么不成立"。至少拼进 `reason`。

### A4. `buildFirst` 的 `catch (...)` 吞掉一切，`catch (Diagnostic&)` 之后继续折叠
`builder_when.cpp:1744-1756`。
- `catch (...)` 会连 `BuildAbortException`（2027、2075 行抛出的就是它）、断言、`std::bad_alloc` 一起吞，然后报告"its instance does not build"。`BuildAbortException` 的意思是"诊断已经提交，整个构建要中止"；吞掉它之后调用方继续走"普通构建"，同一个错误会被再报一次，或者构建带着一个已经提交的错误继续往下走。
- `catch (Diagnostic &)`（1747）把诊断加进去之后**不返回**，继续去读这个函数体并求值——对一个已知有错的函数体算出一个常量，错误被折叠掉了一半。
- **修**：`catch (const BuildAbortException &) { throw; }`；`Diagnostic` 分支加 `diags_->add` 之后返回 `NotAProgramCall`；删掉 `catch (...)`，没有"任何异常都是正常情况"的理由。非泛型分支的 `ensureFunctionBuilt`（1805）也要用同一套规则。

---

## B. 诊断错误与行为缺口

### B1. "读了一个不是常量的名字"被归成 `Effects`，用户看到的是"有副作用"
`builder_when.cpp:325-327`（`DREF` 未解析）、`547-549`（逻辑运算左侧不是 bool）。`EvalProblem::Effects` 在 1897-1899 行映射到 `FoldRefusal::HasEffects`，再到 `SemanticDiag::FoldCallHasEffects`。一个纯函数读了顶层 `let` 或写错了名字，得到的诊断是"带副作用，不能折叠"，真实原因（"读了 `n`，它不是编译期常量"）只在 `reason` 里。该用 `ArgumentNotConstant`（DREF）或 `Construct`（非 bool）。**修**：改枚举，补一个 `NameNotConstant`。

### B2. 失败的种类在三处被丢弃或收窄
- `foldConstantCall` 的 `catch (const EvalFailure&)`（1594-1600）把**所有**失败（含 `Construct`、`TooDeep`）都记成 `ArgumentNotConstant`。一个参数表达式里用了求值器读不了的构造，用户看到"参数不是编译期常量"。
- `evaluateWhenCondition` 的两处 `catch`（1356-1361、1451-1456）只留 `failure.why`，`kind` 与 `call` 丢了；嵌套调用的名字不进报告。
- 三份互逆的映射表：`EvalProblem→FoldRefusal`（1893-1909）、`FoldRefusal→EvalProblem`（1935-1954）、上面的"全部变 `ArgumentNotConstant`"。
- **修**：`EvalFailure` 与 `FoldedCall` 合并成一个类型（kind + call + why），只写一份转换；`WhenResult` 带上 `kind`。

### B3. `with` 参数的值进不了谓词与比较的实参
`builder_when.cpp:1291-1301`、`1425-1436`。实参必须是 `DATA` 节点；`withValues` 里已经有的值（1473）只在整个条件**就是**那个 `with` 参数时用（1462-1494）。
- **场景**：`when n > 3`（`n` 是 `with <n: int>`）。`__gt__` 的实参里 `n` 是 `DREF`，不是 `DATA`，得到"the call gives a value that is not a compile-time constant"，但调用已经给了 `n` 的值。`when is_wide::<T>(w)` 同理。
- **修**：实参求值走 `GctConstantEvaluator` 并把 `withValues` 作为 `dataResolver`（求值器已经支持，1338 行传了 `nullptr`），而不是要求字面 `DATA`。同时让 `1 + 2` 这样的常量算式也能做实参（现在不行）。

### B4. 未解析的 `fields T` 主体给出误导的消息
`builder_when.cpp:462-467`。`subject` 还是类型变量时报"it walks the fields of a type that is not a struct or a tuple"。真实情况是"类型还不知道"，应和 A1 同样走 `TypeNotConcrete`。

### B5. 轮次里"没有类型"被静默跳过绑定
`builder_when.cpp:503`、`532`、`818`：`if (!typeName.empty() && type != nullptr) typeEnv_[typeName] = type;`。`indices` 遍历里元素为 `nullptr` 时（458 行 `element != nullptr ? element->type() : nullptr`），`F` 不绑定，循环体里的 `F` 解析成**外层同名的绑定**或保持未绑定。静默。**修**：`type == nullptr` 且 `typeName` 非空时 `fail(Construct, …)`。

### B6. 求值器比语言宽松
`builder_when.cpp:612-635`、`681-691`。`literalOf` 把 `i32`、`i64`、`f32`、`f64` 统一成 `literal::Value`，比较与算术可以混合数值类型（`1 < 2.0`、`int + float`），结果类型取**左**操作数的（681 行）。语言规定数值之间不做隐式转换（设计原则 5.1）；这里是"编译期比运行时更宽松"，一个在运行时被拒绝的写法在条件里会被算出一个值。同时 `__eq__` 用 `args[0]->equals(args[1])`（593），不同类型的数据相等性是什么**待验证**。**修**：两边类型不同直接 `fail(Construct, "operands of different types")`。
- 同一函数的 `__add__` 在 638-639 行没有检查 `args.size() >= 2` 就取 `args[1]`；一元的 `__add__`（不会由语法产生，但 `evalCall` 的分派不挡）越界。

### B7. 谓词的实例被**先建成图再求值**，异常处理与别处不一致
`builder_when.cpp:1307-1317`。`instantiateGeneric` 先为谓词建图，再用 `constantOfNode` 看结果是不是常量，不是再回头用 GCT 解释。这条路上没有 `try`：谓词体建图失败时，异常（或 `BuildAbortException`）直接穿过 `evaluateWhenCondition` 到调用方；而同一个函数 1356 行对 GCT 路径、`foldConstantCall` 对 `instantiateGeneric` 又各有不同的 `catch`。三套策略。另外每求值一次条件都为谓词建一个图实例，即使 GCT 解释就能回答（`match type` 谓词总是能）。
**修**：先试 GCT 解释，不行再建图（或者只保留 GCT 解释，删掉 `constantOfNode` 这条路；两个求值引擎答同一个问题，行为会分叉）。

### B8. 诊断顺序不确定
`builder_when.cpp:1083-1098`（`whenPredicateElsewhere`）遍历 `units_`，类型是 `unordered_map<const Module*, …>`（`builder.h:400`），以指针为键。两个模块都导出同名谓词时，报告里指名的是哪一个由地址决定，逐次运行可能不同。红线 6 要求结果确定；诊断的文本进金标。**修**：收集所有候选，按模块名排序后全列出（`PredicateAmbiguous` 的格式已经有）。

---

## C. 重复与结构

### C1. 过长的函数
- `evaluateWhenCondition`（1102-1510，≈410 行）是一个函数里套一个 `std::function` 递归 lambda，里面又是一个 ≈150 行的 `case LINK`。建议拆为：`whenFromBool`、`whenOfBranch`、`whenOfPredicateCall`（`TAPP` 分支）、`whenOfOperatorCall`（`DREF` 分支）、`whenOfParameter`，lambda 改成成员函数。
- `evaluateCallAtBuildTime`（1604-1926，≈320 行）依次做：内建运算符、泛型/非泛型的名字解析、类型实参统一、构建实例、纯度检查、`when`、结果类型、求值、失败映射。每一段都以 `result.refusal = …; result.reason = …; return result;` 结尾共三行，出现了 25 次以上。建议 `return refuse(FoldRefusal::X, "…")` 一个辅助函数，再按上面的段落拆开。
- `GctConstantEvaluator::eval` 本身尚可，但 `evalBuiltinCall`（587-696）是一串 `if (name == …)`，可以改成按名字的表。

### C2. 重复
| 重复的东西 | 位置 | 做法 |
|---|---|---|
| `templateOf` lambda 一模一样两份 | 946-951、995-1000 | 提成文件内的函数 |
| "bool → `Holds`/`Fails`"八次 | 1149、1320、1353、1394、1444、1484 等 | `WhenResult::fromBool(bool)` |
| `boolOf`（1128）与 `asBool`（134） | | 删一个 |
| 内建运算符名字集合两份 | 201-205 与 706-708 | `evalCall` 直接调 `isBuiltinOperatorName` |
| 轮次循环的序言（`saved`/`savedTypes`/`bindRoundValue`/`typeEnv_[…] = type`/复位）三份 | 492-511、513-540、813-826 | 抽 `withRounds(node, body)`，一个 RAII 守卫负责复位（见 C3） |
| `EvalFailure`/`EvalProblem` 与 `FoldedCall`/`FoldRefusal` 与 `WhenResult`/`WhenOutcome` 三套互相翻译的"失败"类型 | 见 B2 | 合并 |

### C3. 异常之后环境没有复位
`builder_when.cpp:498-509`、`526-539`、`813-825`：`env_` 与 `typeEnv_` 在循环前保存、循环后恢复，中间任何异常（`EvalFailure`、`EarlyReturn`）都穿过去而不复位。现在求值器在失败后就被丢弃，所以没有表现出来；但 `EXEC` 的 `execDepth_`（343-357）同样只在 `EarlyReturn` 时递减。任何以后想"失败后继续求值"的改动（比如收集所有失败）都会踩到。**修**：RAII 守卫。

### C4. 性能
`env_ = saved` 每一轮复制整张 `unordered_map`（496、501、529、816），`rounds × env` 次；`evalStaticAccess` 对 `SCOMP` 目标每次访问都重算整个推导式（732），`fields T` 上每个字段访问一次，二次方。字段很多的类型（几十个字段的参数树）会明显变慢。**修**：用作用域栈（进入轮次压一层，退出弹出）；推导式结果按节点缓存（`composites_` 已经有，查到就别再 `eval(target)`）。

---

## D. 注释与小处

- **注释与代码不符**：340-342 行"its `return` gives the block's value and the statements after it still run"，而 345-355 行遇到 `EarlyReturn` 立即返回块的值，块里 `return` 之后的语句**不**运行。代码是对的（`return` 是块的值，不离开外层的块），注释要改成"`return` 结束这个块并给出它的值"。
- **孤立的文档注释**：75-82 行一段 `///` 讲"一个条件可以调用库函数……"，后面紧跟的是 84 行另一段 `///` 给 `EvalProblem`——第一段不属于任何声明，应并入文件头或删去。
- **叙述历史的注释**：1233 行"Every predicate is a function of the library **now**"；1323-1334 行整段在讲一个已经修好的缺陷（"refuses it as 'not a generic declaration' -- a scope, not a missing capability"）；这些应当只写现在的规则：条件在声明所在的模块里解析（并且这是 A3 里没有被遵守的那条）。
- 13 行缺一个空格；1232 行缩进错误（左顶格）。
- 1877 行 `const EvalDataResolver dataResolver = nullptr;` 加三行注释，是想说明"不解析名字"；直接传 `nullptr` 并把那段注释放在 `runFunction` 的调用前。
- `EvalProblem`、`WhenOutcome::UnknownName` 等枚举值缺少逐个的说明；`kMaxFoldedCallDepth`=64 与 `holdsCallCandidate` 的 `depth > 64` 是两个互不相关的 64。
- `ConditionContext` 里的 `const_cast<ModuleUnit *>(contextUnit)`（862）：`contextUnit` 参数该是非 const，或者 `self_.unit_` 该是 `const ModuleUnit *`。

---

## 建议的处理顺序

1. A1、A2、A3、A4（都是几行的修改加一个用例）：每条一个负例用例——未解析类型变量的 `match type`、同名重载的泛型谓词、未导入谓词的跨模块折叠、带错误的函数体被折叠。
2. B1、B2（改枚举与映射表，诊断金标随之变）、B3。
3. C1 的拆分与 C2 的重复一起做（先合并失败类型，函数才好拆）。
4. B6、B7、B8 与 C3、C4、D 作为清理。
