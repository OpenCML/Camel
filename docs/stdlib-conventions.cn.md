# 标准库约定

本页是标准库后续每一步的裁判。设计原则仍然优先：本页只把原则落到标准库的具体写法上，冲突时以 `docs/design-principles.cn.md` 为准。新函数按本页写；已有的不一致按模块分批改掉，不保留别名。

1. [一个事实只写一处](#1-一个事实只写一处)
2. [声明](#2-声明)
3. [命名](#3-命名)
4. [参数与链式](#4-参数与链式)
5. [错误](#5-错误)
6. [模块放在哪里](#6-模块放在哪里)
7. [文档](#7-文档)
8. [声明写不出的签名](#8-声明写不出的签名)

## 1. 一个事实只写一处

| 事实 | 写在哪里 |
|---|---|
| 固定签名（参数、`var`、返回类型、`with` 参数） | 模块的 `.cml` 里**没有函数体的声明**（`export func f(x: T): R`） |
| 计算出来的签名（返回类型取自实参、按形状展开） | 类型规则在 C++ 的 `DynamicFuncTypeResolver` / `OpDef`，注册时声明；参考条目与文档在 `stdlib/signatures/<模块>.cml`，由模块条目的 `computed` 指名 |
| 纯度（`@pure` 当且仅当不读环境、不写环境、同输入同输出） | C++ 的 `OperatorTable::declare`；键式随机是纯的，全局生成器不是 |
| 资源（`stdout`、`stderr`、`stdin`、`fs`、`clock`、`env`、`net`、`rng`、`process`） | C++ 的 `declareResources`，用 3.3 的标准名字 |
| 数值语义、原地形式 | C++ 的 `declareSemantics` / `setInPlaceForm` |
| 文档（描述、`@example`、`@fails`） | 声明上方的 `///` 注释 |

C++ 模块不再重复写签名：没有函数体的声明就是签名本身，调用方按它检查，运行时由 `module:function`（例如 `bytes:len`）找到算子的执行器。声明名与算子名不同、或同一模块的算子用别的前缀（`random` 的 `rand:`）时用 `#op<'...'>` 标出；另一个模块的算子不能在这里声明。`docs/stdlib-reference.md` 由声明与计算签名文件生成，不手改。

- 一个名字按类型有各自的执行器 URI 时（`math` 的 `abs_i`、`abs_l`、…），声明逐个写、各带确切的 `#op<'模块:算子'>`；不能用一个 `(typeas T)` 的泛型声明代替，因为泛型声明仍只绑定一个 URI，而执行器是按类型分名的。签名文件里的 `*` 通配只作参考，不是声明可用的写法：`ExecutorManager` 与 `OperatorTable` 都按确切的 URI 查找。
- **一个计算签名只在 `stdlib/signatures/<模块>.cml` 一处记录，由模块条目的 `computed` 字段指名；声明文件里不写 `// dynamic func`，同一个名字不能两处都有、也不能两处都没有。** 计算签名的算子（C++ 的 `DynamicFuncTypeResolver`，结果类型取自实参）的类型规则留在 C++；参考条目、`///` 文档与 `@example` 写进签名文件，写成无函数体的 `export func`，只作参考、不参与编译。内联写法是一条注释，URI 只能读成 `<模块>:<名字>`，没有 `#op` 的位置——`tensor:sum_axis` 的 Camel 名字是 `sum`，`nn:softmax_cross_entropy_labels` 的是 `softmax_cross_entropy`，`python:py_call_kw` 的是 `py_call`——所以文件是唯一能覆盖全部计算名字的记法。`math` 的 `sqrt`（`i32`→`f32`、`int`→`float`）与 `shl`/`shr`（结果取第一个参数的类型）现在记在 `stdlib/signatures/math.cml`，`graph`、`json`、`random` 同样；参考页与 `--verify-ops` 把声明文件与它合起来对账。哪些签名算不出来、为什么，见第 8 节；本页只定记录位置。
- 这条规则由 [`scripts/stdlib-declarations.mjs`](../scripts/stdlib-declarations.mjs) 的 `--check` 守住，检查器把它读成一句话：“a computed name has exactly one place: `stdlib/signatures/<module>.cml`, named by the MODULES entry's `computed:` field (handover H180)”——内联记录、两处都有、`computed` 指名却一个名字都不记录，都报问题；两处都没有由它的导出组检查报 “no computed signature records it”。跑它的是 [`stdlib_registry.plan.toml`](../test/plans/feat/modules/stdlib_registry.plan.toml) 里的 `registry.migrations_stay_migrated`，同计划的 `registry.signatures_match` 跑 `--verify-ops`；两条都在 `feat/modules`。
- C++ 导出的值（`math::pi`）没有可写的声明：`export let` 必须有初始值，写了就是第二份值。声明模块里用 `// value 名字: 类型` 的注释记录参考页条目，`///` 说明与 `@example` 照常写在它上方；值本身仍由 C++ 导出。

## 2. 声明

- 原生实现的函数写成**没有函数体的 `export func`**：没有 `native`、`inner` 之类的新关键字。写了函数体就是普通函数；声明模块里报错，签名文件里由 `--verify-ops` 拒绝。
- 带尖括号参数的声明把 `with <...>` 写在声明上方，与有函数体的函数相同；尖括号参数属于签名，调用点按它绑定，诊断里列出的签名也带着这些参数名。
- 参数名是签名的一部分，必须能当标识符；与关键字撞名时换一个合法写法（`str::replace` 的 `from` 写成 `from_`），类型不变，文档与诊断用新名字。
- 声明只能出现在模块顶层。同一模块里一个名字要么是普通函数，要么是声明，不混用。
- 声明与 C++ 算子的对应关系默认是 `<模块>:<函数>`，前缀是模块**声明**的名字：写在入口文件里也一样（`module bytes` 的入口文件里，`export func size(content: Bytes): int` 是 `bytes:size`，不是 `main:size`）。
- 声明只能命名**本模块自己的算子**：`#op` 用于算子名不是 Camel 标识符、或本模块算子的 URI 前缀不叫模块名（`random` 的 `rand:`）的情况。写另一个模块的算子（在 `bytes_tag` 里写 `#op<'bytes:size'>`）在声明处报错，不静默替换那个模块的签名。
- 没有 `export` 的声明是本模块**私有**的算子：模块自己的函数可以调用它（通常就是包裹原生算子的那层），导入方看不到这个名字，`module::name` 也到不了。带 `export` 的才是模块的实体。
- 有类型变量（`typeas T`）的声明是一个函数族：调用时按实参实例化，URI 仍是一个，图上是一个 OPER 而不是 CALL。
- 声明的 URI 必须对应一个已注册的执行器与一条已登记的算子性质；`#op` 的 `*` 通配因此只出现在不编译的签名文件里，声明模块里写确切 URI（H113）。
- 签名文本保留参数名：`export func size(content: Bytes): int` 的签名读作 `(content: Bytes) => int`，诊断与 `docs/stdlib-reference.md` 都照这样打印。参数名是给读者的，选择只看类型。
- 与 `.cmo` 同名的 `.cml` 不是 C++ 模块的影子，就是**那个模块**：C++ 导出的类型与常量在同一份 `.cml` 里不导入就能写；`.cml` 的 `export type`、`export let`、`export func` 写进同一个模块实体，导入这个名字的程序读到的是同一套名字。C++ 模块导出同名算子组而 `.cml` 又声明了它时，以声明为准（声明是签名的唯一来源），不并存两份。
- 作为声明模块的 `.cml` 顶层的函数全部没有函数体；写了函数体要报诊断，而不是静默丢掉。没有同名 `.cmo` 的 `.cml` 是普通模块，可以混写普通函数与声明（声明绑定到已有的算子）。
- 声明所绑定的算子必须已加载：执行器存在、纯度已声明。缺一个就在加载该模块时定位到声明报错，不留到运行期。
- 同一名字的两个非泛型声明如果参数完全相同，视为重复声明。两个泛型声明之间按同样的标准检查重叠：只要存在一次调用两条规则都可能接受，就在第二条声明处报错。一次调用用哪条规则只能由实参类型决定，不能由声明写下的顺序决定。固定的声明比泛型的声明更具体；一次调用两者都能接受时用固定的那条。
- 一个名字按参数的固定形态给不同结果时，按形态各写一条声明，共用 `<模块>:<名字>` 的 URI：`json` 的 `get` 与访问器对 `Json`、`Json?`、`Json!`、`Json!?` 各一条，依次给 `R?`、`R?`、`R!?`、`R!?`。顺序从最窄到最宽，一次调用落到第一条接受它的签名；联合类型退役之后，重载就是这样写的。尖括号参数写在声明上方、只属于紧接的那一条声明：同一个名字的多条声明各自重写 `with <...>`，不继承上一条的；漏写的一条就是普通参数的签名。
- 声明写不出来的算子留在 C++ 的 resolver，它仍是那条签名的唯一来源：返回类型取自类型规则（`json::encode` 要求值的类型有 JSON 形式）或取自类型实参（`json::decode_as` 的 `shape`）时，声明给不出这条规则，写一条近似声明会让本该拒绝的调用通过。这样的条目不再写进 `stdlib/<模块>.cml`，参考页的那几条来自模块条目的 `computed` 指名的 `stdlib/signatures/<模块>.cml` 里剩下的条目，`--verify-ops` 把两处合起来与已注册的算子对账。
- 声明也是模块的算子重载：本模块里 `a + b` 能找到自己在 `.cml` 里声明的 `operator +`，与找到自己写的、带函数体的那个一样。

## 3. 命名

- 小写加下划线：`read_text`、`from_array`、`sort_by`。
- 函数名是动词（或动词短语）；名词留给取值器 `len`、`size`、`keys`。
- 判断用 `is_x`、`has_x`；转换用 `to_x`（`to_array`、`to_string`）；已有 `get_env` 保留。
- 同一件事只在一个模块里有一个名字：不给旧名留别名，不做 `contains`/`has` 两份。

## 4. 参数与链式

- 数据在前，配置在后：普通参数放数据（`x->trim()->split(',')` 里的接收者），尖括号放配置与函数（`s->split<','>`、`xs->sort_by<key>`、`xs->filter<p>`）。
- 尖括号里的参数不参与重载选择，调用点必须写常量；需要按值分派的参数放普通位置。
- 参数分类：标量值（二元算子）、数组（第一个维度）、轴（张量）。同名的三种运算是不同算子，放在不同模块，由导入决定用哪一个。
- 例外只有 `format<args>`：格式串是接收者，参数在尖括号里（4.4 已经写下）。

## 5. 错误

- 环境造成的失败（文件不存在、网络、解析用户输入）返回 `T!`；"可能没有"返回 `T?`；程序本身的错误（越界、违反前提）是故障，种类具体，不再笼统用 `RuntimeError`。
- 一个函数不要同时用 `T!` 和故障表示同一种失败；调用点用 `!!`、`?->`、`match` 显式处理。
- 错误种类写在目录（`docs/diagnostics.md` 与 `camel/core/error/`）里，不随手起名。

## 6. 模块放在哪里

| 写法 | 位置 |
|---|---|
| 用 Camel 写的模块 | `stdlib/<模块>.cml` |
| C++ 实现的模块 | `modules/<模块>/`（执行器、算子表、资源、语义），签名在 `stdlib/<模块>.cml` |
| 尚未声明化的 C++ 模块（过渡） | `stdlib/signatures/<模块>.cml`，只作参考、不参与编译；每搬一个模块删一个文件 |
| 已声明化模块里算不出来的签名 | 只在 `stdlib/signatures/<模块>.cml`，由模块条目的 `computed` 字段指名；只作参考、不参与编译，其余声明在 `stdlib/<模块>.cml`，两处由 `--verify-ops` 一起对账。声明文件里不写 `// dynamic func`，一个名字不能两处都有（H180；`math`、`graph`、`json`、`random` 都这样） |
| 测试用例 | `test/cases/`，计划在 `test/plans/` |

模块名就是导入名，也是算子 URI 的前缀。新模块按分层放：前奏（内置，尽量小）／基础（`str`、`bytes`、`array`、`math`、`random`、`stats`、`time`、`re`、`json`、`hash`）／系统（`fs`、`io`、`env`、`process`）／数值（`tensor` 栈）／工具（`test_support`、`bench`、`profiler`）。

## 7. 文档

参考页 `docs/stdlib-reference.md` 的每一条都要是读者真能照着做的，`///` 注释里的东西就是参考页的全部内容。规则如下，由 [`scripts/stdlib-reference.mjs`](../scripts/stdlib-reference.mjs) 的 `--verify-docs` 检查，跑它的是 [`stdlib_registry.plan.toml`](../test/plans/feat/modules/stdlib_registry.plan.toml) 里的 `registry.docs_complete`（`feat/modules`）：

- **每个打印出来的名字都要有描述**：`///` 里至少一行正文，说明它做什么、什么时候用。只有标签（`@pure` 一行）不算描述。
- **每个名字都要有 `@example`，并且它要能编译。** 检查器把例子当成一次真实调用：按位置把实参绑到声明上的参数类型（`->` 的接收者是第一个参数），按探测到的模块补上例子里用到的类型的导入，再放进一个函数里编译。所以例子不能出现没有定义的 `x`、`w`、`rules`：它要么只用声明里的名字，要么自己构造值（`[1, 2, 3]->foreach<...>`、`([('a', 1)] as Map<string, int>)->get<'z'>`）。参数名与声明不同的例子按声明改正——`nn.conv2d` 的参数是 `input`、`kernel`、`bias`，例子就写这三个名字，`x`、`w`、`b` 会被编译器拒绝。
- **写不成独立程序的例子**用 `@example-fragment <原因>` 标出，原因必填。这样的例子不编译，参考页把“未编译”和原因一起打印。没有标记又不编译的例子是错误，不是“以后再说”；标记是决定，不是省略。
- **返回 `T!` 的名字要有 `@fails`**，写明错误种类：`error value \`Kind\`` 或 `fault \`Kind\``。返回的只是裸类型变量（`T!`、`F!`）时，错误是调用方传进来的、这个函数没有定义它，描述里说明来源即可，不编一个种类。
- 值（`// value 名字: 类型`）、普通函数与计算签名同样适用上面几条：描述、`@example`、能编译。

`--audit --compile` 从同一份解析给出每个模块的表（名字、条目、缺描述、缺例子、片段、编译失败），查缺口用它；`--audit` 加 `--compile` 需要已构建的编译器。

## 8. 声明写不出的签名

一条声明写不出的判据有四条：结果类型不取自任何参数或类型实参；每个参数都能写成一个类型；一个名字的所有 URI 能写进同一组声明；写出来的声明接受的调用集合与 C++ 相同。近似声明会让本该拒绝的调用通过，少写一条规则会让本该通过的调用被拒绝，两者都不行。按这四条给不出的签名留在 C++ 的 `DynamicFuncTypeResolver` / `OpDef`，参考条目记在 `stdlib/signatures/<模块>.cml`（第 1 节）。

**已有裁定，不加语法。** 可选参数按元数写重载（缺省的那个实参不写；`?` 只出现在不编译的签名文件里，不是声明语法）；整数宽度按拼写各写一条（`int` 与 `i32` 之间没有隐式转换）；伞形参数写 `Tensor`、`(typeas A)[]` 或逐类型重载（`tensor` 的 `show` 就是这样写的）。声明替换整个算子组而不是追加，所以一个名字不能一半写声明、一半留 C++：一半写不出，整个名字留 C++（`range`、`sum`、`mean` 即此）。

**只有语言作者能答的缺口。** 任意参数表的函数类型（`rewrite` 的 `__rule`、`onnx::export_model`、`tiles` 的 `source`/`inspect`/`benchmark` 等；今天只能留 C++）；声明看不见调用点的类型实参（`from_bytes::<T>`、`load_tree`、`decode_as`）；类型变量谓词（`save_tree` 等）；模块类型的类型变量实参（`Device<T>` 接受、`PyObject<T>` 不接受，是 `typeArguments()` 覆写与否的问题）；变参（`..ts` 已落地第一阶段，剩余形参带类型与展开调用仍未做）。这些不是新语法提案，就是待裁定的问题。

**不是签名缺口，不要动。** 类型限定别名（`Tensor.shape`）已有声明替换机制；`range`/`sum`/`mean` 受"一个名字一个来源"约束；结果取自实参的算子按设计就是计算签名；逐叶结构映射（`tiles:upload`/`download`）等泛型按结构展开；`ckpt` 的 `tree_*` 缺的是执行器而不是语法。
