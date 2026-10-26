# 类型系统核心代码审查

范围：`src/core/type/`（`base.cpp`、`intern.cpp`、`text.cpp`、`pure.cpp`、`composite/{array,tuple,struct,func,stream}.cpp`、`collection.cpp`）与 `include/camel/core/type/intern.h`、`include/camel/core/data/primary.h`；`src/compile/gct/builder.cpp` 里的字面量与期望类型（`visitLiteral`、`declaredValue`、`visitDataInto`、`literalNodeInType`、`As`）；`src/compile/gir/builder_expr.cpp` 的 CAST 与 `builder_control.cpp` 的收窄（真正的类型推断在 gir，gct 只做字面量与期望类型的搬运）。对照设计原则 §5（5.1 无隐式转换且字面量随上下文，5.2 命名类型，5.4 数组，5.8 型变，5.11 特化）以及"打印出来的类型都能写出来"。只读代码，没有构建、没有运行；标"需确认"的是由代码推断、未实测。

总体：驻留表（`TypeTable`）、规范 id 与"名字不进入类型同一性"的设计是清楚的，数组类型的形状规则也集中在一处；问题集中在三处：打印出的类型有多种写不出来、写出来含义会变的拼写；`equals`、`assignableFrom`、`unify`、驻留键四处对"什么算同一个类型"的回答不一致；强制转换表在类型层与数据层各写了一份，并与原则 5.1 有出入。

按严重度排序。

## 高

### T1 打印出的类型有多种程序写不出来，或写出来是另一个类型

语法依据：`parser_type.cpp:46-60`（`typeExpr : arrayType ('?' | '!' | '..')*`，"`T?[3]` is still a syntax error"）、`:199-206`（`(T)` 是带括号的类型，不是元组）、`:327-345`（`uses` 子句只接标识符）。

| 位置 | 打印结果 | 问题 |
|---|---|---|
| `text.cpp:447` 数组 | `int?[]`、`int![3]` | 元素是 `T?`/`T!` 时不加括号，语法错误；要写 `(int?)[]` |
| 同上 | `(x: int) => int[]` | 元素是函数类型时不加括号，重新读入成"返回数组的函数"，含义改变（流和 tagged 的同类情形已加括号：`text.cpp:104-112`、`:461-464`） |
| `text.cpp:136-146` 元组 | 一元元组打印成 `(int)` | 解析为 `int`；要写 `(int,)`。空元组 `()` 正确 |
| `text.cpp:261,268,271` 资源子句 | `uses #0`、`any io`、`capture 3` | `uses` 只接标识符；匿名参数、资源类、捕获下标没有写法 |
| `text.cpp:361`、`:199`、`:142` | `=> <null>`、`<null>`、`null` | 未定类型的占位，不是类型 |
| `text.cpp:206-214`、`collection` 不完整 | `Map`、`Set` 无实参；泛型结构体实参缺失时 `_` | 是否可写取决于语法是否接受裸原型名（需确认） |
| `text.cpp:352-358` 剩余参数 | `(.. rest)` 只有名字没有类型 | 不是函数类型的写法 |

- 失败场景：诊断和 `inspect`、语言服务器的悬停都说"你应该写成 X"，X 是上表里的写法时用户照抄得到语法错误或另一个类型（函数元素的数组最糟）。设计原则 §5.1 要求诊断和图都用源码里的类型名。
- 修法：打印与解析共用一份"需要括号"的判断（元素是 tagged / function / stream 时加括号；一元元组加逗号）；资源子句里无法命名的项打印成可读但明确标注的形式，或在出错信息里改用参数序号的说法而不进入类型文本；加一个"打印后重新解析再 `equals`"的循环测试，遍历所有类型形式（见"测试缺口"）。

### T2 `Type::assignableFrom(nullptr)` 恒为真，与复合类型不对称

- `base.cpp:506`：`if (!from || to == from) return true;`。数组、元组、函数、结构体的 `assignableFrom` 对空指针都返回 `false`（`array.cpp` 的 `!sourceType`、`tuple.cpp` 的 `!sourceType`、`func.cpp:373-376`），基本类型对空指针返回真。
- 失败场景：任何尚未求出类型的来源（`nodeTypeOf` 返回空、未解析的 `Ref` 之外的空）被基本类型位置静默接受，同样的空来源进复合类型位置才被拒绝，错误的出现与否取决于目标是什么类型。
- 修法：空来源一律 `false`（或 `ASSERT`），需要"未定"语义的调用点显式处理。

### T3 `equals`/`assignableFrom` 忽略了驻留键里有的字段（`sync` 修饰与 `with` 常量标记）

- 键包含 `modifiers`、`withConstIndices`（`func.cpp:109-111` 起的键构造，注释写"two signatures that differ there are not the same signature"），打印也包含修饰符（`text.cpp:317`）；但 `sameTypeWith` 的函数分支（`text.cpp:632-669`）与 `FunctionType::assignableFrom`（`func.cpp:373-415`）只比较 `constasIndices`、参数、返回和资源，不比较 `modifiers` 与 `withConstIndices`。`checkModifiers()` 恒为 `true`（`func.cpp` "Queries" 一节）。
- 失败场景：`sync (x: int) => int` 与 `(x: int) => int` 打印不同、驻留成两个对象、`equals` 为真、可以互相赋值；一个 `with` 里带编译期常量参数的函数被当作不带的函数传入，重载与特化键会把它们当成一个或两个，取决于用的是 `equals` 还是 TypeId。
- 同类：数组 `equals` 把 `f32[n]` 与 `f32[n + 1]` 视为相等（`array.cpp` 的 `sameExtentConstraint`：非常量一律相等），两个只差线性形式的签名被判重复。
- 修法：先决定哪些字段是类型的一部分，三处（键、`equals`、`assignableFrom`）读同一个函数；若 `sync` 不属于类型，就从键里去掉。

### T4 强制转换矩阵与原则 5.1 不一致，且类型层与数据层各写了一份

- `base.cpp:25-32` 的矩阵把 `i32→f32`、`i64→f64`、`bool→数字`、`数字→bool` 标为 `Safe`；而 `i64→f32` 是 `Unsafe`。`i32→f32` 超过 2^24 就丢精度、`i64→f64` 超过 2^53 就丢精度，这不是"安全"，规则看起来是"位宽不变窄"而不是"值保持"。原则 5.1 要求数值之间不做隐式转换，用 `float(x)` 显式写；`as` 允许这些而转换函数在越界时报故障，两种写法的损失规则不同。
- `base.cpp:266-433` 的 `castSlotFrom` 里实现了 `i64→i32`（`checkedNarrow`）、`f→int`（`checkedTruncate`）、字符串解析等大量分支，但函数开头 `ASSERT(castSafetyFrom == Safe)`，而这些组合在矩阵里是 `Unsafe`/`Forbidden`（`gir/builder_expr.cpp:1110-1116` 只放行 `Unsafe` 进模块类型和数组），所以这些分支在调试构建里不可达、在发行构建里（`ASSERT` 若被去掉）可达，两种构建行为不同（需确认 `ASSERT` 的发行行为）。
- 数据层 `PrimaryData<T>::convertTo`（`primary.h:304-340`）又是第三份：只处理 `Int32↔Int64`、`Float32↔Float64`。后果：`As` 的字面量分支（`gct/builder.cpp:3016-3045`）先问类型层 `castSafetyFrom == Safe`，再 `convertTo`，于是 `5 as float` 通过第一步、`convertTo` 返回空，报 `reportLiteralCastFailure`；而同样的 `n as float`（`n` 是变量）走 CAST 节点正常通过。字面量和变量对同一个 `as` 给出不同答案。
- 修法：以原则为准裁定一张表（`as` 允许哪些数值互转、是否带损失），只在 `Type` 上保留一份；`castSlotFrom` 的死分支删掉；字面量的 `as` 复用同一张表。

## 中

### T5 `pure.cpp` 的缓存以指针为键，且可能缓存"尚未定义完"的命名结构体

- `pure.cpp:52-56`、`:181-199`：`isPureDataType` 的结果永久缓存在 `unordered_map<const Type *, …>`。命名结构体的字段在声明读完前是空的（`struct.cpp:113-125` 的 `get` 先发布条目再填字段，`intern.h` 说明"key 不依赖字段"）。若在字段未齐时询问，`typeAt(i)` 为空 → `isPureDataType(nullptr)` 为假 → 永久缓存"不纯"。
- 失败场景：对自引用或相互引用的声明，数组元素的存储方式（`arrayStorageFor`，`array.cpp` 开头）取决于是谁先询问；同一个类型在不同编译顺序下得到 `Compact` 或 `Slots` 两种存储，而数组值的布局由它决定。
- `thread_local visiting` 命中环时返回 `false`，该 `false` 会被外层当作字段结果缓存（`:198`），污染内层类型。
- 修法：字段不完整时不缓存（返回"未知"）；缓存键换成类型的 TypeId（或把结果放进类型对象），并让环检测不写缓存。

### T6 `unify` 只有数组、流、枚举会，元组/结构体/函数/`T?` 不会，分支合并在聚合里失败

- 只有 `array.cpp:327`、`stream.cpp:77`、`enum.cpp:212` 覆盖 `unify`；其余走 `base.cpp:183` 的"相等才合并"。
- 失败场景：`if c then ([1, 2], 0) else ([1, 2, 3], 0)`：两臂类型 `(int[2], int)` 与 `(int[3], int)`，`joinTypes`（`tuple.cpp:510` 起）用 `unify` 失败，再用 `entersImplicitly` 也失败，报分支类型不一致；而同样的两个数组直接做分支就能合成 `int[]`。数组字面量的元素是这种元组时同理（`ArrayType::resolve` 报 `ElementTypeMismatch`）。`f32[2]?` 与 `f32[3]?` 同理。
- 修法：为元组/结构体（匿名）/可空/可失败/函数返回类型补 `unify`，规则是逐成员合并；与 5.8 的"泛型类型不变"区分开（`unify` 是分支的最小上界，不改变赋值的型变）。

### T7 结构体转换的规则分散在三处，且顺序规则不同

- `StructType::assignableFrom`（`struct.cpp` 末段）与 `castSafetyFrom` 用 `layoutCompatibleFrom`：字段名与顺序必须一致；`entersImplicitly`（`tuple.cpp:454` 起）用 `structFieldsMatchByName`：名字对应，顺序可以不同（原则 5.2）；`gir/builder_expr.cpp:1083-1128` 再用 `convertStructValue` 做复制。
- 两个命名类型字段相同但声明顺序不同时：类型层 `castSafetyFrom` 为 `Forbidden`，`gir` 里 `as` 却会复制通过（5.2 规定"字段按名字一一对应"）。类型层的答案与实际接受的集合不一致，工具（语言服务器、`check`）按类型层判断会误报。
- 修法：一处定义"按名字对应"的转换关系，`castSafetyFrom` 返回对应的 `Safe`（同序、零移动）或新增的"需要复制"状态。

### T8 数组可赋值性里的陈旧说明与 `Void` 目标

- `array.cpp` 的 `assignableFrom` 上方注释举例"`int[]` where `f64[]` is expected when an int is assignable to an f64"；按 5.1，整数不可赋值给 f64，该例子不成立（陈旧）。
- `base.cpp:512-515`：`to` 为 `Void` 时对任何来源返回真（为"丢弃返回值"设计）。这条规则在聚合内部同样生效：元组成员、结构体字段、函数的 `with` 参数位置若是 `void`，会接受任何类型（`(void, int)` ← `(string, int)`）。函数返回位置需要它，其余位置不应该有。
- 修法：把"丢弃"限定在函数返回位置的比较上，其余位置的 `Void` 只接受 `Void`。

### T9 标记类型的转换：陈旧注释和不检查的重新标注

- `tuple.cpp:309-312` 注释"Retyping a failed `U!` as `T!` is the error arm of `?->` and the last step of `throw`"：`?->` 与 `throw` 已移除。该分支对 `U! → T!` 返回 `Unsafe`，`castSlotFrom`（`:339-343`）则原样返回同一对象，不检查值确实是错误。目前 `gir/builder_expr.cpp:1110-1116` 只放行 `Unsafe` 进模块类型与数组，所以程序写不出来；但类型层仍然声称这是一个可以做的转换，一旦放宽那条限制就是类型混淆（把 `string!` 的成功值当成 `int!`）。
- 修法：删除该分支（返回 `Forbidden`），或要求运行期检查值为错误。

### T10 以指针为键的缓存与"规范 id"并存

- 同一结构、不同维度名的类型（`f32[n]`/`f32[m]`）是两个对象、两个 TypeId、同一个规范 id（`intern.h` 文件头注释）。以 `Type *` 为键的缓存：`core/derivative.cpp:255-256`（`cachedTangent`）、`pure.cpp:52`、`tiles/src/graph_values.cpp:54`、`passes/trans/json/gir_canonical.cpp:158`、`service/semantic/analyze.cpp:220`。对象永不释放所以指针稳定，但等价类型各占一项：切空间类型对 `f32[n]` 与 `f32[m]` 各算一次、各返回自己的对象，随后用指针比较梯度类型与参数类型（`autodiff` 里"gradientOf compares with the parameter's tangent type"）会在等价类型之间失配。
- 修法：缓存键用 `canonicalId`；比较一律用 `sameCanonicalType`/`equals`，在注释里写明哪些调用点允许指针比较。

## 低

- 修饰符与异常：`include/camel/core/func.h` 的 `ExitType::{Raise, Throw}` 仍在，`to_string` 还在打印，语言里已无 `throw`。
- 修饰符打印：`sync (x: int) => int` 是可写的头部形式（`parser_type.cpp:304`），OK；但函数类型的修饰符在数组元素位置同 T1 一并要括号。
- 修饰名映射：`base.cpp:34-72` 的 `sourceTypeSpelling` 对任意文本里的单词 `i64`/`f64` 做替换，不看上下文，字段名、维度名恰好叫 `i64` 时被改写；而 `typeCodeToString` 已经直接打印 `int`/`float`，大多数调用（`gir.cpp:285`、`graphviz.cpp:456`、`resolver.h:232`）的结果与不调用相同。要么删除，要么只对类型文本的标记做替换。
- 重整名不是前缀无歧义的：`bool`(`b`) + `f16`(`f16`) 与 `bf16` 同串，空成员 `N` 与可空 `N` + 类型同串（`text.cpp:101-114`、`base.cpp:121-148`）。目前靠成员数前缀与"空成员不会出现"掩盖，建议给每个标量重整名加定界或定长。
- `TypeTable`：`intern.h` 头注释一处说互斥量是递归的、"`make` 可以再取表"，同一份注释另一处说 `make` "must not call back into the table"；`typeKeyWord` 里 `uintptr_t{1} << 63` 在 32 位构建下溢出；`TupleType::create` 的构造函数对空成员直接 `->code()`（`tuple.cpp:55-60`），而键（`typeKeyWord(nullptr) == 0`）、打印、比较都容忍空成员，容忍的代码是死代码或崩溃点。
- `ArrayType::withElementType(nullptr)` 静默返回自身（`array.cpp`）；`ArrayTypeFactory::build` 在没有元素类型时默认 `Void`。两处都把"没有给"变成了合法值。
- 重复逻辑：`ArrayType::castSlotFrom` 里 `anyRank_` 与否的两段形状检查几乎逐字相同；`unstatedBindingIn`（`tuple.cpp`）的 `Stream` 分支里 `isUntypedArrayType(actual)` 对流恒为假，是死代码，数组分支的 `collectTypeVars` 取的是整个模式的第一个类型变量而不是元素位置的那个。
- 大函数：`gct/builder.cpp` 单文件 6404 行；`visitReservedExpr` 约 431 行、`visitFuncType` 约 317 行、`visitModule` 约 314 行、`visitFuncData` 约 248 行、`resolveAmbientResource` 约 234 行；`text.cpp` 的 `sameTypeWith` 约 190 行（一个循环里放了六种类型的比较）。
- 历史叙述型注释（建议改成现状）：`text.cpp:564-567`（"this is what the tensor type's equality did before"）、`array.cpp` 的 `shapeText`（"every `int[]` program keeps the text it had before dimensions moved into this type"）、`array.cpp` 的 `castSlotFrom`（"After the tensor alias the array is where…"）、`intern.h` 文件头（"Before it, every construction…allocated a new object"、"`adopt`…is gone"）、`primary.h:322`（"it used to be cut to 32 bits"）、`parser_type.cpp:57-60`（"`A of B` used to be read here"）、`pure.cpp:46-49`（"…a diagnostic the tests pin down"）、`gct/builder.cpp:4396` 的任务名引用（"on `expected-type-through-literals-and-calls`, item 3"）。

## 逐项核对原则 §5 的结论

| 原则 | 结论 |
|---|---|
| 5.1 数值不隐式转换 | 赋值/期望类型路径（`declaredValue`、`literalNodeInType`）符合：字面量按精确值转换，复数不降为实数，浮点字面量不进整数（`gct/builder.cpp:4205-4270`）。`as` 路径见 T4。 |
| 5.1 字面量随上下文、精确计算、一次舍入 | 符合（`literal::Value::toData`，`LiteralNotRepresentable`）。 |
| 5.1 `[]` 的元素类型只来自位置 | 符合（`visitDataInto` 在 `T[]` 位置生成该类型的空数组；`statesARank` 位置报 `DimensionMismatch`）。 |
| 5.1 诊断用源码里的类型名 | 基本符合（`int`/`float`）；可写性见 T1。 |
| 5.2 命名类型需显式转换，匿名可进命名 | 规则分散，见 T7。 |
| 5.4 数组 | 形状规则集中在 `array.cpp`，`T[]` 与 `T[?]` 同一类型，未知形状进已声明形状需 `as`（`assignableFrom` 与 `sameExtentConstraint` 一致）。`unify` 在聚合里缺失，见 T6。 |
| 5.8 泛型类型不变 | 集合不变（`collection.cpp:196`，仅资源可异）；数组/元组/结构体对成员是协变，因值语义不可变而成立，但 `Void` 目标例外见 T8。 |
| 5.11 开放维度不进特化键 | 本次未审到特化键，见 T10 对键的提醒（需确认特化缓存用规范 id 还是 TypeId）。 |
| 收窄（3.8） | `builder_control.cpp:1838-1990`：只对 `let`/形参（`var` 拒绝）、`!=`/`==` 的直接形式、`&&` 的操作数；规则与注释一致，且保留了被拒绝收窄的解释。未发现漏洞。 |

## 测试缺口

核心类型层只有 `src/core/type/array_type_selftest.cpp` 一份单元自检（只覆盖数组）；`test/cases/compile/casts/` 只有 2 个用例。缺：

1. 类型文本循环测试：对每种类型形式（含数组元素为 `T?`/`T!`/函数、一元元组、带资源子句的函数、泛型命名结构体、Map/Set）做 打印→解析→`equals`，防止 T1。
2. 可赋值关系的性质测试：自反、传递，以及与 `equals` 的一致性（`a.equals(b)` ⇒ 双向可赋值；规范 id 相等 ⇒ `equals`），覆盖 T3、T7、T8。
3. `unify` 的聚合用例：元组/结构体/可空里含数组的分支合并（T6）。
4. 重整名单射性：枚举一批类型，断言 `mangle` 互不相同（含 `bool,f16` 与 `bf16`）。
5. 强制转换矩阵与原则的对照表测试：每个 `(源, 目标)` 对给出 `as` 的期望（T4），字面量与变量各一份。
6. `pure` 缓存：先询问再补全字段的命名结构体，数组存储方式前后一致（T5）。
7. 驻留：同一结构、不同维度名/参数名的类型，规范 id 相同且各缓存命中同一项（T10）。
