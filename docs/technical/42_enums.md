# 枚举 `enum`：类型、降级与运行时

本文说明 `enum`（设计原则 5.3、[语言参考 3.5](../language/03_data_model.md#35-枚举-enum)）在实现中的位置：类型层、图构造层、图 IR 与运行时各做什么，以及为什么这样做。用户可见的规则以语言参考为准。

## 表面语法

```cml
type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
    Empty,
}

type Option<T> = enum { Some(T), None }
type Tree<T> = enum { Leaf, Node(Tree<T>, T, Tree<T>) }
```

变体三种形状：无数据（`Empty`）、元组（`Circle(float)`）、结构体（`Rect { w: float, h: float }`）。`Shape::Circle(1.0)`、`Shape::Rect { w: 2.0, h: 3.0 }`、`Shape::Empty` 构造；`match` 按变体分支并绑定字段。

## 解析与 AST

- `enum` 是关键字（[`parser_type.cpp`](../../src/parse/syntax/parser_type.cpp) 的类型声明入口把它作为 `=` 之后的一种类型体）。`type` 声明仍是 `TypeType` 节点，`enum` 体由 `TypeType::Enum` + `EnumTypeLoad`（变体表）表示，变体形状是 `EnumVariantShape`。
- 模式是 CST 的 `EnumTuplePattern` / `EnumStructPattern`，AST 里是 `DataType::EnumPattern` + `EnumPatternLoad`；结构体模式的每个字段有名字与绑定名（`{ w }` 绑定同名，`{ w: width }` 改名）。
- 限定名（`Shape::Circle`）在表达式里仍是 `IdentRef`；AST 区分不了它与模块成员，区分发生在建图时（名字解析之后）。
- **字段位置暂不支持模式**：`case Shape::Circle(0.0)` 在 AST 建树时给出 `SemanticDiag::ConstructNotSupported`，而不是留给后面的层猜。

## 类型层

- `EnumDecl` 是一次声明：名字、类型参数、变体表、原型（实例未定参数时用）。变体 `EnumVariant` 记录形状、字段名、字段类型与 `signature()`（打印用）。
- `EnumType` 是一个**实例**：`EnumType::get(decl, args)` 按声明与实参驻留（反射相等即同一指针）。`EnumType` 是 `OtherType` 的子类，`typeCode()` 注册为 `Enum`（`GC_Traced`，与 `String` 等对象类型同级）。
- 递归：声明先以原型（`pending()`，字段类型里出现 `EnumHole`）进入类型作用域，再构造变体字段、最后 `complete()`。`asEnumType` 对原型返回空，`asEnumTypeOrPrototype` 两者都接受；`containsEnumHole` 与 `enumEntersFrom` 用于实例化与环检测。
- 泛型实参：`EnumType` 带 `typeArguments()`（与 `Map<K, V>` 相同），`cloneWithParams` / `cloneWithArguments` 支持类型函数实例化；名字修饰（mangle）是 `E<declId>P`（原型）与 `E<declId>_<实参mangle>`（实例），跨模块、跨缓存稳定。
- 类型层不提供"按值的类型判断"的任何入口（5.3）：`EnumType` 上没有 `is`/`as` 的运行时分支，比较、打印都在运行时按内容做。

## 图构造层（GCT）与图 IR（GIR）

**不增加节点种类。** 构造与匹配都落在已有的结构上：一个值、一个标签、一个分支。

- 构造（`visitEnumConstruction`）：结构体变体先要求实参是一个结构体值，按名字用 `ACCS` 取出每个字段（字段必须齐全，不许多），元组变体按位置取；字段逐个 `coerceTo` 成变体声明的类型，然后发出内部算子 `__enum_make__`。算子的**显式类型实参**是完整的 `EnumType` 实例：GIR 用 `TAPP`（`tree_->makeTypeApplication()`）把 `f::<Enum>` 挂在被调用的 `LINK` 上，`visitTypeApplication` 把它记进 `explicitOperatorTypes_`。泛型实参由字段实参 `unifyTypeVars` 推出，推不出时用 `expectedValueType_` 的类型参数，仍推不出是 `EnumTypeArgumentNotInferred`。
- 匹配（`visitEnumMatch`）：先 `__enum_tag__(value) => int` 取一次标签，绑定成临时值；然后按源码顺序生成一条**嵌套分支链**：每一层判断 `tag == 变体的常量`，成立走该臂（用 `__enum_field__(tag, index, value)` 取出字段、绑定名字），否则进入下一层。最后一层要么是 `case _` 的臂，要么在穷尽时是一份最后臂的副本（不可达，只为让 `BRCH` 的第二条臂与 `JOIN` 有值）。只有 `case _` 时直接用一条常量 `true` 的单臂分支。分支链是普通的 `BRCH`/`JOIN`，四个调度器都已有。
- `__enum_field__` 的签名是 `(tag: int, index: int, value: E) => T`，`tag` 与 `index` 都必须是静态字面量：**字段的类型由变体决定**，`EnumOpTypeResolver` 从 `normStatics` 读出它们、从 `norm[2]` 的枚举类型取出 `fieldType(tag, index)`。运行期 `tag` 用作断言：值的标签与模式不一致说明图与源码不一致，是内部错误。
- 结果类型由算子自己的解析器决定（`EnumOpTypeResolver` 重写 `resolve` / `resolveWith` / `resolveForCall`），不在建完调用之后改节点类型：这是算子注册的既有约定，`T?`/`T!` 的算子也这样做。
- 诊断在**建图时**给出，位置是源码区间：`EnumConstructionMismatch`、`EnumVariantFieldCount`、`EnumFieldTypeMismatch`、`EnumTypeArgumentNotInferred`、`EnumFieldAccess`（对枚举值用 `ACCS`）、`EnumPatternMismatch`、`EnumMatchNotExhaustive`、`DuplicateEnumVariant`、`DuplicateEnumField`、`EnumVariantCaseRepeated`（警告）、`MatchCaseAfterWildcard`（警告）、`MatchWildcardRedundant`（警告：列全所有变体后还写 `case _`）。`case Empty`（单元变体不写限定名）被读成绑定名，`EnumPatternMismatch` 指出它是该枚举的变体并给出 `case Shape::Empty`。

## 运行时

- `camel::core::rtdata::Variant` 是 `Object`：`{ vptr, tag: u32, count: u32, data_[count] }`，`create(tag, count, allocator)` 分配，`get/set` 按槽读写 `slot_t`。它不是新的 GC 类型：`updateRefs` 逐槽追踪字段，与元组、结构体的做法相同；`mark-sweep` 与分代回收器都靠这个钩子，递归的 `Tree` 也能被完整追踪。
- 打印与相等在运行时的 `Variant::print` / `Variant::equals` 里按内容做（先标签、后字段），与数组、元组、结构体共用 `valuesEqualByContent`（`src/core/rtdata/equality.cpp`），带环检测，所以递归值不会无限递归。`Shape::Rect { w: 2.0, h: 3.0 }` 这种写法来自 `EnumVariant::fieldNames` 与形状，不是运行时拼字符串的特例。
- 算子执行器在 `src/builtin/operators/enum.cpp`：`__enum_make__` 读第一个参数作标签、其余作字段；`__enum_tag__` 读标签；`__enum_field__` 先断言标签再读槽。三者登记为纯算子（`enum/` 前缀进纯度表），资源与语义声明与其他内建算子相同。

## 各后端

`std::nvm`、`std::fvm`、`std::jit`、`std::prl` 都只看见普通的构造、`__enum_*__` 调用、`BRCH`/`JOIN`，不需要为枚举加分支。`std::macro` 遇到能求值的部分照常求值，枚举值可以成为宏的结果并物化为常量。

`onnx::export` 与 autodiff 拒绝：ONNX 是翻译目标还没有枚举的表示，autodiff 是"把依赖微分参数的值放进枚举"没有导数规则。两者都给出带位置的 `RuntimeDiag::TranslationRefused` / `MacroEvaluationFailed`，而不是返回 0 或崩溃。`std::cpp` 已经支持枚举（H8）：每个枚举生成一个带 `tag` 与 `std::variant` 载荷的 `struct`，构造与读取由声明生成的 `make<Tag>` / `get<Tag, Index>` 完成，打印与解释器逐字一致；实现见 `include/camel/standalone/enum.h`、`cpp_types.cpp` 与 `operator_rules.cpp`，测试见 `trans/cpp.plan.toml` 的 `enum_types` 与 `test/tools/cppexec/expect.txt`。

## 留给语言作者的问题

见 `docs/handover/reports/H1.md` 的"待决问题"：变体跨模块的可见性、枚举作为 `Map` 键与 `Set` 元素、`T?`/`T!` 是否改由 `enum` 实现、泛型实参的推断范围、`json` 访问器是否接受 `Json!`。