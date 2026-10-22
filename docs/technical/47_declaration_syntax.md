# 声明写不出的签名：缺口、计数与提案

本文件是 H168 的提案部分：声明（模块 `.cml` 里没有函数体的 `export func`）今天能写什么、
写不出什么，每一处要不要新语法、依据是哪一段、代价是什么。逐名字的盘点与排序见
[`docs/handover/reports/H168.md`](../handover/reports/H168.md)。规范文本是
[`docs/design-principles.cn.md`](../design-principles.cn.md)，标准库写法以
[`docs/stdlib-conventions.cn.md`](../stdlib-conventions.cn.md) 为准；本文只把它们落到声明上。

## 0. 结论

标准库 9 个模块的 130 个公开名字里，**被语言机制挡住的是 17 个**。其余 113 个：23 个已经
声明，8 个不加任何语法就能声明，3 个被标准库自己的规则挡住，5 个缺执行器（不是语法），
74 个按设计就是计算签名。

被挡住的名字里，**没有一个属于“可选参数”“整数宽度”“联合/伞形参数”**——这三类文档已经
裁定用重载写，代价只是规则条数（P1–P3）。真正要新增机制的只有五处：四处只有作者能答
（Q-F、Q-M、Q1、Q3），变参（Q-V）则已经由 5.5 设计成 `..ts`、只是未实现。其中 **Q-M
最小**：2 个名字，机制已经在 `Device<T>` 上存在，只差 `PyObject` 是否跟进，以及语言参考
那句“不能作为模块类型的实参”要不要补例外。

## 1. 口径

### 1.1 声明是什么

设计原则 6.4：

> 原生实现由模块中无函数体的声明表达，外部实现由模块系统承担…

标准库约定 §2：

> 原生实现的函数写成**没有函数体的 `export func`**：没有 `native`、`inner` 之类的新关键字。
>
> 带尖括号参数的声明把 `with <...>` 写在声明上方，与有函数体的函数相同；尖括号参数属于
> 签名，调用点按它绑定，诊断里列出的签名也带着这些参数名。
>
> 与 `.cmo` 同名的 `.cml` 不是 C++ 模块的影子，就是**那个模块**…C++ 模块导出同名算子组而
> `.cml` 又声明了它时，以声明为准（声明是签名的唯一来源），不并存两份。

三条推论，后文每条提案都要用：

1. **`with` 形参有写法**（上引第二条；实例 `stdlib/str.cml:21-22`）。H157 §7 写“声明没有
   `with` 写法”是笔误：`regions` 的阻塞点是函数类型，不是 `with`。
2. **声明替换整个算子组，不是追加**（上引第三条）。所以一个名字不能“固定的一半写声明、
   计算的一半留 C++”。
3. **声明要求执行器与纯度**：`src/compile/gir/builder_modules.cpp:377-390` 要求
   `execMgr().findWithoutImport(uri)` 与 `OperatorTable::find(uri)` 都有记录，缺一个就在声明
   处报 `NativeDeclarationNotLoaded`。

### 1.2 什么算“写不出”

沿用 H151 §2 的三条判据，再加一条：

1. 结果类型不取自任何参数或类型实参；
2. 每个参数都能写成一个类型；
3. 一个名字的所有 URI 都能写进同一组声明；
4. 写出来的声明接受的调用集合与 C++ 相同。近似声明“会让本该拒绝的调用通过”（约定 §2 `docs/stdlib-conventions.cn.md:46`）
   不行；**少写一条规则会让本该通过的调用被拒绝**，同样不行。

**缺口 = 按第 1–3 条写不出、且第 4 条补不回来的形状。** 计数按名字；一个名字有两条缺口时
两条都记，合计时去重。

## 2. 已有裁定：不新增语法

### 2.1 P1 可选参数 → 按元数写重载

**事实。** 形参默认值的*语法*已经被接受（`docs/Camel.asdl:165` 的 `NamedPair` 带 `Data? data`），
建图时直接拒绝：

```text
src/compile/gct/builder.cpp:4625-4629
// Default parameter values are not supported
diags_->of(SemanticDiag::FeatureNotSupported)…commit("default parameter values");
```

**裁定。** 语言参考 7.4.1：

> 形参默认值 `b: int = 5` 保留，尚未实现（`SemanticDiag::FeatureNotSupported`）；用重载或
> `with` 形参。

标准库约定 §2 `docs/stdlib-conventions.cn.md:44`：

> 同一名字的两个非泛型声明如果参数完全相同，视为重复声明…一次调用用哪条规则只能由实参类型
> 决定，不能由声明写下的顺序决定。固定的声明比泛型的声明更具体；一次调用两者都能接受时用
> 固定的那条。

**先例。** `pyplot:plot` 的 8 条是 4 种元素类型 × 2 种元数（H157 §6.1：声明里写不出
`filename?: string`，第一版因此 `ModuleParseFailed`）；`tensor:range` 的 14 条（H151 §3.4）。

**写法。** 每个可能的元数各一条，缺省的那个实参不写。`?` 不是声明语法：它只出现在不编译的
`stdlib/signatures/*.cml` 里（照抄 C++ 签名字面），照抄进 `.cml` 会解析失败。

**代价。** 规则数 `= Σ_{元数} Π_{该元数下每个位置可写的类型数}`。`range` = 2 + 4 + 8 = 14，
`plot` = 8。收益是 0 个新语法、0 个新诊断、0 个 resolver 改动。建议把“一个名字 ≤ 16 条就写，
超过就留 C++”写进迁移清单（`nn:adam` 的 4–7 元数与可选参数会爆炸）。

### 2.2 P2 整数宽度 → 按拼写写重载

**事实。** C++ 的 `ParamKind::Int` 同时接受 `int64` 与 `int32`（`modules/tensor/ops/registry.cpp:50-52`）；
声明里写 `int` 只表示 `Int64`。写一条 `int` 会拒绝 C++ 接受的 `range(0, i32(5))`——按 §1.2
第 4 条这是收窄。

**裁定。** 设计原则 5.1：

> **数值类型之间不做隐式转换。** 整数不会自动变成浮点，别的数值类型之间也一样（参数、
> 返回值、变量之间都一样）；需要时显式写 `float(x)`。
>
> …**变量之间仍不做任何隐式转换。**

所以不能靠“让 `i32` 隐式进 `int`”省规则，只能一个拼写一条。

**先例。** `tensor` 的 `dim`、`seed`、`set_num_threads` 各两条（`stdlib/tensor.cml`），三处都是
这个形状。

**代价。** `2^{Int 位置数} × 元数`；`range` 的 14 条里 8 条来自宽度。**0 个名字被宽度挡住，
只有乘法。**

### 2.3 P3 伞形参数 → `Tensor`、`(typeas A)[]`、逐类型重载

**事实。** `ParamKind::TensorLike` 接受张量或**任意**数组（`registry.cpp:66-67` 只看
`TypeCode::Array`，不看元素类型）；`TensorOrScalar` 同理。声明能写 `Tensor`（不带元素类型与
形状），它接受任意 dtype/shape 的张量（`modules/tensor/type.cpp:301-321`），`load_npy` 已经
这么写（H151 §3.1）。数组这一半用类型变量，5.5 的例子就是：

```camel
func first(xs: (typeas T)[]): T
```

**裁定。** 5.10（没有 `any`，“接受多种类型用类型参数”）与约定 §2 `docs/stdlib-conventions.cn.md:45`（“联合类型退役
之后，重载就是这样写的”）。

**唯一还用得上的名字：`tensor:show`。** 在所有伞形参数的名字里，只有 `show` 的结果是固定的
（其余 `new`、`reshape`、`cast`、`add` 一族的结果都取自实参，本来就算计算签名）。它是
`TensorLike`、结果固定 `void`（`modules/tensor/ops/utility_ops.cpp:227-235`）；H151 把它记成
“计算”，实际是“参数是伞形”。两条声明：

```camel
export func show(t: Tensor): void
export func show(t: (typeas A)[]): void
```

**要补的一个用例。** `show([])`：C++ 接受（只看是不是数组），而声明第二条的元素类型 `A`
绑不出来。如果编译器在到达 resolver 之前就拒绝 `[]`，两条声明是精确的；否则这一处是收窄，
需要一条负例把它记下来。这是 P3 唯一的边界，不是新语法。

**现状（H175）。** 两条已迁入 `stdlib/tensor.cml`（`tensor` 的声明 13 → 15 条），C++ 的
`kDeclaredOperators` 跳过 `show`，计算参考条目从 `signatures/tensor.cml` 删除。实测 `show([])`
**仍被接受**（`[]` 把 `A` 绑到空数组的元素类型），随后在内核报与迁移前逐字相同的
`Unsupported Tensor dtype`：两条声明没有收窄接受面，失败仍在运行时。P3 因此关闭。

## 3. 只有作者能答的问题

### 3.1 Q-F 任意函数类型（9 个名字，独占 6）

**已定的部分。** 函数类型是结构类型（5.2：“元组、数组、函数类型…是别名，按结构比较”），
函数可以作普通参数（4.4：“`grad(loss)`、`with_grad(loss)`”）。函数类型本身**写得出来**；
写不出的是“参数表未知的函数”。

**缺什么。** 一个“任意参数表”的函数类型。今天用 `typeas F` 代替，会接受非函数（约定 §2 `docs/stdlib-conventions.cn.md:46` 禁止这种近似）。H157 的探针里 `Function` 这个名字报 `UnresolvedTypeReference`。

**影响 9 个名字**：`rewrite:__rule`、`rewrite:__rule_when`、`rewrite:__analysis`、
`onnx:export_model`、`tiles:gpu`、`tiles:source`、`tiles:inspect`、`tiles:benchmark`、
`tiles:regions`。

**选项。**

| 选项 | 谁改什么 | 解锁 | 代价 |
|---|---|---|---|
| A. 通配参数表 `(…) => R` | 新类型形式；`assignableFrom`（具体函数类型 → 通配）、`equals`/`mangle`/打印；文档 | `source`、`inspect`、`benchmark`、`__rule`、`__rule_when`（5） | 与 5.8“泛型不变”、5.1“不隐式转换”的关系要写清：函数按结构比较，加一个超类型就是一条新的“进入”规则 |
| B. A 再加一个 `Function` 值类型（无结果约束） | 同上，再加一个未参数化类型 | 再加 `regions`（6） | 更大：`Function` 出现在值位置，部分应用、`with` 实参都要照顾 |
| C. 维持 C++ | 无 | 0 | 6 个名字继续留 C++（`__rule` 是内部名，公开面少 4 个） |

`export_model` 还要 `example` 的三形态（`Tensor` 一条 + 元组/结构体的通配形状，属 5.5 第二期），
`gpu` 还要 Q3 的谓词，两者都不随 A/B 一起解锁。**不做决定。**

### 3.2 Q-V 变参（4 个名字，独占 0）

**已定的部分。** 5.5：

> 标准库签名的文本用类型变量，不用 `any`…参数个数可变、各参数类型不同的 Camel 函数暂不做。
>
> 可变元数 `..ts` 与展开调用 `f(t, ..ts)`，元数编译期确定、可以为 0。

`..ts` 是 5.5 方案 A（按结构展开）的一部分，已设计、未实现。语法树里没有 rest 形参
（`docs/Camel.asdl:165` 的 `NamedPair` 只有 `isVar` 与 `data?`），所以今天连解析都不接受。

**影响 4 个名字**：`rewrite:__analysis`、`python:py_call`、`python:py_print`、`python:py_println`。

**关键发现：变参本身解锁 0 个名字。**

| 名字 | 还要什么 | 依据 |
|---|---|---|
| `py_call` | 同名两个 URI；`py_call_kw` 的第二、三个参数是“任意长的元组 / 任意字段的结构体”，rest 形参与固定类型都写不出 | `modules/python/module.cpp:374-428` |
| `py_print`、`py_println` | 结果是**第一个实参的类型**（`return norm.empty() ? Void : norm[0]`），不是固定的 `PyObject` | `module.cpp:524-530`、`541-547` |
| `__analysis` | 结果取 `initial` 的返回类型，参数里还有任意函数 | H157 §2 |

**选项。** A 等 `..ts` 落地（0 改动；落地时这四个名字仍各缺别的规则）；B 给声明单开一个
rest 标记（第二套写法，与 5.5 冲突）；C 枚举有限元数（说谎）。**建议 A**：这一题不需要新
提案，只需要把“变参声明等 `..ts`”写进账本。

### 3.3 Q-M 模块类型的类型变量实参（2 个名字，独占 2）

**事实。** 语言参考 7.5：

> `typeas` 可以出现在数组元素、元组元素、结构体字段、函数类型的形参与返回值、`T?`/`T!` 的
> 内层、`Map`/`Set` 与类型函数的实参这些位置上。不能作为模块类型的实参。

代码的规则更细（`src/compile/gct/builder.cpp:4871-4884`）：模块类型只要覆写
`typeArguments()` 就接受类型变量，“它的实参是普通类型”（Map、Set、Device）。`tiles` 的
`Device` 覆写了（`modules/tiles/include/tiles/device.h:78`），H157 的探针里 `Device<typeas T>`
装载成功；`PyObject` 没有覆写（`modules/python/type.h`），所以 `py_wrap`/`py_unwrap` 报
`InvalidTypeArgument`。

**`PyObject` 的实参确实是类型。** `PyObjectType::equals` 逐参数比较
（`modules/python/type.cpp:74-90`），`cloneWithParams` 拷贝 `Type*`（105-110）；`py_wrap` 的
resolver 就是 `cloneWithParams(norm[0])`，`py_unwrap` 要求 `paramCount() >= 1` 并返回
`params()[0]`（`module.cpp:554-582`）。所以两条声明与 C++ 逐点相同：

```camel
export func py_wrap(x: typeas T): PyObject<T>
export func py_unwrap(obj: PyObject<typeas T>): T
```

`py_unwrap` 不接受裸 `PyObject`（resolver 直接 `nullopt`），声明也不接受，因此不是收窄。

H166 §6 已经把这一题写成“待作者裁定”，并给了三个方向（放开但由模块导出“可参数化”标记、
加专用类型构造器语法、保持阻塞）。下表的 A 就是第一个方向，`Device` 已经是它的现成实现。

**选项。**

| 选项 | 谁改什么 | 解锁 | 代价 |
|---|---|---|---|
| A. `PyObjectType` 覆写 `typeArguments()`（与 `Device` 同形） | 一处覆写 + 7.5 那句加例外；再检查 `PyObject<int>` 与 `PyObject<string>` 之间不做赋值 | 2 | 低。`unifyTypeVars` 已经会进 `OtherType` 的参数（`src/core/type/typevar.cpp:161-168`）；剩下的是“标签不参与赋值”的规则或用例 |
| B. 退成无参 `PyObject` | 声明写 `(x: typeas T): PyObject`、`(obj: PyObject): typeas T` | 1（`py_wrap`，且丢掉标签；`py_unwrap` 只能靠 `::<T>`，即 Q1） | 违反“不收窄” |
| C. 维持 C++ | 无 | 0 | 2 个名字继续留 C++ |

**不做决定。** 但要指出：这不是新机制，是“`PyObject` 要不要像 `Device` 一样跟进”，以及
7.5 那句要不要补例外（它现在与 `Device<T>` 的实际行为冲突，见 §6 缺陷 1）。

### 3.4 Q1 声明看不见类型实参（2 个名字，独占 2）

**影响**：`tensor:from_bytes`（三个 URI `from_bytes_f32` / `_i64` / `_bool`）、
`ckpt:load_tree`；不在本表但同一形状：`json::decode_as`。

**完整的选项表与非决定记录**在 H148 Q1 的 A/B/C（A 参数化声明、B 永远留 C++、C 只接受
`::<T>`），`docs/handover/reports/H148.md:54-74`。本文不重复、不改写。要补的只有一句：
语言参考 7.5 已经允许调用侧这么写（“只出现在返回类型中的变量由期望类型推出…没有足够上下文
时写显式类型实参”），缺的只是声明侧。

### 3.5 Q3 类型变量谓词（3 个名字，独占 1）

**影响**：`ckpt:save_tree`（独占）、`tiles:gpu`、`onnx:export_model`（这两个还要 Q-F）。

H148 Q3 的 A/B/C（`where`/类型谓词、接受编译期检查退化、维持 C++），
`docs/handover/reports/H148.md:106-126`。**不建议顺手加**：H140 §7 已经写过它比 H108 的选项 1
更大。

## 4. 不是语法缺口，不要动

### 4.1 类型限定别名（`Tensor.shape`）

- 只有 `tensor` 有（`modules/tensor/module.cpp:92,100` 的 `exportEntity(Reference({"Tensor", name}), …)`），
  共 68 条别名记录；`tiles`/`graph` 没有。
- 已有通用机制：声明替换模块实体时 `Module::replaceTypeQualifiedAliases` 把别名指到声明
  （H151，提交 `18766a65a`）；检查脚本的注释就是裁定（`scripts/stdlib-declarations.mjs:63-68`）。
- **0 个名字被它挡住。** H156/H141 的缺口是 `Device<...>` 与 `__neq__`，不是别名。

### 4.2 一个名字只能有一个来源（`range`、`sum`、`mean`，3 个名字）

`range` 的三个位置都可省略、宽度要两种拼写，14 条写得出，但结果形状取自实参——写
`=> Tensor<int64>` 是丢掉形状（H151 §3.4 的“不收窄”）。`sum`/`mean` 的名字里同时有
`(t) => float`（可声明）与 `(t, axis, keepdims?) => Tensor`（计算）；声明替换整组
（§1.1 推论 2），一半写不出就整个名字留 C++（H151 §3.4）。改这条规则才会解锁这 3 个名字，
代价是把“签名只写一处”破成两处，不建议。

### 4.3 结果取自实参（74 个名字）

约定 §1 把“计算出来的签名（返回类型取自实参、按形状展开）”指定给 C++ 的
`DynamicFuncTypeResolver`/`OpDef`，并说“声明写不出来的算子留在 C++ 的 resolver，它仍是那条
签名的唯一来源”（§2 `docs/stdlib-conventions.cn.md:46`）。这是设计，不是缺口。构成：`tensor` 49、`nn` 18、`tiles` 5
（`run`×3、`upload`、`download`）、`autodiff` 2。

### 4.4 逐叶结构映射（`tiles:upload` / `download`）

结果把输入类型里的每个张量叶映射成 `Device<...>`（`tree_transfer.cpp`，H157 §7）；需要 5.5
第二期的类型函数按结构展开（“`tree_map` 这类…最终用 Camel 的泛型函数写在库里…为此泛型要能在
编译期按结构体、元组的字段递归”）。不是签名语法。

### 4.5 缺执行器（`ckpt` 的 5 个 `tree_*`）

`tree_map`/`tree_reduce`/`tree_unzip`/`tree_leaves`/`tree_paths` 由编译器展开，只有占位
resolver（`modules/tree/module.cpp:30-60`），没有执行器；声明要求
`execMgr().findWithoutImport` 命中（§1.1 推论 3），写上也装不上。缺的是执行器/引擎，不是语法。

## 5. 汇总

| 提案 | 性质 | 独占解锁 | 改动面 | 风险 |
|---|---|---|---|---|
| P1 可选参数 → 元数重载 | 已有裁定 | 0（使 `range`/`plot` 等可写） | 无 | 无 |
| P2 宽度 → 拼写重载 | 已有裁定 | 0（同上） | 无 | 无 |
| P3 伞形 → 重载 | 已有裁定 | 1（`show`） | 2 条声明 + 1 个边界用例（H175 已完成） | 低 |
| Q-M `PyObject<T>` | 作者一句确认 | 2 | `typeArguments()` + 7.5 例外 | 低 |
| Q-F 任意函数类型 | 作者问题 | 5（A）/ 6（B） | 新类型形式 | 高 |
| Q1 类型实参 | 作者问题（已有） | 2 | H148 Q1 的 A | 高 |
| Q3 类型变量谓词 | 作者问题（已有） | 1 | H148 Q3 的 A | 很高 |
| Q-V 变参 | 已设计未实现 | 0（4 个名字的前置） | 等 `..ts` | 中 |
| 策略（`sum`/`mean`/`range`） | 策略 | 0 | 改“一处签名” | 不建议 |
| 计算签名 74 | 设计 | 0 | 无 | — |

## 6. 缺陷（H176 核对：第 1、6 条交作者；第 2–5 条已改文档）

1. **语言参考 7.5 与代码冲突**（交作者）：`typeas` “不能作为模块类型的实参”，但 `Device<typeas T>`
   装载成功（`typeArguments()` 覆写；`src/compile/gct/builder.cpp:4871-4884` 只拦没有覆写
   `typeArguments()` 的模块类型；`modules/tiles/include/tiles/device.h:78`）。要么补例外句，要么
   收回 `Device` 的行为。Q-M 的裁定依赖这句话的准确写法。
2. **H151 §3.2 把 `show` 记成“计算”**，实际是“伞形参数”；它有一条固定 `void` 的结果，
   P3 可以写。H151 §3.4 标题写“2 条”，列的是 3 个名字（`range`、`sum`、`mean`）。**H176 已改**：
   H151 的标题、§0、§3.2 与 §3.4 按 53 个计算 / 55 条签名更正，`show` 单列为可声明未迁。
3. ~~**`stdlib/signatures/tensor.cml` 头部**说“一个名字一条签名”，同文件的 `dim` 有两条
   （`int`/`i32`），是它自己的反例。~~ **已由 `5a6dc71c0` 修好**：`dim` 已迁到 `stdlib/tensor.cml`，
   当前 `signatures/tensor.cml` 头部写的是“computed signatures”，文件里没有 `dim`。
4. **H157 §7 写“声明没有 `with` 写法”**，与约定 §2 `docs/stdlib-conventions.cn.md:32`、`stdlib/str.cml:21` 冲突；
   `regions` 的阻塞点是函数类型。**H176 已改** H157 §7 的那一行。
5. **H157 §10 的“任意函数类型 3 个”只数六个模块**（`tiles` 的 5 个在 §7 单列），合起来是 8；
   它把 `py_print`/`py_println` 记成纯变参，实际结果还取自第一个实参。**H176 已改**：H157 §0/§10
   写明范围，§6.2 两行补上结果类型（`modules/python/module.cpp:450`/`:467`）。
6. **语言参考 7.4.3 第 155 行**（交作者）“泛型函数不能与其他函数同名”与约定 §2
   `docs/stdlib-conventions.cn.md:44`“固定的声明比泛型的声明更具体”措辞冲突；对象不同（带函数体的
   函数 / 声明），但 7.4.3 没有限定。

## 7. 顺序

排序与建议见 [`docs/handover/reports/H168.md`](../handover/reports/H168.md) 第 7、8 节：
先做 0 语法成本的声明（基点上 16 条：`python` 6 + `pyplot` 8 + `show` 2；`develop` 上
`python`/`pyplot` 的 14 条已由 H166 迁完，`show` 2 条已由 H175 迁完），再看 Q-M。