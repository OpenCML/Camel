# 标准库约定

本页是标准库后续每一步的裁判。设计原则仍然优先：本页只把原则落到标准库的具体写法上，冲突时以 `docs/design-principles.cn.md` 为准。新函数按本页写；已有的不一致按模块分批改掉，不保留别名。

1. [一个事实只写一处](#1-一个事实只写一处)
2. [声明](#2-声明)
3. [命名](#3-命名)
4. [参数与链式](#4-参数与链式)
5. [错误](#5-错误)（含 5.1 错误种类目录）
6. [模块放在哪里](#6-模块放在哪里)
7. [文档](#7-文档)
8. [声明写不出的签名](#8-声明写不出的签名)

## 1. 一个事实只写一处

| 事实 | 写在哪里 |
|---|---|
| 固定签名（参数、`var`、返回类型、`with` 参数） | 模块的 `.cml` 里**没有函数体的声明**（`export func f(x: T): R`） |
| 计算出来的签名（返回类型取自实参、按形状展开） | 类型规则在 C++ 的 `DynamicFuncTypeResolver` / `OpDef`，注册时声明；参考条目与文档在 `stdlib/signatures/<模块>.cml`，由模块条目的 `computed` 指名 |
| 纯度与资源（`pure` 当且仅当不读环境、不写环境、同输入同输出；资源 `stdout`、`stderr`、`stdin`、`fs`、`clock`、`env`、`net`、`rng`、`process`） | 无函数体的原生声明的签名：`pure` 或 `uses 资源`（用 3.3 的标准名字）。Camel 函数体不写，由编译器推出。键式随机是纯的，全局生成器用 `rng` |
| 数值语义、原地形式 | C++ 的 `declareSemantics` / `setInPlaceForm`（性能与数值事实，不是签名） |
| 文档（描述、`@example`、`@fails`） | 声明上方的 `///` 注释 |

C++ 模块不再重复写签名与纯度、资源：没有函数体的声明就是它们本身，调用方按它检查，运行时由 `module:function`（例如 `bytes:len`）找到算子的执行器。声明名与算子名不同、或同一模块的算子用别的前缀（`random` 的 `rand:`）时用 `#op('...')` 标出；另一个模块的算子不能在这里声明。`docs/stdlib-reference.md` 由声明与计算签名文件生成，不手改。

- 一个名字按类型有各自的执行器 URI 时（`math` 的 `abs_i`、`abs_l`、…），写成一个泛型签名、按类型分臂：每个臂是一条 `when` 互斥的 `func f::<T>` 声明（`when is_same::<T, i32>()`），各带确切的 `#op('模块:算子')`；互斥检查不接受时，退回逐类型的固定声明。一个无分臂的泛型声明只绑定一个 URI。签名文件里的 `*` 通配只作参考，不是声明可用的写法：`ExecutorManager` 与 `OperatorTable` 都按确切的 URI 查找。
- **一个计算签名只在 `stdlib/signatures/<模块>.cml` 一处记录，由模块条目的 `computed` 字段指名；声明文件里不写 `// dynamic func`，同一个名字不能两处都有、也不能两处都没有。** 计算签名的算子（C++ 的 `DynamicFuncTypeResolver`，结果类型取自实参）的类型规则留在 C++；参考条目、`///` 文档与 `@example` 写进签名文件，写成无函数体的 `export func`，只作参考、不参与编译。内联写法是一条注释，URI 只能读成 `<模块>:<名字>`，没有 `#op` 的位置——`tensor:sum_axis` 的 Camel 名字是 `sum`，`nn:softmax_cross_entropy_labels` 的是 `softmax_cross_entropy`，`python:py_call_kw` 的是 `py_call`——所以文件是唯一能覆盖全部计算名字的记法。`math` 的 `sqrt`（`i32`→`f32`、`int`→`float`）与 `shl`/`shr`（结果取第一个参数的类型）现在记在 `stdlib/signatures/math.cml`，`graph`、`json`、`random` 同样；参考页与 `--verify-ops` 把声明文件与它合起来对账。哪些签名算不出来、为什么，见第 8 节；本页只定记录位置。
- 这条规则由 [`scripts/stdlib-declarations.mjs`](../scripts/stdlib-declarations.mjs) 的 `--check` 守住，检查器把它读成一句话：“a computed name has exactly one place: `stdlib/signatures/<module>.cml`, named by the MODULES entry's `computed:` field ”——内联记录、两处都有、`computed` 指名却一个名字都不记录，都报问题；两处都没有由它的导出组检查报 “no computed signature records it”。跑它的是 [`stdlib_registry.plan.toml`](../test/plans/feat/modules/stdlib_registry.plan.toml) 里的 `registry.migrations_stay_migrated`，同计划的 `registry.signatures_match` 跑 `--verify-ops`；两条都在 `feat/modules`。
- C++ 导出的值（`math::pi`）没有可写的声明：`export let` 必须有初始值，写了就是第二份值。声明模块里用 `// value 名字: 类型` 的注释记录参考页条目，`///` 说明与 `@example` 照常写在它上方；值本身仍由 C++ 导出。

## 2. 声明

- 原生实现的函数写成**没有函数体的 `export func`**：没有 `native`、`inner` 之类的新关键字。写了函数体就是普通函数；声明模块里报错，签名文件里由 `--verify-ops` 拒绝。
- 带尖括号参数的声明把 `with <...>` 写在声明上方，与有函数体的函数相同；尖括号参数属于签名，调用点按它绑定，诊断里列出的签名也带着这些参数名。
- 参数名是签名的一部分，必须能当标识符；与关键字撞名时换一个意思相近的词（`replace` 的 `old`、`new`，不写 `from_` 这样的凑合拼写），类型不变，文档与诊断用新名字。
- 声明只能出现在模块顶层。同一模块里一个名字要么是普通函数，要么是声明，不混用。
- 声明与 C++ 算子的对应关系默认是 `<模块>:<函数>`，前缀是模块**声明**的名字：写在入口文件里也一样（`bytes.cml` 里，`export func to_array(content: Bytes): int[]` 是 `bytes:to_array`，不是 `main:to_array`；文件就是模块，名字取文件名，没有 `module` 行）。
- 声明只能命名**本模块自己的算子**：`#op` 用于算子名不是 Camel 标识符、或本模块算子的 URI 前缀不叫模块名（`random` 的 `rand:`）的情况。写另一个模块的算子（在 `bytes_tag` 里写 `#op('bytes:size')`）在声明处报错，不静默替换那个模块的签名。
- 没有 `export` 的声明是本模块**私有**的算子：模块自己的函数可以调用它（通常就是包裹原生算子的那层），导入方看不到这个名字，`module::name` 也到不了。带 `export` 的才是模块的实体。
- 有类型参数（`::<T>`）的声明是一个函数族：调用时按实参实例化，URI 仍是一个，图上是一个 OPER 而不是 CALL。
- 声明的 URI 必须对应一个已注册的执行器与一条已登记的算子性质；`#op` 的 `*` 通配因此只出现在不编译的签名文件里，声明模块里写确切 URI。
- 签名文本保留参数名：`export func to_array(content: Bytes): int[]` 的签名读作 `(content: Bytes) => int[]`，诊断与 `docs/stdlib-reference.md` 都照这样打印。参数名是给读者的，选择只看类型。
- 与 `.cmo` 同名的 `.cml` 不是 C++ 模块的影子，就是**那个模块**：C++ 导出的类型与常量在同一份 `.cml` 里不导入就能写；`.cml` 的 `export type`、`export let`、`export func` 写进同一个模块实体，导入这个名字的程序读到的是同一套名字。C++ 模块导出同名算子组而 `.cml` 又声明了它时，以声明为准（声明是签名的唯一来源），不并存两份。
- 作为声明模块的 `.cml` 顶层的函数全部没有函数体；写了函数体要报诊断，而不是静默丢掉。没有同名 `.cmo` 的 `.cml` 是普通模块，可以混写普通函数与声明（声明绑定到已有的算子）。
- 声明所绑定的算子必须已加载：执行器存在，签名里写了 `pure` 或 `uses`。缺一个就在加载该模块时定位到声明报错，不留到运行期。
- 同一名字的两个非泛型声明如果参数完全相同，视为重复声明。两个泛型声明之间按同样的标准检查重叠：只要存在一次调用两条规则都可能接受，就在第二条声明处报错。一次调用用哪条规则只能由实参类型决定，不能由声明写下的顺序决定。固定的声明比泛型的声明更具体；一次调用两者都能接受时用固定的那条。
- 一个名字按参数的固定形态给不同结果时，按形态各写一条声明，共用 `<模块>:<名字>` 的 URI：`json` 的 `get` 与访问器对 `Json`、`Json?`、`Json!`、`Json!?` 各一条，依次给 `R?`、`R?`、`R!?`、`R!?`。顺序从最窄到最宽，一次调用落到第一条接受它的签名。尖括号参数写在声明上方、只属于紧接的那一条声明：同一个名字的多条声明各自重写 `with <...>`，不继承上一条的；漏写的一条就是普通参数的签名。
- 声明写不出来的算子留在 C++ 的 resolver，它仍是那条签名的唯一来源：返回类型取自类型规则（`json::encode` 要求值的类型有 JSON 形式）或取自类型实参（`json::decode_as` 的 `shape`）时，声明给不出这条规则，写一条近似声明会让本该拒绝的调用通过。这样的条目不再写进 `stdlib/<模块>.cml`，参考页的那几条来自模块条目的 `computed` 指名的 `stdlib/signatures/<模块>.cml` 里剩下的条目，`--verify-ops` 把两处合起来与已注册的算子对账。
- 声明也是模块的算子重载：本模块里 `a + b` 能找到自己在 `.cml` 里声明的 `operator +`，与找到自己写的、带函数体的那个一样。

## 3. 命名

- 小写加下划线：`read_text`、`from_array`、`sort_by`。
- 函数名是动词（或动词短语）；名词留给取值器 `len`、`keys`。
- 判断用 `is_x`、`has_x`；转换用 `to_x`（`to_array`）；类型谓词一族都是 `is_x::<T>()`。
- **一个名字只有一个来源，一个意思只有一种写法**：不留别名，不做 `contains`/`has` 两份；能用语言的构造写的不另起函数（`xs[0]` 而不是 `head(xs)`，`xs[a:b]` 而不是再来一个 `substr`）。
- **同名同义**：一个名字在各模块里意思相同（`len` 是长度，`find<pred>` 是第一个满足的元素，`index_of<x>` 是下标，`slice` 是切片），否则换名字——字符串的 `find` 若返回下标，就叫 `index_of`。
- **不同模块里同名、作用于不同类型的函数（前奏的数组 `map`、`stream` 的流 `map`、`bytes` 的 `len`）同时可见时合成一个重载集**，不互相遮蔽；只有真正有歧义的调用才是错误。重载选择把 `with` 参数的个数与名字计算在内，`sum(x)` 与 `sum<axis>(x)` 是同一组。
- 参数名要能当标识符；与关键字撞名时换一个意思相近的词，不加下划线凑数。

## 4. 参数与链式

- **`with` 参数是函子的 `this`**：设想一个函子，构造时绑定一次、之后反复作用于数据的东西放进尖括号；每次调用流过的数据是普通参数。不要按"是不是配置"机械地分。`->` 一次只传一个值，所以串联的那个值就是普通参数：`s->trim->split<','>`。
- **一层网络就是绑定了权重的函数**：`linear<w, b>` 是那一层，输入是流过它的数据，所以一个模型读起来是一条管道：`x->linear<w, b>->relu`；`logits<m>(x)` 是权重为 `m` 的那个网络作用在 `x` 上。输出也一样：被写出的文本是数据，写到哪里是配置：`text->write_text<path>`。
- **两个对等的数据都是普通参数**：`concat(xs, ys)`、`zip_with<f>(xs, ys)`、`fold(k, i)`、`permutation(k, n)`。函数、条件、范围、键这类"决定行为"的东西进尖括号，变换类函数所变换的那个函数（`grad(loss)`）是普通参数。
- 形参可以有默认值、实参可以按名字给，普通参数与 `with` 参数同一条规则（`with <pattern: string, start: int = 0>`）；缺省的写法用默认值，不按元数写重载。名字写在持有那个参数的表里：普通参数在圆括号，`with` 参数在尖括号（`len(x: xs)`、`j->get<key: 'port'>`）。
- **含义相同的位置，各重载用同一个名字**（`json` 的 `get` 的 `key`，`len` 的 `x`），具名实参靠它定位；含义不同的位置不强行同名。名字写在声明与 C++ 规则签名两处，`--verify-ops` 逐项对账。
- 例外只有 `format<args>`：格式串是接收者，参数在尖括号里。
- `T?`、`T!` 沿 `->` 自动传递：`path->read_text->parse->validate !! default`，管道只写正常路径，失败走旁路到末尾处理一次（设计原则 3.9）。

## 5. 错误

- **三种失败，三种写法**：
  - 可能没有（查不到、没有这样的元素）：返回 `T?`；
  - 环境或输入造成的失败（文件不存在、网络、解析用户输入、文本不合法）：返回 `T!`，调用点用 `!!`、`->` 的传递或 `match` 处理；
  - 调用方违反了前提（越界、区间为空、参数不合法）：故障，Camel 函数体里写 `fail<'Kind'>(message)`，种类具体，不用笼统的 `RuntimeError`。
- **同一种失败只有一种表示**：不要既返回 `T!` 又报故障，也不用 `-1`、空串之类的哨兵值。
- **有合理定义的值就不报故障**：全函数优先。`split<''>` 得到码点，`repeat<n>` 在 `n` 不为正时得空串，`take<n>` 按定义截断；只有没有合理值时才是前提。
- **输入可能来自外部、又只有一部分取值合法时，先验证再使用**：给一个 `validate(x): X!`（成功原样交回，可以串起来：`p->validate -> use`），使用它的函数在 `x` 不合法时是故障。`re::validate` 返回 `string!`，`re::find` 遇到非法模式是故障；常量模式写错是程序错误，动态模式先过 `validate`。
- 构造错误值用 `error('Kind', 'message')`，用 `return` 交出；`return` 也可以写在表达式里。隔离故障用 `attempt(f)`，它是一个函数变换。
- 一个函数不要把调用方传来的错误重新编号：裸类型变量的 `T!` 里错误原样带下去。

### 5.1 错误种类目录

错误值（`T!`）的种类名（`e.kind`）是 PascalCase，一个种类只有一个意思，不同模块的同类失败共用同一个种类；`@fails error` 只能用本表的种类，要新种类先加到这里。故障的种类是 `RuntimeDiag` 的名字（`InvalidArgument`、`IndexOutOfRange`、`DivisionByZero` 等，见 `docs/diagnostics.md`），`fail<'Kind'>` 取这些名字，不在本表。

**通用（环境与输入）**

| 种类 | 含义 | 返回它的函数 |
|---|---|---|
| `NotFound` | 路径指向的东西不存在；或系统里没有所问的对象 | `fs` 的读、`file_size`、`list_dir`、`mkdir`（父目录不存在）、`remove`、`remove_dir`；`env::get_cwd`；`process::user_name` |
| `PermissionDenied` | 系统拒绝了这次访问（含只读文件系统） | `fs` 各函数；`env::get_cwd` |
| `AlreadyExists` | 要创建的东西已经在了，或同路径是另一种东西 | `fs::mkdir`、`fs::mkdirs`（路径上是文件） |
| `IOError` | 其余输入输出失败：路径是目录而要读文件、写失败（含磁盘满）、非空目录、读到一半失败 | `fs` 的读写与目录函数；`io::set_terminal_raw_mode`（终端拒绝） |
| `NotATerminal` | 标准输入不是终端，无法进入 raw 模式 | `io::set_terminal_raw_mode` |
| `EncodingError` | 字节不是合法的 UTF-8，或文本超过字符串的上限；数不是码点 | `bytes::decode`、`str::from_codepoints`、`fs::read_text`、`fs::read_lines`（元素） |
| `ParseError` | 外部文本不符合所期望的写法 | `parse_int`、`parse_float`、`time::strptime` |
| `RegexInvalid` | 模式不是正则表达式 | `re::validate` |

**json**

| 种类 | 含义 | 函数 |
|---|---|---|
| `JsonParseError` | 文本不是 JSON（消息给出位置） | `json::decode`、`json::decode_as` |
| `JsonTooDeep` | 文档嵌套超过深度限额（默认 10000） | 同上 |
| `JsonTypeError` | 文本是 JSON，但与要解码成的类型不符（消息给出路径） | `json::decode_as` |

**训练、互操作、设备**

| 种类 | 含义 | 函数 |
|---|---|---|
| `PythonError` | Python 一侧失败：异常、缺失的模块或属性；消息含异常类型、文本与回溯 | `python::exec`、`eval`、`run_file`、`import_module`、`attr`、`call`、`to_string`、`to_py` |
| `PythonTypeError` | Python 对象不是所要的 Camel 类型（消息给出路径与所见） | `python::from_py` |
| `ExportUnsupported` | 图不能翻译成目标格式，或目标运行时拒绝某节点 | `onnx::export_model` |
| `DeviceUnavailable` | 本构建或本机没有所要的设备 | `tiles::device` |
| `GpuScopeUnsupported` | `#gpu` 范围里有设备不能运行的算子 | `tiles::compile` |
| `CheckpointError` | 目录不是所要类型的检查点，或文件缺失、校验和不符 | `ckpt::load_tree` 及其读写 |

**图、改写、自动微分**

| 种类 | 含义 | 函数 |
|---|---|---|
| `IndexError` | 索引越界而调用声明为可失败 | `rewrite`、`graph` |
| `RuleInvalid`、`RuleTwoWay` | 规则不能编译；规则被按它不允许的方向使用 | `rewrite`（`seq`、`first`、`repeat`、`until`、`alternate` 等） |
| `GraphInvalid`、`GraphConflict`、`TxnUsed`、`NoChain`、`SignatureMismatch`、`ClosureNotEmpty` | 图事务与图改写的各类失败 | `graph`、`rewrite` |
| `NoAttribution`、`NoCostModel` | 自动微分的归因/代价模型缺失 | `autodiff` |

- `IOError` 是兜底；能归入 `NotFound`、`PermissionDenied`、`AlreadyExists` 的不要用 `IOError`。
- `JsonParseError` 与 `ParseError`：前者是 JSON 文本，后者是其他按固定格式读的文本。

## 6. 模块放在哪里

| 写法 | 位置 |
|---|---|
| 用 Camel 写的模块 | `stdlib/<模块>.cml` |
| C++ 实现的模块 | `modules/<模块>/`（执行器、算子表、资源、语义），签名在 `stdlib/<模块>.cml` |
| 尚未声明化的 C++ 模块（过渡） | `stdlib/signatures/<模块>.cml`，只作参考、不参与编译；每搬一个模块删一个文件 |
| 已声明化模块里算不出来的签名 | 只在 `stdlib/signatures/<模块>.cml`，由模块条目的 `computed` 字段指名；只作参考、不参与编译，其余声明在 `stdlib/<模块>.cml`，两处由 `--verify-ops` 一起对账。声明文件里不写 `// dynamic func`，一个名字不能两处都有（`math`、`graph`、`json`、`random` 都这样） |
| 测试用例 | `test/cases/`，计划在 `test/plans/` |

模块名就是导入名，也是算子 URI 的前缀。新模块按分层放：前奏（内置，只放几乎每个程序都要的：转换、输出、序列与映射的基本操作、错误与故障；`input` 在 `io`，`sleep`、`exit` 在 `process`）／基础（`str`、`bytes`、`array`、`math`、`random`、`stats`、`time`、`re`、`json`、`hash`）／系统（`fs`、`io`、`env`、`process`）／数值（`tensor` 栈）／工具（`test_support`、`bench`、`profiler`）。

## 6.5 写法：标准库是 Camel 的范本

标准库是用户最常读、最常照抄的 Camel 代码，它的写法就是推荐写法。每个用 Camel 写的函数都按下面几条写；做不到的地方是语言的缺口，提出来补进语言，不在库里绕路。

- **一个定义覆盖所有类型。** 不为 `int`、`float`、`i32`、`f32` 各写一份：类型参数在函数头声明（设计原则 5.5），条件用 `when`（`when is_float::<T>()`），字面量在实例里按实例的类型取型；结果类型随输入变化的，用固定签名加泛型签名，或类型函数。
- **作用于数组时，用标量的定义。** 数学函数（`exp(xs)`）和转换（`f32(xs)`）对数组逐元素作用，定义就是标量那一条映射到元素上（`xs->map<exp>`），不为数组另写一套语义；快的实现是挂在这个定义上的内核，不是第二个定义。
- **归约在 f64 中累加、舍入一次，空归约是故障**（设计原则 7.7），`sum` 也是；`min`/`max` 取 IEEE 的 `minimum`/`maximum`，NaN 传播，-0 小于 +0。超越函数在 f64 里算、舍入一次，目标是 `exp`、`log` 不超过 2 ulp，特殊值精确。
- **用语言自己的构造，不拼装。** 提前结束用 `loop … for … while`，不先 `map` 出一个中间数组再 `reduce`；取一组位置用 `xs[idx]`，切片用 `xs[a:b]`；列表推导 `[e for x in xs if c]`；字符串用插值；按类型选择用 `match type`；按字段展开用 `static for`；类型的性质用 `is_x::<T>()` 一族谓词，不在运行时检查类型。
- **不写推得出的东西。** lambda 的参数类型、和默认 URI 相同的 `#op` 都不写。
- **同名同义，一个意思一种写法**（第 3 节）。
- **不存在用 `T?`，会失败用 `T!`**，不用 `-1`、空串之类的哨兵值；示例里也不用（`'hello'->index_of<'l'>`，不写 `?? -1`）。
- **配置放尖括号，数据放圆括号**（第 4 节、设计原则 4.4）：`xs->take<3>`、`x->linear<w, b>`。
- **Camel 函数体先行，原子只在三种情形**（设计原则 10.4）：与外部世界打交道、需要内存与布局控制的内核、标量数学。语义在函数体，速度在 `#impl`：内核是本模块的私有无体声明，**签名与函数完全一致**（`with` 与普通参数的个数、类型，返回类型）；常用而热的才配，测量表明编译器追上后删除。
- **资源与纯度**（设计原则 3.3）：无函数体的原生声明在签名里写 `pure` 或 `uses 资源`；Camel 函数体什么都不写，资源由编译器推出。只有一种情形在 Camel 函数上写 `pure`：它是刻意对外的承诺，或这是对回调参数的要求（`f: (x: T) => U pure`）。文档里不写 `@pure`、`@effect` 标签。
- **前奏只放几乎每个程序都要的东西**；其余在自己的模块里。
- **短**：一个函数体通常一两行；超过五行的先想是不是缺了一个更基础的函数或语言构造。

## 7. 文档

参考页 `docs/stdlib-reference.md` 的每一条都要是读者真能照着做的，`///` 注释里的东西就是参考页的全部内容。规则如下，由 [`scripts/stdlib-reference.mjs`](../scripts/stdlib-reference.mjs) 的 `--verify-docs` 检查，跑它的是 [`stdlib_registry.plan.toml`](../test/plans/feat/modules/stdlib_registry.plan.toml) 里的 `registry.docs_complete`（`feat/modules`）：

- **每个打印出来的名字都要有描述**：`///` 里至少一行正文，说明它做什么、什么时候用。只有标签（`@fails` 一行）不算描述。
- **每个名字都要有 `@example`，并且它要能编译。** 检查器把例子当成一次真实调用：按位置把实参绑到声明上的参数类型（`->` 的接收者是第一个参数），按探测到的模块补上例子里用到的类型的导入，再放进一个函数里编译。所以例子不能出现没有定义的 `x`、`w`、`rules`：它要么只用声明里的名字，要么自己构造值（`[1, 2, 3]->foreach<...>`、`([('a', 1)] as Map<string, int>)->get<'z'>`）。参数名与声明不同的例子按声明改正——`nn::conv2d` 的参数是 `input`、`kernel`、`bias`，例子就写这三个名字，`x`、`w`、`b` 会被编译器拒绝。
- **写不成独立程序的例子**用 `@example-fragment <原因>` 标出，原因必填。这样的例子不编译，参考页把“未编译”和原因一起打印。没有标记又不编译的例子是错误，不是“以后再说”；标记是决定，不是省略。
- **返回 `T!` 的名字要有 `@fails`，会报故障的名字（`fail`、越界）也写**，写明种类：`error value \`Kind\`` 或 `fault \`Kind\``，种类取自 5.1 的目录。返回的只是裸类型变量（`T!`、`F!`）时，错误是调用方传进来的、这个函数没有定义它，描述里说明来源即可，不编一个种类。
- 值（`// value 名字: 类型`）、普通函数与计算签名同样适用上面几条：描述、`@example`、能编译。

`--audit --compile` 从同一份解析给出每个模块的表（名字、条目、缺描述、缺例子、片段、编译失败），查缺口用它；`--audit` 加 `--compile` 需要已构建的编译器。

## 8. 声明写不出的签名

一条声明写不出的判据有四条：结果类型不取自任何参数或类型实参；每个参数都能写成一个类型；一个名字的所有 URI 能写进同一组声明；写出来的声明接受的调用集合与 C++ 相同。近似声明会让本该拒绝的调用通过，少写一条规则会让本该通过的调用被拒绝，两者都不行。按这四条给不出的签名留在 C++ 的 `DynamicFuncTypeResolver` / `OpDef`，参考条目记在 `stdlib/signatures/<模块>.cml`（第 1 节）。

**已有定论，不加语法。** 可选参数写默认值（`with` 参数同理），不按元数写重载；各种宽度写一个泛型（`::<T>` 加 `when`），只有执行器按宽度分名时才分臂（`int` 与 `i32` 之间没有隐式转换）；伞形参数写 `T[..]`、`A[]`（函数头声明 `T`、`A`）或逐类型重载（`tensor` 的 `show` 就是这样写的）。声明替换整个算子组而不是追加，所以一个名字不能一半写声明、一半留 C++：一半写不出，整个名字留 C++（`range`、`sum`、`mean` 即此）。

**只有语言作者能答的缺口。** 任意参数表的函数类型（`rewrite` 的 `__rule`、`onnx::export_model`、`tiles` 的 `source`/`inspect`/`benchmark` 等；今天只能留 C++）；声明看不见调用点的类型实参（`from_bytes::<T>`、`load_tree`、`decode_as`）；类型变量谓词（`save_tree` 等）；模块类型的类型变量实参（`Device<T>` 接受、`PyObject<T>` 不接受，是 `typeArguments()` 覆写与否的问题）；变参（`..ts` 已落地第一阶段，剩余形参带类型与展开调用仍未做）。这些不是新语法提案，就是待裁定的问题。

**不是签名缺口，不要动。** 类型限定别名（模块类型自己名字下的别名）已有声明替换机制；`range`/`sum`/`mean` 受"一个名字一个来源"约束；结果取自实参的算子按设计就是计算签名；逐叶结构映射（`tiles:upload`/`download`）等泛型按结构展开；`ckpt` 的 `tree_*` 缺的是执行器而不是语法。
