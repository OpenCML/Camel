# `any`：现状普查，与"编译之后每个值都有确定类型"的方案（调研稿）

状态：**调研稿；已按设计原则 5.10 落实（2026-10-14）。** 本稿正文保持调研时的样子，所描述的是落实之前的实现；`examples/` 里写了 `any` 的程序今天编译不过（`any` 是普通的未知类型名，报 `SemanticDiag::UnresolvedTypeReference`；H27 删去了原来的 `AnyNotAllowed` 与按位置的推荐写法）。落实的结果：
- Q1 定为甲（用户写 `any` 是错误）；Q2 定为乙（`json.decode` 给出自描述的 `Json`，另有 `decode_as`）；Q4 定为甲（签名说明用类型变量）；Q5 定为甲（不做）；Q6、Q7 照建议。
- `TypeCode::Any` 与 12 个文件里的特判已删去；附录 B 的 D0–D6 的复现程序都成了回归用例（`test/plans/feat/compile/typing_any.plan.toml`、`test/plans/feat/modules/json_value.plan.toml`）。
- 图的提交校验加了"图上的类型都已确定"（[图模型设计稿](../graph-model/README.cn.md) 的 I17）。
依据：`docs/design-principles.cn.md`（5.5、5.6 的泛型与类型函数，10.5 的垃圾回收，5.10 的 `any`，第 15 节；[裁定记录](../../design-principles-log.cn.md) 2.7 的"`<any>`"一条）；`docs/technical/36_gc_handle_rules.md` 第 9 节；`docs/technical/37_generational_gc_report.md` 6.4 与第 7 节；`docs/technical/28_generic_functions_and_type_functions.md`；[计算图的对外数据模型](../graph-model/README.cn.md)。
基线：分支 `lane/analysis`，提交 `6dd61bcd`（Camel v0.2.9），Linux x86-64 容器里的发布构建。文中的输出与数字都可以用附录 A 的命令复现。

文中每条事实标注来源：

- 【实测】在本仓库构建出的 `camel` 上实际运行看到的；
- 【读码】读代码得到、没有运行验证的（给出文件与行号）；
- 【未查】没有核实的。

示例在本文档旁边的 [`examples/`](examples/)，脚本在 [`tools/`](tools/)；文中写作 `examples/x`、`tools/x` 的路径都相对于 `docs/design/any-typing/`。

裁定原文（2026-10-13，设计原则 5.10）："`any` 只是一种简化的写法，是类型系统成熟之前临时开的口子。不应当存在到了运行时还不确定的类型：每个值在编译之后都有确定的类型。"

---

## 0. 摘要

**`any` 比想象的少，但每一处都是实打实的洞。**

- 全仓库 1,266 个 `.cml` 文件里，把 `any` 写在类型位置的只有 11 处：9 处在 `stdlib/signatures/`（C++ 算子的签名说明），1 处在测试（`func a(x: int): any`），1 处在编辑器语法的测试。标准库里用 Camel 写的代码（`stdlib/array.cml`、`stdlib/perf.cml`）一处都没有。【实测 `tools/any_census.py`】
- C++ 一侧共 33 处、12 个文件：13 处 `Type::Any()`、16 处 `TypeCode::Any`、4 处签名文本。274 处直接写出的类型规则里，签名带 `any` 的是 10 处登记；**其中真正在运行时产出 `any` 值的只有一个算子：`json.decode`**。其余 9 处只是"什么类型都收"，算子体从调用方的帧里读到的是实参的具体类型。【实测 + 读码，1.2】
- Python 桥不用 `any`：Python 值的类型是 `PyObject`，一个回收器认识的对象。【实测 `examples/t17_python.cml`】

**一个 `any` 值今天是一个没有类型的 64 位槽。**

- 进 `any` 不检查，出 `any` 也不检查：`x as any` 再 `as float` 把整数的位当浮点读，`as string` 把 12345 当地址读，进程崩溃。【实测 `examples/t02_cast_unchecked.cml`、`t03_cast_crash.cml`】
- 类型为 `any` 的槽（帧槽、数组元素、元组与结构体字段、闭包捕获、图的静态区）回收器一律跳过，两种回收器都是如此。【读码 `include/camel/core/type/base.h:83`、`include/camel/core/context/frame.h:251-257`，2.5】
- **默认回收器、不加任何开关就能崩溃**：`examples/t20_default_collector.cml`（22 行，不用 `json`）在 `n=1500000` 时三次运行三次段错误，把 `any` 换成 `string` 的对照程序三次都对。【实测，2.9】
- `--gc generational` 下 5 个把引用放在 `any` 里的程序（帧槽、数组元素、元组/结构体字段、经过用户函数、`std::macro` 算出的常量）在测过的后端上（`nvm`、`fvm`，其中三个加测 `jit`、`prl`）或崩溃、或**打印错值而退出码为 0**（`200000 157292`、`300 item-299 item-299 item-299`）。【实测，2.9】
- `docs/technical/36_gc_handle_rules.md` 第 9 节说"立刻转换（`decode(s) as T`）在 `std::nvm`、`std::fvm` 上安全"。**这不成立**：`CAST` 是纯节点，没有控制边，后端把它排在用它的地方而不是 `decode` 之后。`examples/t21_cast_at_once.cml` 在四个后端上都出错。【实测，2.9；附录 B 的 D4】
- `any` 的静态性质会被遍抹掉：同一个程序不加遍打印 `<any>`，加 `std::opt::inline` 打印 `42`。【实测 `examples/t18_inline_changes_print.cml`】这正说明它"只是一种写法"：值本来就有类型，是签名把它遮住了。
- `std::cpp` 直接拒绝任何带 `any` 的图。【实测，2.8】

**方案的要点。**

1. 签名里的 `any`（9 处登记）改成类型规则或类型变量，约 100 行，图上从此不出现 `any`。不需要新的语言能力。
2. 用户代码里的 `any` 有两条路，需要负责人选（Q1）：直接判错；或者照裁定的字面意思，把形参位置的 `any` 定义为"匿名类型变量的简写"，于是 `func keep(x: any)` 就是泛型函数，图上只有具体类型。本稿建议后者。
3. `json.decode` 是唯一真正的难点：JSON 文本的形状运行时才知道。三条路（Q2）：只留已有的 `decode_as`；返回一个模块类型 `Json`（带种类标记、回收器认识的对象，用算子取出成员）；保留一个通用的带标记动态值。本稿建议第二条，4.4 给出它的内存排布与追踪方式——回收器不用改，因为"自描述对象"这条路今天已经在给函数值、张量与 `PyObject` 用。
4. 做完之后把 `TypeCode::Any` 连同 12 个文件里的特判一起删掉，并在图的校验里加一条不变量："节点、端口、槽位的类型不带 `Auxiliary` 标志"。`36` 号文档第 9 节的开放问题与 `37` 号文档"分代回收暂不做默认"的第一条理由随之消失。
5. 迁移分 6 步（第 5 节），总计约 900–1,200 行；仓库内受影响的用户程序是 3 个测试文件、8 处调用。

**需要负责人确认的事**在第 6 节，共 7 条；其中 Q1、Q2、Q3 决定方案的走向。调研中发现的 6 个现有缺陷在附录 B，都有复现程序，没有修。

---

## 1. `any` 现在在哪里

普查脚本：`python3 docs/design/any-typing/tools/any_census.py --list`（在仓库根目录执行）。它分别数 `.cml` 里写在类型位置的 `any`（去掉注释与字符串）、C++ 里的 `Type::Any()` 与 `TypeCode::Any`、C++ 字符串里的签名文本。下面的数字都是它的输出。【实测】

### 1.1 类型系统与语法

| 位置 | 内容 | 出处 |
|---|---|---|
| 类型码 | `TypeCode::Any = Primitive \| Auxiliary, 0x00D`。`Auxiliary` 的注释是"用于解析、没有数据对象的辅助类型"；它**不带** `GC_Traced` 标志，这是回收器看不见它的根源 | 【读码 `include/camel/core/type/base.h:52-57, 83`】 |
| 名字 | 类型作用域里预置了 `any` 这个名字，指向 `Type::Any()`。它不是关键字，解析器不认识它；所以 `stdlib/array.cml` 可以有一个叫 `any` 的函数 | 【读码 `src/compile/gct/builder.cpp:280`；`stdlib/array.cml:93`】 |
| 赋值相容 | 任何类型都可以赋给 `any`（实参给形参、`return` 给返回类型都走这条）；`any` 不能赋给别的类型 | 【读码 `src/core/type/base.cpp:442-447`】 |
| 转换 | `any` 与任何类型之间的 `as` 一律是 `Safe`，转换时"槽原样传过去" | 【读码 `src/core/type/base.cpp:188-194, 233-236`】 |
| 数组字面量 | 推断元素类型时，已有的元素类型若是 `void`、`any` 或 `Ref`，就被下一个元素的类型**替换** | 【读码 `src/core/type/composite/array.cpp:84-87`；后果见附录 B 的 D2】 |
| `T?`、`T!` | 源类型是 `any` 时走基类的"原样传过去"，不装箱 | 【读码 `src/core/type/composite/tuple.cpp:414-417`；后果见 D1】 |
| 映射与集合 | `any` 不能作键（"没有可以用来比较的类型"）；`any as Map<...>` 被拒绝（"运行时没有东西能检查它"） | 【读码 `src/core/type/collection.cpp:81-82, 173`；实测 `examples/t05h_map_key.cml`】 |
| 语言文档 | `docs/language/03_data_model.md:29`："类型名 `any` 存在，供内建算子的签名使用；用户代码里不能把值声明为 `any`"。**实现并不拒绝**，见 2.1 与 D3 | 【实测】 |
| 语言服务器 | 补全用的内建类型名单里有 `any` | 【读码 `src/service/semantic/queries.cpp:172`】 |

### 1.2 C++ 算子的签名与类型规则

算子的类型规则有两种【读码 `include/camel/core/type/resolver.h:102-135`】：固定签名（`StaticFuncTypeResolver`，全仓库直接写出 202 处）和一段 C++ 函数（`DynamicFuncTypeResolver`，72 处，外加一段给人看的签名文本）。

**固定签名里用了 `Type::Any()` 的算子，共 6 处登记：**

| 算子 | 签名 | 运行时有没有无类型的值 | 出处 |
|---|---|---|---|
| `:op/atos`（`string(x)` 的兜底重载） | `(any) => string` | 没有。算子体用 `norm.type(0)` 取到实参在调用方帧里的具体类型，按它打印 | 【读码 `src/builtin/modules/globals.cpp:214-215`；`src/builtin/operators/cast.cpp:153-158`】 |
| `:op/strict_eq`、`:op/strict_ne`（`===`、`!==`） | `(any, any) => bool` | 没有；算子体比较两个槽的位。`===` 已定为暂不支持，前端拒绝，这两个登记是死的 | 【读码 `globals.cpp:689-708`；`src/builtin/operators/ops.cpp:486-498`；`test/cases/compile/not_supported/strict_equal.cml`】 |
| `json:encode`（两个重载） | `(any) => string`、`(any, int) => string` | 没有；同样读实参的具体类型。实参的静态类型本身是 `any` 时运行期故障 `JsonEncodeUnsupported` | 【读码 `modules/json/module.cpp:41-46`；`modules/json/operators.cpp:56-60, 435-438`；实测 `examples/t06c_json_encode_any.cml`】 |
| **`json:decode`** | `(string) => any` | **有。全仓库唯一返回 `any` 的算子** | 【读码 `modules/json/module.cpp:49-52`】 |

**签名文本里写了 `any` 的类型规则，共 4 处：**

| 算子 | 文本 | 实际规则 | 出处 |
|---|---|---|---|
| `:io/print`、`:io/println` | `<fmt?: string> (...args: any) => void` | 接受任意个任意类型的实参，返回第一个实参的类型 | 【读码 `globals.cpp:1145-1180`】 |
| `:str/format` | `<...args: any> (fmt: string) => string` | 同上 | 【读码 `globals.cpp:1203-1213`】 |
| `onnx:export_model` | `(fn: (...) => any, example: ...` | 文本里的 `any` 只是"返回什么都行" | 【读码 `modules/onnx/module.cpp:141`】 |

这 9 处登记（除 `decode`）有一个共同点：**`any` 只出现在签名里，不出现在图上。** 算子节点的类型由规则从实参类型算出；算子体经 `ArgsView::type(i)` 读到的是调用方帧槽的静态类型【读码 `include/camel/core/context/frame.h:623, 675`】。它们是"类型规则里的占位"，不是无类型的值。

**运行时造出 `any` 的代码只有一处**：`json.decode` 的 `jsonToCamel`。JSON 数组变成一个不带元素类型的 `Array`，JSON 对象变成一个运行时现造的结构体类型，字段类型全是 `Type::Any()`（这个类型分配在永久空间，从不释放）。【读码 `modules/json/operators.cpp:408-427`】

**为 `any` 设了特判的消费者**（都是防御，迁移完可以删）：分代回收器的 `isPreciseGCType`（`src/core/mm/gen.cpp:927-930`）、调试器（`tools/debugger/src/core/values.cpp:238-241`，显示 `<any>`）、`std::cpp` 的桥（`src/passes/trans/cpp/bridge.cpp:282, 304, 464`）、`json.encode`（上表）、`algo.unique`（`modules/algo/sequence.cpp:99-104`，非追踪类型按槽的位去重，于是 `any[]` 里的引用按地址去重）、Python 类型的转换规则（`modules/python/type.cpp:93`）。【读码】

### 1.3 标准库

| 文件 | 类型位置的 `any` | 说明 |
|---|---|---|
| `stdlib/array.cml`（21 个函数）、`stdlib/perf.cml` | 0 | 全部用 `typeas T` 写成泛型；`array.cml:93` 的 `any` 是函数名 |
| `stdlib/signatures/globals.cml` | 4 | `string(x: any)`、`print(...args: any)`、`println(...args: any)`、`format` 的 `with <...args: any>` |
| `stdlib/signatures/json.cml` | 5 | `encode` 两个重载的形参、`decode` 的返回类型、`decode_as` 的 `with <shape: any>` 与返回类型 |

`stdlib/signatures/` 是 C++ 算子的签名说明，不参与执行。其中 `decode_as` 一条与实现不符：它写的是 `<shape: any> (text: string): any`，实际的规则是"返回 `shape` 的类型 `T` 的 `T!`"【读码 `modules/json/operators.cpp:478-485`；实测 `examples/t06b_json_typed.cml`】。签名说明的写法今天表达不了"结果的类型取自这个实参"，只好写 `any`。这是 4.5 里要问的一件事。

### 1.4 测试、示例、基准、文档

| 位置 | 内容 |
|---|---|
| `test/cases/compile/return_type/accepted.cml:6` | `func a(x: int): any { return x }`，计划里断言输出含 `<any>`。全部测试程序里用户自己写 `any` 的只有这一处 |
| `test/cases/tools/grammar/types.cml:19` | `type All = i32 \| i64 \| f32 \| f64 \| bool \| any \| void`，编辑器语法着色的测试 |
| `test/cases/modules/std/json/decode.cml` | 6 处 `decode(...) as T`；`test/cases/compile/diag_render/runtime_json.cml` 1 处 `println(decode('{bad json'))`。这是仓库里 `json.decode` 的全部使用者 |
| `examples/`、`exemplars/`、`benchmarks/` 的 `.cml` | 0 |
| `benchmarks/pipeline/exclusions.json` | 两条排除项，理由是"打印静态类型为 `any` 的值，内联前后输出不同" |
| 文档 | 设计原则裁定记录 2.7（`<any>` 是正常现象）、设计原则 5.10；`docs/language/03_data_model.md:29`；`docs/stdlib-reference.md` 7 处签名；`docs/technical/35_debugger.md:253`；`36`、`37` 号文档的开放问题 |

### 1.5 分类汇总

| 类别 | 是什么 | 数量 | 运行时有无类型的值 | 去处（第 4 节） |
|---|---|---|---|---|
| A 签名占位 | 算子"什么都收"，算子体按实参的具体类型工作 | 9 处登记：`print`、`println`、`format`、`string` 的兜底、`encode`×2、`===`、`!==`、`export_model` | 无 | 类型规则 / 类型变量；`===`、`!==` 删除 |
| B 真正的动态值 | 形状运行时才知道 | 1 个算子：`json.decode`；仓库内 7 处调用 | **有** | 待定（Q2）：`decode_as`、`Json` 类型或带标记的动态值 |
| C 用户写的 `any` | 形参、返回类型、`as any`、`with` 形参、函数类型、由此派生的 `any[]`、`(int, any)`、`{…: any}` | 仓库内 1 处 | **有** | 泛型（Q1） |
| D 类型系统里的支撑 | 相容、转换、数组推断、装箱的特判 | 5 个文件 12 处 | — | 随 A、B、C 清空后删除 |
| E 防御性特判 | 回收器、调试器、`std::cpp`、`unique`、`encode` | 6 处 | — | 删除 |
| 真正泛型的（应是类型变量） | — | 归入 A（`string`、`println`…）与 C | | |
| 异构容器 | `any[]` 装不同类型的东西 | 仓库内 0 处；只有 `decode` 的结果是 | 有 | 元组 / 结构体；JSON 走 B |
| 打印与格式化 | `print`、`println`、`format`、`string` | 4 个算子 | 无 | 归入 A |
| 外部接口 | Python 桥 | 0（用的是 `PyObject`） | 无 | 不动；它是 B 的参照物 |

### 1.6 不是 `any`、但容易混为一谈的三样东西

- **`PyObject`。** Python 桥的每个算子收发的都是 `PyObject` 或 `PyObject<T>`（由类型为 `T` 的 Camel 值包装而来）【读码 `modules/python/module.cpp:373-600`】。它是登记了 `GC_Traced` 的模块类型【读码 `modules/python/type.cpp:30-33`】，值是一个外部资源包装对象，回收器认识。`examples/t17_python.cml` 在毒化模式下通过。【实测】它说明"运行时才知道内容的值"可以有一个确定的静态类型。
- **联合类型 `A | B`。** 它也是 `Auxiliary`（`TypeCode::Union`）。今天它只在 `stdlib/signatures/` 里当作重载的简写（`f32(x: i32 | int | f32 | float | string)`）；用户函数的形参写成 `int | string` 可以通过声明，但没有任何实参能匹配它，带标注的 `let x: int | string = 'abc'` 是 `LiteralNotRepresentable`。【实测 `examples/t14_union.cml`】所以今天没有联合类型的值。它的去留在设计原则第 15 节（"类型表达式语言的具体内容"），不在本稿范围；但 4.6 的不变量把它一并挡在图外。
- **没有元素类型的 `[]`。** `let e = []` 是编译期错误 `EmptyArrayWithoutType`。【实测 `examples/t15_empty_array.cml`】

---

## 2. 一个 `any` 值在各层发生什么

### 2.1 能写在哪，能对它做什么

【实测；程序是 `examples/t01*.cml`、`t05*.cml`、`t19_misc_positions.cml`，`bash docs/design/any-typing/tools/run_examples.sh` 一次跑完】

| 写法 | 结果 |
|---|---|
| `func id(x: any): any`；`func wrap(x: int): any { return x }` | 通过。实参与返回值进入 `any` 不需要写 `as` |
| `(t + 'x') as any`（算出来的值） | 通过 |
| `42 as any`、`let a: any = 42`（字面量） | `LiteralNotRepresentable` |
| `let a: any = n`、`let xs: any[] = [...]`（带标注的 `let`） | `FeatureNotSupported: dynamic type casting` |
| `any[]`、`(int, any)`、`{ id: int, payload: any }` 作函数的返回类型 | 通过 |
| `with <tag: any>`、`let f: (x: string) => any = wrap_s`、结构体字面量的字段 | 通过 |
| `var v = wrap_i(7); v = wrap_s('seven')`（一个变量先后装两种类型） | 通过 |
| 泛型函数以 `T = any` 实例化（`first(xs)`，`xs: any[]`） | 通过，结果的类型是 `any` |
| `a + 1`、`len(a)`、`a == b`、`a[0]`、传给 `int` 形参 | `NoMatchingFunction`：`any` 不能当别的类型用 |
| `[(k as any, 2)] as Map<any, int>` | `CollectionKeyNotAllowed` |
| `if a then 1 else 2`（`a: any`） | **通过**，把槽当 `bool` 读（附录 B 的 D5） |
| `match v { case 1 => … case 'ab' => … }`（`v: any`，装着字符串 `'ab'`） | **通过**，结果是默认分支：比较的是槽的位，字符串按地址比（D5） |
| `println(a)`、`string(a)`、`'{}'->format<a>`、`println(xs)`、`println(('k', a))` | `<any>`、`<any>`、`<any>`、`[<any>, <any>]`、`(k, <any>)` |

也就是说，一个 `any` 值今天只能做五件事：传给 `any` 形参、放进 `any` 的槽、打印成 `<any>`、当条件、`as` 回某个类型。它不是"动态类型"，没有任何操作能问它是什么。

### 2.2 `as`：进去不记，出来不查

```camel
let bits = 4607182418800017408      // 1.0 的位模式
let a = bits as any
println(a as float)                 // 1.0
let x = 3.5
println((x as any) as int)          // 4615063718147915776
let h = 'hel' + 'lo'
println(len((h as any) as int[]))   // 33554272
```

`camel docs/design/any-typing/examples/t02_cast_unchecked.cml`【实测，`std::nvm` 与 `std::fvm` 输出相同】。把整数 12345 `as any` 再 `as string` 后打印，进程段错误（退出码 139）：`examples/t03_cast_crash.cml`。【实测】

原因：`any` 与任何类型之间的转换被判为 `Safe`，执行时槽原样传递（1.1）。没有任何地方记录值进入 `any` 之前的类型。

`json.decode` 之后的 `as` 同样不查。【实测 `examples/t06d_json_shapes.cml`】

```camel
let o = decode('{"b": 1, "a": 7}') as { b: int, a: int }
println(o.b, o.a)                          // 7 1    对象按键排序建成 (a, b)，类型说 b 在前
println(decode('[1, 2.5]') as float[])     // [5e-324, 2.5]    1 是按整数存的
let ys = decode('[[1], [2]]') as int[]     // 元素是两个数组的地址
```

三行都不报错，退出码 0。

### 2.3 图上

`camel docs/design/any-typing/examples/t01_declare.cml std::rgir`【实测】：

- `x as any` 是一个 `CAST` 节点，节点类型是 `any`；`a as string` 是另一个 `CAST`，节点类型是 `string`。两个 `CAST` 都只有一条数据入边，**没有控制边**。
- `func wrap(x: int): any { return x }` 的图是 `PORT x → EXIT`，没有 `CAST`：端口的类型是 `int`，函数类型的返回类型是 `any`（`camel inspect --gir` 显示 `(x: int) => any`）。调用它的 `FUNC` 节点的类型是 `any`。
- `id(3.5)` 是 `DATA 3.5 → FUNC id`，实参进入 `any` 形参同样没有节点。

所以 `any` 在图上是节点、端口与函数类型上的一个类型标注，没有专门的节点。对图的数据模型来说，去掉 `any` 不改变节点种类清单，只是让"节点的 `type` 是一个具体类型"成为可以校验的不变量（[图模型设计稿](../graph-model/README.cn.md) 3.4 的 `type` 字段）。

### 2.4 帧与槽位

四个后端共用一种帧：一段 8 字节的槽，加一个描述每个槽类型的布局（`TupleType`）。`any` 槽与 `int` 槽一样是 8 字节，值是进入 `any` 之前那个值的位。【读码 `include/camel/core/context/frame.h:235-268`】

| 后端 | 对 `any` 槽的处理 | 出处 |
|---|---|---|
| `std::nvm` | `CAST` 调目标类型的 `castSlotFrom`（原样传递）。`COPY` 按源槽的类型码决定：带 `GC_Traced` 的克隆对象，不带的拷贝槽——所以 `any` 里的对象被 `COPY` 时只拷了指针 | 【读码 `src/passes/sched/nodevm/nodevm.cpp:545-567`】 |
| `std::fvm` | 同上（`casevm.cpp:139`、`gotovm.cpp:337`）。`match` 的条件不是追踪类型时比较槽的位 | 【读码 `src/passes/sched/fastvm/runtime_support.cpp:45-60`】 |
| `std::jit` | 与 `fvm` 共用帧与字节码；`COPY` 的慢路径同 `fvm`（`jit/runtime/trampoline.cpp:486`）。`src/passes/sched/fastvm/jit` 下除这一处外没有按类型码分支的代码，机器码不区分 `any` 槽 | 【读码：检索 `TypeCode::`、`isGCTraced`】 |
| `std::tf` / `std::prl` | 同一种帧，由 `TaskflowFramePool` 当根遍历；`COPY` 同上（`taskflow.cpp:1633, 1938`） | 【读码 `src/passes/sched/taskflow/taskflow_frame_pool.cpp:309-312`】 |

对象里也一样：数组、元组、结构体的元素/字段是 8 字节的槽，类型在外面（持有它的那个槽的静态类型里）。对象自己不带类型。【读码 `docs/technical/36_gc_handle_rules.md` 第 5 节；`include/camel/core/rtdata/array.h:186-196`】

### 2.5 回收器

两种回收器是同一个分配器的两种模式（`src/core/mm/gen.cpp`），追踪方式相同：**对象不带类型，回收器拿着"到达这个对象的那个槽的静态类型"去追踪它。** 判断一个槽里是不是引用，看的是类型码上的 `GC_Traced` 位。`any` 没有这一位。

| 持有引用的地方 | 追踪代码 | 遇到 `any` |
|---|---|---|
| 帧槽 | `Frame::updateRefs`：`if (isGCTraced(codes[i]))` | 跳过 【读码 `frame.h:251-257`】 |
| 数组元素 | `FixedArray::updateRefs`：元素类型码不带 `GC_Traced` 就整个数组返回 | 跳过 【读码 `array.h:186-196`】 |
| 元组、结构体字段 | 逐字段看类型码 | 跳过 【读码 `include/camel/core/rtdata/tuple.h:193`、`struct.h:217`】 |
| 图的静态区（常量） | `shouldTraverseStaticSlotType` | 跳过 【读码 `src/runtime/graph.cpp:222-230`】 |
| 宏求值器的值根 | `traceValueRoots` | 跳过 【读码 `src/execute/macro/eval.cpp:245-250`】 |
| 写屏障 | 被存入的类型不带 `GC_Traced` 就返回 | 不登记 【读码 `src/core/mm/gen.cpp:206-224`】 |
| 分代回收器的"精确类型"判断 | `isPreciseGCType` 显式排除 `Any` | 【读码 `gen.cpp:927-930`】 |

后果有两层：

1. **`any` 槽里的引用不算根，也不被更新。** 只从 `any` 槽可达的对象，标记-清除会释放它，分代会把它留在被腾空的空间里；槽里的地址从此指向别人的内存。
2. **就算对象本身因为别的引用活着，从它往下也追不动。** `json.decode` 给出的数组没有元素类型，结构体的字段类型是 `any`；`any[]` 这个数组对象是被追踪的（数组类型带 `GC_Traced`），但它的元素不是。

（数组、元组、结构体的 `updateRefs` 开头都是"传进来的类型不是我这种就返回"，所以拿着 `any` 到达一个对象时，它的成员一个也不会被追踪。）回收器没法靠自己补上这个洞：对象头只有 8 字节（48 位大小、年龄、标记、区域），没有类型【读码 `include/camel/core/mm/alloc/header.h:39-55`】；给回收器一个"不知道类型的引用"，它不知道该往下看哪些槽。

### 2.6 打印、相等、拷贝、调试器

- 打印：`printSlot` 对不带 `GC_Traced`、也不是模块类型的类型码按原始类型分支，`any` 落到 `default`，输出 `<类型名>` 即 `<any>`。这就是设计原则裁定记录 2.7 那条裁定的实现。【读码 `include/camel/core/rtdata/base.h:240-283`】
- 相等：`==` 的重载里没有接受 `any` 的，编译期拒绝（2.1）。`valuesEqualByContent` 对非追踪类型比较槽的位，`any` 成员（数组元素、元组字段）因此按位比，引用按地址比。【读码 `src/builtin/operators/ops.cpp:461-470`】
- 拷贝与克隆：元组、结构体、数组的克隆对非追踪字段拷贝槽，深拷贝到 `any` 字段为止。【读码 `include/camel/core/rtdata/tuple.h:123-150`】
- 调试器：显示 `<any>`，不展开。【读码 `tools/debugger/src/core/values.cpp:238-241`】

### 2.7 遍

- **内联抹掉 `any`。** `examples/t18_inline_changes_print.cml`：

  <!-- camel-snippet: fragment: a proposal; any is not a type the current language has -->

  ```camel
  func wrap(x: int): any { return x }
  func main(): int sync {
      let n = 40 + 2
      println(wrap(n))     // 不加遍：<any>     std::opt::inline 之后：42
      println(n as any)    // 两种情况都是 <any>（这里有一个 CAST 节点）
      return 0
  }
  ```

  【实测 `camel …/t18_inline_changes_print.cml` 与 `camel …/t18_inline_changes_print.cml std::opt::inline std::fvm`】内联之后 `println` 的实参节点是 `n` 本身，类型是 `int`。`benchmarks/pipeline/exclusions.json` 为此排除了两个程序。按设计原则 9.6，遍前后行为不同不算缺陷；但它直接说明 `any` 遮住的类型编译器本来就知道。
- **内联也改变 GC 问题是否暴露。** `examples/t11_gc_user_func.cml` 加 `std::opt::inline` 后（`std::fvm`）前四种设置都对（`any` 形参没了），只剩毒化模式报出；`examples/t07_gc_frame.cml` 加 `std::opt::inline` 后五种设置全对。【实测，附录 A 的矩阵命令加 `PASSES=std::opt::inline`】这不是修复，是时序变了。
- **`std::macro` 把 `any` 值变成常量。** `#macro func table(): any { return decode('[…]') }` 经 `std::macro` 求值后，结果成为 `main` 静态区里一个类型为 `any` 的常量；静态区的追踪同样跳过它（2.5）。`examples/t16_macro_const.cml` 加 `std::macro` 后在强制回收下打印 `300 299`、`300 0`（退出码 0）或崩溃。【实测】

### 2.8 `std::cpp`

`std::cpp` 只翻译 `int32`、`int64`、`float32`、`float64`、`bool`、`string`、`void` 七种类型的值；函数的形参、返回值、`CAST`/`COPY`/`GATE` 节点或算子结果的类型是别的，就拒绝整张图。【读码 `src/passes/trans/cpp/bridge.cpp:113-129`；`runtime_emitter.cpp:346-409, 491-504`】

```
$ camel docs/design/any-typing/examples/t01_declare.cml std::cpp
error[RuntimeDiag::TranslationRefused]: Translation pass 'std::cpp' refused the graph: function 'id'
returns a value of type 'any'; std::cpp translates values of the types int32, int64, float32,
float64, bool, string and void only.
$ camel docs/design/any-typing/examples/t06_json.cml std::cpp
… the result of operator 'json:decode' in function 'main' has type 'any' …
```

【实测】桥接层另有三处把 `any` 当原子类型处理（生成 `Type::Any()` 的类型表达式，`bridge.cpp:282, 304, 464`），因为前面的检查，它们只在"算子的形参类型是 `any`"时被走到。发出的 C++ 里所有非七种类型的值都是 `slot_t`（`cppTypeFor` 的 `default`，`bridge.cpp:131-148`）——也就是说 `std::cpp` 若放开 `any`，得到的同样是一个无类型的 64 位数。对 `std::cpp` 而言，"编译后每个值都有确定类型"是它能给每个值选 C++ 类型、选内存管理方式的前提。`std::cpp` 的现状与它的内存模型见[另一份报告](../cpp-memory/README.cn.md)。

### 2.9 实测：回收器看不见

**程序。** 都是"造一个值、分配 `n` 个字符串、再用这个值"，`main` 的参数 `n` 由 `--arg n=` 给：

| 程序 | 引用放在哪 |
|---|---|
| `examples/t07_gc_frame.cml` | `json.decode` 的结果放在 `any` 的帧槽里（`37` 号文档 6.4 的程序） |
| `examples/t09_gc_array_elem.cml` | `any[]` 的元素（不用 `json`） |
| `examples/t10_gc_tuple_field.cml` | `(int, any)` 的字段与 `{ id: int, payload: any }` 的字段 |
| `examples/t11_gc_user_func.cml` | 经过 `func keep(x: any, n: int): any`，再被闭包捕获 |
| `examples/t16_macro_const.cml` | `std::macro` 算出的 `any` 常量（图的静态区） |
| `examples/t21_cast_at_once.cml` | `decode(...) as string[]` 写在同一个表达式里 |
| `examples/t12_typed_control.cml`、`t13_generic_instead.cml` | 对照：同样的程序，类型写明 / 写成泛型 |

**设置。** `bash docs/design/any-typing/tools/gc_matrix.sh <文件…>` 对每个程序跑五种：默认（标记-清除）与 `CAMEL_GC=generational` 各 `n=200000`；`CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=major`（默认回收器，每个安全点强制一次）、分代加 `CAMEL_GC_STRESS_MODE=minor`、`CAMEL_GC_POISON=1` 各 `n=300`。

**结果**【实测；"对"= 输出正确，"错值"= 输出错误而退出码 0，"崩溃"= 段错误 139，"毒化"= 毒化模式报告并以 134 退出】：

| 程序 / 后端 | 默认 | 分代 | 默认 + 强制 major | 分代 + 强制 minor | 毒化 |
|---|---|---|---|---|---|
| `t07` nvm | 对 | 崩溃（另一次运行：错值 `200000 157292`） | 崩溃 | 错值 `300 299` | 毒化 |
| `t07` fvm | 对 | 对 | 崩溃 | 崩溃 | 毒化 |
| `t07` jit | 对 | 对 | 崩溃 | 崩溃 | 毒化 |
| `t07` prl | 对 | 崩溃 | 崩溃 | 错值 `300 287` | 毒化 |
| `t09` nvm | 对 | 崩溃 | 错值 `300 item-0 item-0 item-0` | 错值 `300 item-299 item-299 item-299` | 毒化 |
| `t09` fvm | 对 | 对 | 崩溃 | 崩溃 | 毒化 |
| `t09` jit | 对 | 对 | 崩溃 | 崩溃 | 毒化 |
| `t09` prl | 对 | 崩溃 | 超时（40 秒） | 崩溃 | 毒化 |
| `t10` nvm | 对 | 崩溃 | 错值 `300 1 item-0 2 item-0` | 错值 `300 1 item-299 2 item-299` | 毒化 |
| `t10` fvm | 对 | 对 | 错值 | 崩溃 | 毒化 |
| `t11` nvm | 对 | 崩溃 | 对 | 错值 `300 item-299 item-299` | 毒化 |
| `t11` fvm | 对 | 崩溃 | 对 | 崩溃 | 毒化 |
| `t11` jit | 对 | 崩溃 | 对 | 崩溃 | 毒化 |
| `t11` prl | 对 | 崩溃 | 对 | 错值 `300 item-169 item-169` | 毒化 |
| `t16` + `std::macro`，nvm | 对 | 崩溃 | 错值 `300 299` | 错值 `300 299` | 毒化 |
| `t16` + `std::macro`，fvm | 对 | 对 | 错值 `300 0` | 崩溃 | 毒化 |
| `t21` nvm | 对 | 崩溃 | 崩溃 | 错值 `300 299` | 毒化 |
| `t21` fvm | 对 | 对 | 崩溃 | 崩溃 | 毒化 |
| `t21` jit | 对 | 对 | 崩溃 | 崩溃 | 毒化 |
| `t21` prl | 对 | 崩溃 | 崩溃 | 错值 `300 284` | 毒化 |
| `t12`、`t13`（对照）nvm、fvm | 对 | 对 | 对 | 对 | 对 |

读法：

- 20 行 `any` 程序里，分代回收器不加任何压力开关时 12 行崩溃；两种强制回收设置下 40 格里 36 格出错（含 1 格超时；其余 4 格是 `t11` 在标记-清除下：那个字符串还被 `main` 帧里一个类型为 `string` 的槽持有，对象不移动，所以没事）；毒化模式 20 行全部报出。对照程序 20 格全对。
- 结果不稳定：同一条命令（`t07`，nvm，分代）一次打印 `200000 157292` 退出码 0，一次段错误。打印出来的"错值"是后来分配的字符串（`item-299`）或它的长度：`any` 槽里的旧地址上现在住着别的对象。
- **`t11` 只把 `any` 用在一个用户函数的形参和返回类型上**——这正是"泛型还不好写时拿 `any` 顶一下"的典型用法，分代下四个后端全部崩溃。

**默认回收器、不加任何开关。** 上表的"默认"一列全对，是因为标记-清除要分配到几十兆才回收一次，而且释放的内存要被重新分出去才看得出错。把两件事都凑齐：

<!-- camel-snippet: fragment: a proposal; any is not a type the current language has -->

```camel
// examples/t20_default_collector.cml
func churn(n: int): int { return len(range(0, n)->map<(i: int): string => 'item-' + string(i)>) }
func make(n: int): any[] {
    return range(0, n)->map<(i: int): any => ('a-long-payload-string-number-' + string(i)) as any>
}
func main(n: int): int sync {
    let xs = make(3)
    let c1 = churn(n)
    let c2 = churn(n)
    println(c1 + c2, xs[0] as string, xs[1] as string, xs[2] as string)
    return 0
}
```

| `camel …/t20_default_collector.cml --arg n=N std::fvm` | 结果 |
|---|---|
| `n=1000`（没有回收） | 对 |
| `n=1200000` | 对 |
| `n=1500000` | **段错误，三次运行三次**；`std::jit` 同样 |
| `n=1800000` | 段错误 |
| `n=2000000`、`n=2500000` | 对（做了 2 次 major 回收，没有踩到） |
| `examples/t20b_default_typed.cml`（`any` 换成 `string`），`n=1500000`，三次 | 对 |

【实测】所以这不是"只有打开分代才有的问题"。默认回收器下它需要更大的分配量和一点运气，这也是全套测试没有碰到它的原因。

**`decode(s) as T` 不是安全的写法。** `t21` 把转换写在同一个表达式里，照样出错。原因在 2.3：`CAST` 没有控制边，同步块只给有效应的语句串控制链；后端按依赖把它排到使用者之前，而不是 `decode` 之后，于是 `decode` 的结果在 `any` 槽里度过了整个 `churn`。`examples/t08_gc_as_any.cml` 是它的反面：`make(3) as any` 的 `CAST` 同样被推迟，`any` 槽存在的时间很短，五种设置全对——同一个机制，这次碰巧帮了忙。【实测；"`CAST` 被排在使用者之前"是从两个程序的结果推断的，没有逐条看字节码】`test/cases/modules/std/json/decode.cml` 的 6 处用法都是这个形状。

**没能构造出来的。** 想在默认回收器、默认设置下构造"打印错值且退出码 0"（而不是崩溃）的程序，没有成功：`t20` 在试过的 6 个 `n` 上要么对、要么段错误。错值只在强制回收与分代下看到。

---

## 3. 目标与判据

**目标**：编译之后，图上每个节点、每个端口、每个帧槽、每个对象成员的类型都是具体类型。

判据（每一条都能机械地检查）：

- T1 图校验能表述并检查"节点类型不带 `Auxiliary` 标志"（`Any`、`Ref`、`Union`、`TypeVar`、`TypeFunc` 都在这个标志下）。
- T2 回收器不需要任何"类型未知"的分支：每个引用都经由一个带 `GC_Traced` 的静态类型到达，或者指向一个自描述对象。
- T3 没有不检查的转换：`as` 要么在编译期就知道成立，要么在运行时检查并给出 `T!` 或故障。
- T4 `std::cpp`、GPU 内核等后端可以对每个值问"你是什么类型"并总能得到答案。
- T5 设计原则 3.8 不变：不做通用联合类型的运行时分辨；运行时没有"类型"这种值。

T5 与"JSON 的形状运行时才知道"之间的张力是 Q2 的全部内容。

---

## 4. 方案：每一类用法的去处

### 4.1 A 类：签名里的占位 → 类型规则或类型变量

不需要语言改动，只动 C++ 的登记。

| 算子 | 现在 | 改成 | 规模 |
|---|---|---|---|
| `:op/atos` | 固定签名 `(any) => string` | 类型规则：接受一个任意的具体类型，返回 `string`；签名文本写 `(x: T) => string` | 约 10 行 |
| `json:encode` ×2 | 固定签名 `(any[, int]) => string` | 类型规则：实参类型有 JSON 形式才接受（判断函数 `jsonTypeHasForm` 已有，在 `modules/json/operators.cpp`）。函数值、张量今天是运行期故障 `JsonEncodeUnsupported`，改后是编译期的 `NoMatchingFunction` | 约 25 行 |
| `:op/strict_eq`、`:op/strict_ne` | 固定签名 `(any, any) => bool` | 删除登记与算子体（`===` 暂不支持；将来若恢复，用类型规则要求两边同类型） | 净减约 30 行 |
| `print`、`println`、`format`、`onnx:export_model` | 已经是类型规则，只有文本里有 `any` | 改文本（写法见 Q4） | 4 行 |
| `stdlib/signatures/*.cml` 的 9 处、`docs/stdlib-reference.md` 的 7 处 | `any` | 随上面重新生成 | 生成 |

做完这一步，`TypeCode::Any` 出现在图上的来源只剩 B 与 C。

### 4.2 C 类：用户写的 `any` → 泛型

`any` 在用户代码里做的事，泛型都能做，而且图上是具体类型：

```camel
// 今天（examples/t11_gc_user_func.cml）：x 的槽没有类型，回收器看不见
func keep(x: any, n: int): any { let c = churn(n); return if c > 0 then x else x }
let v = keep(('abc' + string(1)) as any, n)
println(v as string)

// 泛型（examples/t13_generic_instead.cml）：实例化为 keep<string>，槽的类型是 string
func keep(x: typeas T, n: int): T { let c = churn(n); return if c > 0 then x else x }
let v = keep('abc' + string(1), n)
println(v)
```

【实测：`t13` 在 2.9 的五种设置下全对】

逐种写法的对应：

| 今天的写法 | 泛型下 | 备注 |
|---|---|---|
| `func f(x: any)` | `func f(x: typeas T)` | 每组实参类型一个实例 |
| `func f(x: int): any { return x }` | 返回类型写 `int`，或不写（推断） | 今天不写返回类型已经可以（`return_type/accepted.cml` 的 `func c`） |
| `func f(x: any): any { return x }` | `func f(x: typeas T): T` | |
| `x as any` | 删掉 | 没有它要去的地方了 |
| `any[]` 装同一种东西 | `T[]` | |
| `any[]`、`(int, any)` 装**不同**类型的东西 | 元组 `(int, string)`、结构体 | 元素个数与各自类型编译期已知时 |
| `var v: any` 先后装两种类型 | 不支持；用两个变量，或 `T?` | 这是真正的动态类型，属于 B |
| `with <tag: any>` | `with <tag: typeas T>` | 【未查】`with` 形参上的 `typeas` 是否已支持 |
| `let f: (x: string) => any` | 写出真实的返回类型 | |

泛型现有的限制会在这里被碰到【读码 `docs/technical/28_generic_functions_and_type_functions.md` 第 7 节】：泛型函数不能被修饰（`GenericFunctionUnsupported`）；`f<w>` 对泛型的 `f` 不是部分应用；类型实参不能在调用处显式写出。前两条是实现上的缺口，第三条是设计原则第 15 节的未决项（Q3）。

**两种做法**（Q1）：

- 甲：用户代码里出现 `any` 就是编译期错误，诊断指向 `typeas`。这与 `docs/language/03_data_model.md` 今天的说法一致。
- 乙：照裁定的字面意思——"只是一种简化的写法"——把**形参位置**的 `any` 定义为一个匿名类型变量的简写：`func keep(x: any, n: int)` 就是 `func keep(x: typeas _1, n: int)`。返回类型位置的 `any` 等于不写返回类型（由函数体推断）。其余位置（`as any`、带标注的 `let`、类型声明的右边、`with` 与函数类型里）是错误。这样现有的 `func a(x: int): any { return x }` 不改一个字，图上是 `(x: int) => int`，打印 `2`。

两种做法在图上的结果相同（只有具体类型），差别只在用户看到什么。

### 4.3 B 类：`json.decode`

这是唯一"形状运行时才知道"的地方。先看今天已经有的：

```camel
// examples/t06b_json_typed.cml
let xs  = decode_as('[1, 2, 3]', [0]) !! [0]      // [1, 2, 3]
let bad = decode_as('["a"]', [0]) !! [-1]         // [-1]：形状不符是错误值，不是悄悄读错
```

`decode_as` 把"要解码成的类型"作为一个只取类型、不读值的 `shape` 实参交进去，结果是 `T!`，解码时逐层检查【读码 `modules/json/operators.cpp:183-400, 478-500`；实测】。它满足 T1–T5。它的写法是 `DESIGN-QUESTION(json-typed-decode)` 留下的权宜之计（"一次调用没法交一个类型进去"），见 Q3。

`decode_as` 做不到的是"事先不知道形状"：配置文件里可有可无的键、元素类型不一的数组、要先看一个字段再决定怎么读其余部分的消息。对这类需求有三条路（Q2）：

- 甲：不支持。`json.decode` 删除，只留 `decode_as`。
- 乙：`json.decode` 返回模块类型 **`Json`**。一个 `Json` 值是一个带种类标记的对象（4.4），静态类型就是 `Json`——确定的类型。取东西用算子，结果是 `T?` 或 `T!`：

  ```camel
  // 拟议
  let j = decode(text) !! json::null()          // decode: (string) => Json!
  let port = j->get<'port'>->as_int ?? 8080     // get: Json?，as_int: int?
  let names = j->get<'names'>->as_array<[] as string[]> !! []
  ```

  这与 `PyObject` 是同一种东西：一个由模块定义、回收器认识的不透明类型，配一组算子。它不是通用联合类型，语言核心不需要知道它里面有什么（T5 不变）。
- 丙：把 `any` 留下，改造成通用的带标记动态值：每个值进入 `any` 时装进一个带类型指针的盒子，`as T` 变成检查，结果是 `T!`。任何类型都能进，程序能拿 `as` 试出它是什么。

### 4.4 剩余的动态值怎样摆放、怎样被回收器追踪

无论选乙还是丙，剩下的那一小类"运行时才知道内容"的值都落在同一个已有的机制上：**自描述对象**。回收器今天追踪一个对象有两种方式：拿着到达它的槽的静态类型去追踪（数组、元组、结构体）；或者传 `nullptr`，由对象自己的 `updateRefs` 知道往哪看（函数值、张量、外部资源包装）【读码 `docs/technical/36_gc_handle_rules.md` 规则 7、第 7 节第 11 项；`src/core/mm/gen.cpp:798`】。带标记的值走第二种，回收器的代码不用改。

**乙的排布（`Json`）：**

```
类型：  Json = registerOtherType("Json", TypeFlag::GC_Traced)      // 与 PyObject 同样的登记方式
对象：  JsonValue : rtdata::Object            // 在自动空间里，可移动
        ┌───────────────┬──────────────────────────────────────────────┐
        │ kind : 1 字节 │ payload : 8 字节（一个槽）                    │
        └───────────────┴──────────────────────────────────────────────┘
        kind      payload                         payload 的类型（固定，不存在对象里）
        Null      —                               —
        Bool      位                              bool
        Int       位                              int
        Float     位                              float
        String    String *                        string
        Array     Array *（元素是 JsonValue *）    Json[]
        Object    Table *（键 string，值 JsonValue *）  Map<string, Json>
追踪：  JsonValue::updateRefs(relocate, 忽略传入的类型)：
            kind 是 String / Array / Object 时  payload = relocate(payload, 上表第三列的类型)
        第三列的三个类型是进程里各一份的常驻类型对象，于是从一个 Json 往下每一层都是带类型的追踪。
写屏障：对象在一次 decode 调用里建成，之后不可变，用不到（规则 6）。
```

一个 `Json` 标量占 8（对象头）+ 16 字节；数组与对象复用现有的 `Array` 与 `Table`。`json.decode` 今天为每个 JSON 对象在永久空间新建一个结构体类型且从不释放（1.2），这一条随之消失。

**丙的排布（通用动态值）：**

```
对象：  Dyn : rtdata::Object { Type *type;  slot_t value; }       // 16 字节 + 8 字节对象头
        type 指向永久空间里的类型对象（不移动、不回收，规则 10）
追踪：  Dyn::updateRefs：type 带 GC_Traced 时  value = relocate(value, type)
进入：  x as any  ⇒  分配一个 Dyn{ typeof(x), x }            （每个标量一次分配）
取出：  d as T    ⇒  d.type 与 T 结构相等则给 value，否则给错误值；结果是 T!
```

代价：每次进入一次分配；`as` 时一次类型的结构比较（类型今天不是规范化的，`docs/technical/28` 第 7 节：两个 `T[]` 是不同的对象，要用 `equals` 比）；以及它实际上引入了运行时的类型分辨，与 3.8 冲突。

**不建议的摆法**：把槽加宽成（类型，值）两个槽——四个后端的帧、对象、调用约定全要动；在槽里借位做标记——`int` 是完整的 64 位，没有位可借；让标记-清除对 `any` 槽做保守扫描——只能保住对象本身，往下仍然没有类型（2.5 的第二层），而且对分代无效。

### 4.5 需要语言新能力的地方

本稿只列出，不做决定：

| 需要 | 谁需要 | 今天怎么凑合 | 对应问题 |
|---|---|---|---|
| 把一个类型交给一次调用（"解码成 `int[]`"） | `decode_as`；将来任何"按类型造值"的算子（解析、读文件、`zeros`） | 传一个只取类型不读值的 `shape` 实参：`decode_as(text, [0])`、`decode_as<[] as Map<string, int>>(text)` | Q3 |
| 签名说明里表达"结果类型取自某个实参""任意个任意类型的实参" | `stdlib/signatures/` 与文档 | 写 `any` | Q4 |
| 用 Camel 写参数个数与类型都不定的函数（自己的 `println`） | 今天没有使用者；只有 C++ 算子能这样 | — | Q5 |

除此之外，A 类与 C 类的迁移不需要新的语言能力：`typeas` 与类型规则都是现成的。

### 4.6 收尾：删掉 `TypeCode::Any`，立不变量

A、B、C 清空后：

- 删除 `Type::Any()`、`TypeCode::Any`、类型作用域里的 `any`（除非 Q1 选乙，那时它只在前端作为形参简写存在，不产生类型对象）、`checkCastSafetyWithAny`、数组推断与装箱里的特判、1.2 列出的 6 处防御性特判。
- 图的校验加一条：节点类型、端口类型、函数类型的各个成分、静态区各槽的类型都不带 `Auxiliary` 标志。这对应图模型设计稿 3.10 的类型级不变量；`Ref` 今天在三处校验里被当作"跳过"的类型（`src/runtime/graph.cpp:223`、`src/compile/gir/graph_encode.cpp:106`、`src/runtime/draft_session.cpp:71`），是否也能挡在图外【未查】。
- 设计原则裁定记录 2.7 的"`<any>` 是正常现象"一条失去对象，建议标为已由 5.10 取代；`docs/technical/36` 第 9 节的 `any` 问题、第 7 节的第 21 与 23 项关闭；`docs/technical/37` 第 7 节"暂不把分代设为默认"的两条理由去掉第一条。

---

## 5. 迁移顺序

每一步能独立合入，合入后全套测试通过。规模是净改动的估计。

| 步 | 内容 | 规模 | 风险 | 用户会碰到什么 |
|---|---|---|---|---|
| 0 | **止血**（不依赖任何裁定）：修 D1（`any` 进 `any?` 不装箱）、D2（数组字面量吞掉 `any`）、D5（`any` 作 `if` 条件与 `match` 对象：改为编译期错误）；更正 `36` 号文档第 9 节"立刻转换是安全的"的说法（D4） | 约 60 行 | 低 | 原来能编译、结果是乱码或崩溃的三种写法变成编译期错误 |
| 1 | **量出来**：图校验里加"节点类型带 `Auxiliary`"的**警告**（先不报错），全套测试与 `progen` 跑一遍，得到带 `any` 的图的确切清单 | 约 40 行 | 低 | 无 |
| 2 | **A 类**（4.1）：9 处登记的签名；重新生成签名说明与标准库参考 | 约 100 行，净减 | 低 | `encode(函数值)` 之类从运行期故障变成编译期错误；文档里 `println` 的签名不再写 `any` |
| 3 | **C 类**（4.2，按 Q1 的裁定）：甲——用户代码里的 `any` 报错；乙——形参位置的 `any` 脱糖成匿名类型变量，返回位置等于不写，其余报错。先以警告发一个版本 | 甲约 40 行；乙约 150 行（前端一处脱糖 + 诊断） | 甲低；乙中（泛型的三条限制会被更多程序碰到：不能修饰、不能部分应用） | 甲：`func f(x: any)` 要改写成 `typeas`。乙：多数程序不用改，`x as any` 要删，`println(wrap(n))` 从 `<any>` 变成 `42` |
| 4 | **B 类**（4.3，按 Q2 的裁定）。乙：`Json` 类型、对象、追踪、打印、相等、8 个左右的访问算子、`encode` 接受 `Json`；`decode` 的返回类型改为 `Json`（或 `Json!`） | 乙约 500–600 行 C++，加测试 | 中：新的对象类型要过 `36` 号文档第 6 节的三项检查（毒化、强制回收 + 提升年龄 1、静态搜索） | `decode(s) as T` 不再编译；改成 `decode_as(s, shape) !! …`（形状已知）或 `Json` 的访问算子（形状未知）。仓库内：2 个测试文件 7 处 |
| 5 | **收尾**（4.6）：删 `TypeCode::Any` 与 12 个文件里的特判；第 1 步的警告升为错误；更新设计原则裁定记录 2.7、`36`、`37` 号文档与语言文档 | 净减约 150 行 | 低到中：`modules/json/operators.cpp` 里有三处拿 `Type::Any()` 当"类型缺失"的兜底，要逐处确认走不到 | 无（前几步已经改完） |
| 6 | **之后才可以做的事**：分代回收设为默认（`37` 号文档第 7 节，另需 Windows 上的一轮测试）；`std::cpp` 放宽类型范围时不必考虑无类型的槽 | — | — | — |

依赖：0、1、2 互不依赖，可以马上做；3 等 Q1；4 等 Q2（若选乙且想要顺手的写法，还等 Q3）；5 在 2、3、4 之后。**3 与 4 都先以警告发布一个版本**，因为它们是仅有的两步会让现有用户程序停止编译的。

仓库内要改的用户程序合计：`test/cases/compile/return_type/accepted.cml`（1 处，Q1 选乙时只改期望输出）、`test/cases/modules/std/json/decode.cml`（6 处）、`test/cases/compile/diag_render/runtime_json.cml`（1 处）；`benchmarks/pipeline/exclusions.json` 的两条排除项可以撤掉；`test/cases/tools/grammar/types.cml:19` 是语法着色的测试，`any` 留不留在着色词表里是工具的事。

---

## 6. 要负责人确认的问题

已经裁定的不在这里（"不应当存在到了运行时还不确定的类型"；不做通用联合类型的运行时分辨；泛型按实参类型实例化、图上只有具体类型）。

### Q1 用户代码里写 `any`，是错误，还是"匿名类型变量"的简写

```camel
func keep(x: any, n: int): any { … }
let v = keep('abc', 3)
```

- 甲：编译期错误，诊断给出改写：`func keep(x: typeas T, n: int): T`。语言里从此没有 `any` 这个词。
- 乙：形参位置的 `any` 是匿名类型变量的简写，上面的函数就是泛型函数，`keep('abc', 3)` 实例化为 `keep<string>`，`v` 的类型是 `string`；返回类型位置的 `any` 等于不写返回类型。`as any`、`let a: any`、`any[]` 出现在形参之外的位置是错误。
- 丙：保留今天的含义，值带运行时标记（即 Q2 的丙）。

**建议乙。** 它是"只是一种简化的写法"的字面实现：写的人不必给一个只用一次的类型变量起名字，图上仍然只有具体类型。代价是泛型实例的个数从签名上看不出来（`typeas` 至少显眼），以及返回位置的 `any` 与"不写"重复。若负责人认为语言里不该有两种写泛型的办法，选甲，迁移成本只多出"仓库内 1 处、仓库外未知"的改写。丙与 2.9 冲突，不建议。

### Q2 `json.decode`：形状事先不知道的数据怎么办

```camel
let d = decode(text)          // 今天：any
```

- 甲：删除 `decode`，只留 `decode_as`（结果 `T!`，解码时检查）。形状不定的 JSON 不支持。
- 乙：`decode` 返回模块类型 `Json`（4.3、4.4），用算子取成员，取出的结果是 `T?`/`T!`。
- 丙：`any` 变成通用的带标记动态值，`as T` 检查并返回 `T!`。

**建议乙，并把 `decode_as` 作为文档里的首选写法。** 甲最干净，但配置文件、日志、别的程序给的消息里"可有可无的键"是常事，没有乙就只能在 C++ 里另写算子。丙让任何值都能变成动态的，是把 3.8 不要的东西从后门请进来；乙把动态性关在一个模块类型里，语言核心、图、回收器都只看见一个确定的类型 `Json`，与 `PyObject` 同理。乙的成本是一个新的对象类型和大约 8 个算子。

### Q3 怎样把"要得到的类型"交给一个算子

设计原则第 15 节已有"泛型函数的类型参数是否允许调用时显式给出"；`modules/json/module.cpp:53` 的 `DESIGN-QUESTION(json-typed-decode)` 是同一个问题。去掉 `any` 之后它从"可以绕开"变成"每个解码的人都会碰到"。

```camel
let a = decode_as(text, [0]) !! []                         // (1) 今天：形状实参，只取类型
let b = decode_as<[] as Map<string, int>>(text) !! …       // (1) 的 with 写法
let c: int[]! = decode(text)                               // (2) 从目标位置取类型
let d = decode(text) as int[] !! []                        // (3) as 把类型交给算子，as 是检查
```

- (1) 保持现状。缺点：要凭空造一个那种类型的值（`[0]`、`[] as Map<…>`），结构体尤其别扭。
- (2) 类型规则能看到"调用处期望的类型"。这与第 15 节的 `entering-operator-args`（"让类型规则能报出我接受的类型"）是同一个机制的另一半；3.8 已经让空数组字面量从目标位置取类型。
- (3) `as` 的目标类型交给被转换的那个算子的类型规则，结果是 `T!`。写法与 `[...] as Map<K, V>` 一脉相承。
- (4) 允许调用处写类型实参。尖括号已经被 `with` 实参占用，需要另找写法。

**建议 (2)，在它落地之前保持 (1)。** (2) 不添加语法，并且顺带回答 `entering-operator-args`。(3) 读起来最顺，但它让 `as` 在一种情况下从"不会失败的转换"变成"可能失败、结果是 `T!` 的检查"，同一个关键字两种性质。这是语言设计，请负责人定。

### Q4 签名说明里怎样写"什么类型都收"

`println: <fmt?: string> (...args: any) => void`、`string: (x: any) => string` 是给人看的文本（文档、悬停提示、诊断里的候选列表）。

- 甲：换成类型变量的记法：`string: (x: T) => string`、`println: (...args: T...) => T`（`idx_arr` 的文本今天已经写 `(arr: T[], index: int) => T`）。
- 乙：文本里保留 `any`，在语言文档里说明它只是签名说明里的记号。

**建议甲**：`any` 这个词从用户能看到的所有地方消失，免得有人照着写。`T...` 这个记号只在说明文本里用，不是语法。

### Q5 用 Camel 写参数个数与类型都不定的函数

今天只有 C++ 算子可以（`println`、`format`）。去掉 `any` 之后，"用 Camel 给 `println` 包一层"仍然写不出来——但今天也写不出来（没有变长形参的语法给用户函数用）。

- 甲：不做，记为已知的边界。
- 乙：设计类型包（`...args: typeas Ts`）。

**建议甲**：没有使用者；包一层日志函数可以收一个已经 `format` 好的字符串。

### Q6 过渡期里 `any` 值的三个现有行为

在第 3、4 步完成之前，`any` 还在。下面三处今天能编译、结果没有意义（附录 B 的 D5、D1、D2），建议在第 0 步就改成编译期错误，不等其余裁定：

- `if a then …`（`a: any`）与 `match v { … }`（`v: any`）；
- `any` 进入 `any?`、`any!`；
- 数组字面量里 `any` 元素与别的类型的元素混排。

请确认可以直接判错。

### Q7 设计原则与文档里的两处

- 裁定记录 2.7"静态类型为 `any` 的值打印为 `<any>` 是正常现象"：迁移完成后没有这样的值。建议那时在裁定记录 2.7 该条后注明"已由 5.10 取代"。
- `docs/language/03_data_model.md:29` 说"用户代码里不能把值声明为 `any`"，实现并不拒绝（D3）。按 Q1 的结果改文档或改实现。

---

## 7. 最不放心的地方

1. **仓库外有多少程序在用 `any`，不知道。** 仓库内的数字很小（1 处用户写的 `any`，7 处 `decode`），但这只说明测试没有覆盖它，不说明没人用；`json.decode` 是标准库参考里有例子的公开算子。第 3、4 步先警告一个版本是唯一的缓冲。

2. **Q1 的乙把更多函数变成泛型，会撞上泛型现有的限制。** 泛型函数今天不能被修饰、`f<w>` 不是部分应用、一个名字不能既有泛型又有普通重载。原来写 `any` 能通过的程序，脱糖成泛型后可能因为这三条之一而不能通过。本稿没有逐条试，只读了 `28` 号文档第 7 节；`with <tag: any>` 脱糖成 `with <tag: typeas T>` 是否已被支持也没有查。

3. **`Json` 类型的访问算子长什么样，本稿只给了一个示意。** `j->get<'port'>->as_int ?? 8080` 这条链里 `get` 作用在 `Json?` 上需要 `?->` 的可空版本（今天 `?->` 只对错误定义，`src/compile/gir/builder.cpp:5461` 的 `DESIGN-QUESTION(nullable-error) Q6`），否则每一步都要拆一次。这组算子好不好用决定乙是不是真的比甲强，需要单独的小设计。

4. **"`CAST` 被排在使用者之前"是推断。** `t21` 出错、`t08` 不出错，两者合起来只有这一种解释说得通，但本稿没有打印四个后端的执行序逐条核对。若实际原因不同，D4 的表述要改，不过"`decode(s) as T` 不安全"这个实测结论不受影响。

5. **第 5 步之后"图上没有 `Auxiliary` 类型"是否真的成立，没有验证。** `Ref` 在三处校验代码里被特意放过，说明至少曾经有带 `Ref` 的类型到达过运行期的图。第 1 步的警告就是为了先量出来；如果量出来的不只是 `any`，这条不变量要分两次立。

6. **实测只在一个平台、一台负载很高的共享机器上做。** Linux x86-64，发布构建；崩溃与错值的具体形态（哪个 `n` 崩、打印哪个错值）取决于分配的时序，换一台机器数字会变。结论"对照程序全对、`any` 程序出错"不应当变，但没有在别处验证。Windows 上毒化模式不可用（`37` 号文档 6.5）。

7. **`std::prl` 下 `t09` 有一格是超时而不是崩溃**（默认回收器 + 强制 major，40 秒）。没有查它是死锁、还是只是慢；并行调度器下回收要等所有工作线程离开区域，悬空指针造成的后果可能与单线程不同。

---

## 附录 A 复现

在仓库根目录、已构建的环境里执行（`out/latest/bin/camel` 存在，或 `camel` 在 `PATH` 上；`python3`、`bash`）。

```
# 第 1 节：普查
python3 docs/design/any-typing/tools/any_census.py
python3 docs/design/any-typing/tools/any_census.py --list

# 2.1–2.8：每个示例跑一遍（默认 std::fvm；可以给别的后端作参数）
bash docs/design/any-typing/tools/run_examples.sh
bash docs/design/any-typing/tools/run_examples.sh std::nvm

# 2.3：图
camel docs/design/any-typing/examples/t01_declare.cml std::rgir
camel inspect --gir docs/design/any-typing/examples/t01_declare.cml

# 2.7：内联前后
camel docs/design/any-typing/examples/t18_inline_changes_print.cml
camel docs/design/any-typing/examples/t18_inline_changes_print.cml std::opt::inline std::fvm

# 2.8：std::cpp
camel docs/design/any-typing/examples/t01_declare.cml std::cpp
camel docs/design/any-typing/examples/t06_json.cml std::cpp

# 2.9：回收器矩阵
E=docs/design/any-typing/examples
bash docs/design/any-typing/tools/gc_matrix.sh $E/t07_gc_frame.cml $E/t09_gc_array_elem.cml \
     $E/t10_gc_tuple_field.cml $E/t11_gc_user_func.cml $E/t21_cast_at_once.cml \
     $E/t12_typed_control.cml $E/t13_generic_instead.cml
BACKENDS="std::jit std::prl" bash docs/design/any-typing/tools/gc_matrix.sh \
     $E/t07_gc_frame.cml $E/t09_gc_array_elem.cml $E/t11_gc_user_func.cml $E/t21_cast_at_once.cml
PASSES=std::macro bash docs/design/any-typing/tools/gc_matrix.sh $E/t16_macro_const.cml
PASSES=std::opt::inline BACKENDS=std::fvm bash docs/design/any-typing/tools/gc_matrix.sh \
     $E/t11_gc_user_func.cml $E/t07_gc_frame.cml

# 2.9：默认回收器、不加开关
camel $E/t20_default_collector.cml --arg n=1500000 std::fvm     # 段错误
camel $E/t20b_default_typed.cml    --arg n=1500000 std::fvm     # 3000000 a-long-payload-…
```

| 目录 / 文件 | 内容 |
|---|---|
| `examples/t01*`–`t06*` | `any` 能写在哪、`as` 不检查、打印、对 `any` 值能做的操作、`json` |
| `examples/t07`–`t13`、`t16`、`t20*`、`t21` | 回收器实验与对照；`main` 带参数 `n` |
| `examples/t14`、`t15`、`t17` | 联合类型、空数组、Python 桥：不是 `any` 的三样东西 |
| `examples/t18`、`t19` | 内联前后的输出；`any` 的其余位置 |
| `examples/d1`、`d2` | 附录 B 的 D1、D2 |
| `tools/any_census.py` | 普查 |
| `tools/run_examples.sh`、`tools/run.sh` | 逐个运行示例，印出前几行输出与退出码 |
| `tools/gc_matrix.sh` | 一个程序 × 五种回收器设置 × 若干后端 |

## 附录 B 调研中发现的现有缺陷

只记录，未修改。

| 编号 | 现象 | 最小复现 | 期望 |
|---|---|---|---|
| D0 | 类型为 `any` 的槽里的引用回收器看不见，两种回收器都是（`36` 号文档第 7 节第 21 项已记录）。本稿新增的事实：默认回收器、不设任何环境变量即可崩溃；`any[]` 元素、元组/结构体字段、用户函数的 `any` 形参、`std::macro` 的常量各有复现 | `camel docs/design/any-typing/examples/t20_default_collector.cml --arg n=1500000 std::fvm`：段错误；其余见 2.9 | 第 4、5 节 |
| D1 | `any` 值进入 `any?`（或 `any!`）时不装箱：`TupleType::castSlotFrom` 见到源类型是 `any` 就原样传递（`src/core/type/composite/tuple.cpp:414-417`），之后 `??` 把这个槽当盒子读 | `camel docs/design/any-typing/examples/d1_any_nullable.cml`：段错误（默认回收器，`nvm` 与 `fvm`） | 装箱，或拒绝 `any?` |
| D2 | 数组字面量里 `any` 元素后面跟一个有类型的元素，数组取后者的类型，`any` 元素不经 `as`、不经检查就成了那个类型（`src/core/type/composite/array.cpp:84-87`） | `camel docs/design/any-typing/examples/d2_array_literal.cml` 打印 `5 true` 与 `true`：`[wrap_s('ab'), 5]` 是 `int[]`，第 0 个元素是字符串的地址，可以做算术 | `ElementTypeMismatch` |
| D3 | `docs/language/03_data_model.md:29` 说用户代码不能把值声明为 `any`；实现接受形参、返回类型、`as any`、`with` 形参、函数类型里的 `any`，只拒绝字面量与带标注的 `let` | `camel docs/design/any-typing/examples/t01_declare.cml` 通过；`t01b_literal.cml`、`t01c_let.cml` 被拒绝 | 文档与实现一致（Q1） |
| D4 | `docs/technical/36_gc_handle_rules.md` 第 9 节称 `decode(s) as T` 在 `std::nvm`、`std::fvm` 上安全（"两个节点之间没有安全点"）。`CAST` 是没有控制边的纯节点，实际执行位置在使用者之前而不是 `decode` 之后 | `bash docs/design/any-typing/tools/gc_matrix.sh docs/design/any-typing/examples/t21_cast_at_once.cml`：四个后端在强制回收下崩溃或打印 `300 299` | 更正文档；`test/cases/modules/std/json/decode.cml` 的 6 处写法都受影响 |
| D5 | `any` 值可以作 `if` 的条件（槽被当 `bool` 读），可以作 `match` 的对象（与字面量比较槽的位，字符串按地址比，总是落到默认分支） | `camel docs/design/any-typing/examples/t05f_branch.cml` 打印 `1`；`t05j_match.cml` 对装着 `'ab'` 的值打印 `other` | 编译期错误（Q6） |
| D6 | `stdlib/signatures/json.cml:40-41` 把 `decode_as` 写成 `<shape: any> (text: string): any`，实际返回 `shape` 的类型 `T` 的 `T!`；`json.decode` 为每个 JSON 对象在永久空间新建一个结构体类型，从不释放（`modules/json/operators.cpp:415-427`，【读码】，未量泄漏量） | `camel docs/design/any-typing/examples/t06b_json_typed.cml` 打印 `[1, 2, 3]` 与 `[-1]` | 签名说明能表达结果类型（Q3、Q4）；`Json` 类型下不再造结构体类型 |

D1、D2、D5 是 `any` 自己的规则漏洞；D4 是文档的断言不成立；D0 是本稿的主题。
