# 48 声明迁移：结果、边界与下一步

本页是 H19「标准库重新规划」第 1 步（原生声明机制）和第 10 步（逐模块声明化）的结账页：迁移
做成了什么、还剩什么、每一处归谁。逐名字的缺口分类与论证在
[47 声明写不出的签名](47_declaration_syntax.md)；本页只给每模块的映射，不重复那份目录。步骤
清单见 handover note ⟨b5ede1f⟩ (H19-plan)，旧的逐模块账本是
[handover/README.cn.md](../handover/README.cn.md) 的 H19 一节（H153 的快照，数字以本页为准）。

下面所有数字都在 Linux 主机上用 H200 的树重跑过：`scripts/stdlib-declarations.mjs --check`、
`scripts/stdlib-reference.mjs --verify-ops`、`camel inspect --ops
test/cases/modules/stdlib/registry_all.cml`。本页不再靠人复述数字：`node
scripts/stdlib-declarations.mjs --check-ledger` 从本页读出每个数字与树（或 `inspect --ops`）比较，
不一致就报 `page says X, tree has Y`，跑它的是 `feat/modules` 的
`registry.ledger_matches_tree`。所以改一个数、漏一行、或让标题与合计对不上，计划会先红。

## 1. 为什么做

声明化不是一次改名，而是把「一个函数的签名」从三处收成一处。设计原则 6.4 给的理由是：

> 没有 `inner`、`outer`、`atomic`、`shared`：原生实现由模块中无函数体的声明表达，外部实现由
> 模块系统承担，"作为整体、不展开"由 `#kernel` 一类标签表达，数据共享由不可变数据取代。

[标准库约定](../stdlib-conventions.cn.md) §1「一个事实只写一处」把它落成一张表：固定签名（参数、
`var`、返回类型、`with` 参数）写在模块 `.cml` 里**没有函数体的声明**；计算出来的签名留在 C++ 的
`DynamicFuncTypeResolver` / `OpDef`；纯度、资源、数值语义、原地形式由 `OperatorTable::declare`、
`declareResources`、`declareSemantics` 回答。声明是签名的唯一来源，C++ 只回答声明表达不了的
那一半。

迁移之前，同一个签名可以有三份：C++ 的 `OperatorGroup` 与 `StaticFuncTypeResolver`、只读的
`stdlib/signatures/<模块>.cml`、以及参考页。三份不一致时不会崩，只会悄悄选错重载，或者让一份
没人读的副本冒充事实。迁移之后，加一个重载、改一个返回类型、补一条文档，都是改 `.cml` 的一行；
C++ 的 `exportEntity` 不再写签名，`inspect --ops` 的注册行不变。`kDeclaredOperators` 这类跳过
表是这次收拢的代价：C++ 仍持有内核与算子表，必须显式知道哪些名字已经交给声明。

## 2. 状态

### 2.1 一句话的账

| 项 | 数字 | 来源 |
|---|---:|---|
| 声明模块（`stdlib/<m>.cml` + C++ `BuiltinModule`） | **26** | `--check`（`declarationModules()`） |
| 声明名 / 声明条数（`export func`） | **185 / 301** | 读 26 个声明文件 |
| 计算名（C++ 算签名） | **112** | 12 个 `computed:` 签名文件，0 条内联 |
| 其中因语言机制写不出（阻塞） | **14** | 本页 §2.4 的口径，逐条见表 |
| 公开名（26 个声明模块） | **297** | `inspect --ops`：185 个声明名 + 112 个计算名 |
| 注册重载（26 个声明模块） | **421** | `inspect --ops`，301 条由声明回答、120 条由 C++ 计算 |
| 注册重载（参考页全部模块，含 `<builtin>`） | **524** | `--verify-ops`；`<builtin>` 占 96 条 |
| `stdlib/signatures/` 的文件 | **14** | 12 个是已迁移模块的计算签名，2 个属于尚未迁移的模块 |

这 26 个模块的 297 个公开名 = 185 个声明名 + 112 个计算名。12 个模块有计算签名、14 个没有；
5 个模块有阻塞名（`json`、`onnx`、`python`、`tensor`、`tiles`）。

### 2.2 逐模块

「名字」是模块今天注册的公开名（`inspect --ops`，去掉 `::` 别名与 `__` 组）；「声明」是其中
写在 `stdlib/<m>.cml` 的名字数 / 声明条数；「计算」是留在 C++ 的名字数；「阻塞」是计算名里
因为语言机制写不出的那部分（§2.4），它按 [47](47_declaration_syntax.md) 的缺口分类记而不是从树
读出来，检查器只校验这一列的行合计与标题数字一致；「重载」是注册的公开重载数。

| 模块 | 名字 | 声明（名/条） | 计算 | 阻塞（缺口） | 重载 | 依据 |
|---|---:|---:|---:|---|---:|---|
| `algo` | 8 | 1 / 6 | 7 | — | 13 | handover note ⟨8db99af⟩ (H135) |
| `autodiff` | 3 | 1 / 1 | 2 | — | 3 | handover note ⟨65416a0⟩ (H165) |
| `bytes` | 6 | 6 / 6 | 0 | — | 6 | handover note ⟨05673a8⟩ (H76) |
| `env` | 2 | 2 / 2 | 0 | — | 2 | `stdlib/env.cml` 的声明 |
| `fs` | 15 | 15 / 15 | 0 | — | 15 | handover note ⟨f7dfc75⟩ (H131) |
| `graph` | 41 | 35 / 37 | 6 | — | 43 | handover note ⟨9a5ec0a⟩ (H141) |
| `hash` | 2 | 2 / 3 | 0 | — | 3 | `stdlib/hash.cml` 的声明 |
| `io` | 5 | 5 / 5 | 0 | — | 5 | handover note ⟨e6a16f6⟩ (H116) |
| `json` | 12 | 10 / 35 | 2 | 2：`encode`（参数类型谓词）、`decode_as`（Q1） | 38 | handover note ⟨26cee7d⟩ (H115) §3；[47](47_declaration_syntax.md) §3.4 |
| `math` | 44 | 41 / 96 | 3 | — | 99 | handover note ⟨fa3cccc⟩ (H113) |
| `nn` | 19 | 1 / 1 | 18 | — | 21 | handover note ⟨65416a0⟩ (H165) |
| `onnx` | 2 | 1 / 1 | 1 | 1：`export_model`（Q-F + Q3） | 2 | handover note ⟨a05a542⟩ (H179) §2.2；[47](47_declaration_syntax.md) §3.1、§3.5 |
| `para` | 3 | 1 / 1 | 2 | — | 3 | handover note ⟨b083ff2⟩ (H146) §2.2 |
| `process` | 3 | 3 / 3 | 0 | — | 3 | `stdlib/process.cml` 的声明 |
| `pyplot` | 1 | 1 / 8 | 0 | — | 8 | handover note ⟨26e8cc0⟩ (H166) |
| `python` | 11 | 6 / 6 | 5 | 5：`py_call`、`py_print`、`py_println`（Q-V）；`py_wrap`、`py_unwrap`（Q-M） | 12 | handover note ⟨cdf4d60⟩ (H157) §6.2；[47](47_declaration_syntax.md) §3.2、§3.3 |
| `random` | 10 | 7 / 7 | 3 | — | 10 | handover note ⟨b3452d6⟩ (H128) |
| `re` | 1 | 1 / 1 | 0 | — | 1 | handover note ⟨bd54bb4⟩ (H126) |
| `rewrite` | 8 | 8 / 10 | 0 | — | 10 | handover/README.cn.md 的 H183 行（报告在最终消息）；`stdlib/rewrite.cml` 头注释 |
| `sim` | 2 | 2 / 2 | 0 | — | 2 | handover note ⟨a05a542⟩ (H179) §3 |
| `statistics` | 5 | 5 / 20 | 0 | — | 20 | handover note ⟨8db99af⟩ (H135) |
| `str` | 14 | 14 / 14 | 0 | — | 14 | handover note ⟨73ce85e⟩ (H104) |
| `tensor` | 64 | 11 / 15 | 53 | 1：`from_bytes`（Q1） | 72 | handover note ⟨8dd93b7⟩ (H151) §3.2；[47](47_declaration_syntax.md) §3.4 |
| `this` | 1 | 1 / 1 | 0 | — | 1 | handover note ⟨b083ff2⟩ (H146) |
| `tiles` | 12 | 2 / 2 | 10 | 5：`gpu`、`source`、`inspect`、`benchmark`、`regions`（Q-F；`gpu` 另有 Q3） | 12 | handover note ⟨0ebbbbd⟩ (H156)；handover note ⟨cdf4d60⟩ (H157) §7；[47](47_declaration_syntax.md) §3.1、§3.5 |
| `time` | 3 | 3 / 3 | 0 | — | 3 | handover note ⟨55e0bf3⟩ (H133) |
| **合计** | **297** | **185 / 301** | **112** | **14** | **421** | |

计算名里没有阻塞的 98 个，按约定是「结果类型或参数类型取自实参」的计算签名：`algo` 的 `sort`
族、`autodiff` 的 `grad`/`with_grad`、`graph` 的 6 个、`math` 的 `sqrt`/`shl`/`shr`、`nn` 的
18 个张量算子、`para` 的 2 个、`random` 的 3 个、`tensor` 的其余 52 个、`tiles` 的
`upload`/`download`/`run`/`run_device`/`run_preferred_resident`。它们不是「没做完」，是 C++
必须保留的那一半。

### 2.3 旧报告与树不一致时

- handover note ⟨a05a542⟩ (H179) 记的是 **22 / 113 / 513**（声明模块 / 计算名 / 注册重载），
  那是它自己基点上的数。今天的树是 **23 / 112 / 524**：`show` 已由
  handover note ⟨ce4a81c⟩ (H175) 从计算签名改成两条声明，所以计算名少 1；重载数以今天
  `--verify-ops` 打印的 524 为准，旧报告不能相加。
- handover note ⟨9897519⟩ (H170) 的「16 个模块 / 87 条计算签名」、
  handover note ⟨19465cf⟩ (H173) 的「18 / 92」、H165 的「18 / 107」、H175 的「20 / 111」
  都是各自基点的快照。它们不能相加，也不能拿来当今天的总数。
- handover note ⟨0ebbbbd⟩ (H156) 与 H19 账本把 `tiles` 的 10 个计算名全记作「计算、0 阻塞」；
  handover note ⟨cdf4d60⟩ (H157) §7 按机制把它们中的 5 个记作阻塞，并写明这是分桶差异。
  本页按 §2.4 的口径记阻塞，`tiles` 的「计算」仍是 10。
- H180 的「112 个计算名 / 124 条条目」是它 20 个模块基点上的数（`tensor` 54 名
  56 条，含 `show`）。今天同样是 112 / 124，组成不同：`show` 已由
  handover note ⟨ce4a81c⟩ (H175) 声明，`tensor` 是 53 名 55 条，加上 `onnx` 的 1 名 1 条，
  这两个合计仍是 54 / 56；其余 10 个模块 58 名 68 条。
- 本页 H186 的版本记的是 **22 / 170 / 283 / 403 / 522**（声明模块 / 声明名 / 声明条数 /
  22 个模块的重载 / 总重载；`rewrite` 当时还没迁）。H183 把 `rewrite` 的 8 个名字写成
  `stdlib/rewrite.cml` 的 10 条声明后，同一组命令给出 **23 / 178 / 293 / 413 / 524**。旧数不能
  相加，也不能再引用；本页以上面的表为准。
- [47](47_declaration_syntax.md) 的「130 个名字 / 17 个阻塞」是它 9 个模块的基点，不是 23 个
  声明模块的合计。

### 2.4 口径

- **名字**按唯一名计；一个名字可以有多条重载（`math` 41 个名字 96 条）。
- **声明条数**只数无函数体的 `export func`；`operator ==` 这类运算符声明不计入（`graph` 另有
  8 条，所以它的声明文件是 37 + 8 = 45 条）。
- **声明** = `stdlib/<m>.cml` 里无函数体的 `export func`；**计算** = 留在 C++ 的
  `DynamicFuncTypeResolver` / `OpDef`，参考条目只在模块条目的 `computed:` 指名的
  `stdlib/signatures/<m>.cml` 里（§3.4）。
- **阻塞** = 计算名里「只差一条语言机制」的那些：47 的 Q-F（任意函数类型）、Q-V（变参）、
  Q-M（模块类型不接受类型变量）、Q1（声明看不见类型实参）、Q3（类型变量谓词），加上 H140 的
  「没有执行器」。由实参算结果、但写法本身合法的名字算**计算**，不算阻塞；这是 H157 §0 与
  47 §4.3 的分法。
- **重载** = `inspect --ops` 的 `OPERATOR` 行去掉 `::` 别名与 `__` 组后的条数。
  524 = 421（26 个声明模块）+ 7（`ckpt`）+ 96（`<builtin>`）。
- **签名文件** = `stdlib/signatures/*.cml`。其中 12 个是已迁移模块的 `computed:` 文件，2 个属于
  尚未迁移的模块（`ckpt`、`globals`）。

## 3. 机制

### 3.1 声明是唯一来源

同一目录下的 `.cml` 与 `.cmo` 是**同一个模块**：`.cml` 是声明模块，`.cmo` 是实现。声明按
`<模块>:<名字>` 绑定 C++ 算子；名字与 URI 不同、或模块用别的前缀时写 `#op<'...'>`；`with` 形参
写在声明上方。声明**替换**整个算子组，不是追加（[约定](../stdlib-conventions.cn.md) §2；H94 第
1 条）。所以一个名字不能「固定的一半写声明、计算的一半留 C++」：要么整组交给声明，要么整组留在
C++。

### 3.2 C++ 留什么

C++ 保留声明表达不了的事实：内核（`checkpointKernels()` 一类）、执行器
（`ExecutorManager` / `registerExecutorFactory`）、纯度（`OperatorTable::declare`）、资源与效果
（`declareResources`）、数值语义与原地形式（`declareSemantics` / `setInPlaceForm`）、类型导出
（`exportType`），以及计算签名本身。迁移删掉的是 `StaticFuncTypeResolver` 和声明名的
`exportEntity`，不是这些。

### 3.3 `kDeclaredOperators` 止住第二次导出

`tensor` 与 `nn` 的算子来自算子表循环：`for (group : OpRegistry::operatorGroups("tensor"))
exportEntity(group->name(), group)`。声明名也在这张表里（C++ 仍要它们的 `OpDef` 与内核），
循环若不跳过就会把同一个名字再导出一次。`kDeclaredOperators` 是那张跳过表：`tensor` 11 个名字、
`nn` 的 `save_params`。循环对声明的名字只导出类型限定别名 `Tensor::<name>`，再由
`Module::replaceTypeQualifiedAliases` 把别名指回声明。`--check` 第 4 条同时检查两个方向：
C++ 不能导出声明名，计算条目也必须有导出组。

### 3.4 计算签名只有一处

记法不变量由 H180 定下并已合入：一个计算签名只记录在 `stdlib/signatures/<模块>.cml`，由
`scripts/stdlib-reference.mjs` 的 `MODULES` 条目用 `computed:` 指名；声明文件里不写
`// dynamic func`，同一个名字不能两处都有，也不能两处都没有。今天 112 个计算名全部在 12 个
`computed:` 文件里（124 条条目），树上没有一处内联；`--check` 第 6 条会拒绝内联写法、两处都有、
以及 `computed:` 指名却一个名字也不记录。迁移新模块时按文件记法写，不要新增内联。

### 3.5 检查器

`scripts/stdlib-declarations.mjs --check` 有六条规则，第 3 条复用 `--verify-ops`：

| # | 拦什么 |
|---|---|
| 1 | 声明名在 C++ 里又有 `StaticFuncTypeResolver` |
| 2 | `stdlib/signatures/<m>.cml` 存在但 `MODULES` 的 `computed:` 没指名它（或指了不存在的文件） |
| 3 | 声明背后没有已注册的算子（复用 `--verify-ops`：逐条比名字、URI、`with`/普通参数个数、纯度） |
| 4 | C++ 的导出组与声明不一致（导出声明名，或计算条目没有导出组） |
| 5 | 注册算子的 C++ 模块既不在 `registry_all.cml` 探针、也不在 `MODULES`、也不在排除表 |
| 6 | 计算签名写进声明文件的内联 `// dynamic func`、两处都有，或 `computed:` 指名却一个名字也不记录 |

### 3.6 三种退回写法，各由谁抓

| 退回写法 | 谁抓 | 只跑 `--verify-ops` 为什么不够 |
|---|---|---|
| 给声明名重新加 `StaticFuncTypeResolver` | `--check` 第 1 条 | 第二份签名与声明逐字相同，名字 / URI / 元数 / 纯度都对得上 |
| 把旧 `stdlib/signatures/<m>.cml` 留在原处（第二份清单） | `--check` 第 2 条 | 参考生成器只读声明与 `computed:` 指名的文件，副本没人读，也就没人对账 |
| 从 C++ 再导出声明名的算子组 | `--check` 第 4 条 | 同一个 URI 仍只出现一次，`--verify-ops` 看到一条 |

## 4. 剩下什么，谁负责

### 4.1 还开着的项

| 项 | 缺口 | owner | 证据 |
|---|---|---|---|
| Q1：声明看不见类型实参 | `tensor:from_bytes`（三个 URI）、`ckpt:load_tree`（`::<T>`）、`json:decode_as`（`shape` 的类型实参） | 作者（H148 Q1） | [47](47_declaration_syntax.md) §3.4；handover note ⟨3597306⟩ (H140) §3.3；handover note ⟨26cee7d⟩ (H115) §3 |
| Q-F：任意函数类型 | `onnx:export_model`、`tiles` 的 `gpu`/`source`/`inspect`/`benchmark`/`regions`，以及 `rewrite` 的三个保留组（见 §4.2） | 作者（Q-F） | [47](47_declaration_syntax.md) §3.1；handover note ⟨a05a542⟩ (H179) §2.2 |
| Q3：类型变量谓词 | `ckpt:save_tree`、`tiles:gpu`、`onnx:export_model`（`example` 必须是参数树） | 作者（H148 Q3） | [47](47_declaration_syntax.md) §3.5；handover note ⟨3597306⟩ (H140) §3.2 |
| Q-V：变参 | `python:py_call`、`py_print`、`py_println`。`..ts` 已设计、未实现；三者的结果类型还各自取自实参，`rewrite:__analysis` 同理 | 作者 / 语言（47 §3.2） | handover note ⟨cdf4d60⟩ (H157) §6.2；[47](47_declaration_syntax.md) §3.2 |
| Q-M：模块类型作为类型实参 | `python:py_wrap`、`py_unwrap`；`PyObject` 没有覆写 `typeArguments()` | 作者（H148 Q-M） | handover note ⟨cdf4d60⟩ (H157) §6.2；[47](47_declaration_syntax.md) §3.3 |
| `ckpt` 的 5 个 intrinsic：`tree_map`、`tree_reduce`、`tree_unzip`、`tree_leaves`、`tree_paths` | 没有执行器：建图时按树形状展开，声明在加载时被 `NativeDeclarationNotLoaded` 拒绝；4 个的结果类型也写不出 | H51 第 2 项（用 Camel 重写 5 个 `tree_*` 并删掉核心里的硬编码展开）；不是声明语法 | handover note ⟨3597306⟩ (H140) §3.1；handover note ⟨2f3c6a8⟩ (H174) §2.1 |
| `globals`：50 个名字、93 条声明 | 语言全局，`BuiltinModule("")`、不可导入；不是模块，没有声明宿主。93 条声明在 `stdlib/signatures/globals.cml`，96 条注册重载在 `inspect --ops` 的 `<builtin>` 行：两个数计的不是同一件事，前者读文件、后者读注册表 | 作者（H141 的调查，未裁定） | `stdlib/signatures/globals.cml`；`inspect --ops`；handover note ⟨9a5ec0a⟩ (H141) |
| `env`（2 名 2 条）、`process`（3 名 3 条）、`hash`（2 名 3 条） | 固定签名，参考文件仍在 `stdlib/signatures/`；没有 `stdlib/<m>.cml`。没有机制缺口 | H19 第 10 步的剩余批次 | handover note ⟨b5ede1f⟩ (H19-plan) §0.1；handover note ⟨26cee7d⟩ (H115) §10 |

### 4.2 已定，不再算迁移工作

这些不是「还没做」，而是有决定、无动作；记在这里是因为它们解释了树为什么是这个样子。

| 项 | 决定 | 证据 |
|---|---|---|
| `graph` 的 `__neq__` | `!=` 由 `==` 的重载给出，编译器只否定 Camel 的 `==`，不否定原生组；检查器的例外 | 设计原则 4.3；handover note ⟨9897519⟩ (H170) §3.4；`modules/graph/module.cpp` |
| `rewrite` 的保留组 `__rule`、`__rule_when`、`__analysis` | H183 把三个名字加进检查器的例外（与 `__neq__` 同类）：它们没有可调用的 Camel 名，留在 C++；8 个可调用名字已全部声明，`stdlib/signatures/rewrite.cml` 已删除 | `stdlib/rewrite.cml` 头注释；`scripts/stdlib-declarations.mjs` 的 `UNDERIVABLE_OPERATORS`；handover/README.cn.md 的 H183 行 |
| `math` / `graph` 的内联计算签名 | H180 把 9 条迁进签名文件；今天 0 内联，`--check` 第 6 条守住 | 本页 §3.4；handover note ⟨f782b3d⟩ (H180) |
| `tensor` 的 `range`、`sum`、`mean` | 轴形式与全量形式共用一个名字，拆成两条声明会违反「一个名字一处」；47 §4.2 把它记作策略，不是缺口 | handover note ⟨8dd93b7⟩ (H151) §3.4；[47](47_declaration_syntax.md) §4.2 |

### 4.3 解锁上限

把 Q1 与 Q3 都答完，声明路线只能解锁 `save_tree` 与 `load_tree`，`ckpt` 仍是 2/7；5 个 intrinsic
缺的不是声明语法，是执行器与展开器（handover note ⟨2f3c6a8⟩ (H174) §0）。所以 `ckpt` 的
「阻塞」状态不会因为声明机制的一次扩展而消失，得先做 H51 第 2 项。

## 5. 迁移下一个模块：七步

1. **枚举与分类。** 写一个只 `import` 该模块的探针（文件名不要与模块同名，也不要放在同名文件
   旁边），跑 `camel inspect --ops <探针>`，按名字记下 URI、`with`/普通参数个数、纯度。用
   [47](47_declaration_syntax.md) 分四类：固定、计算、保留组、阻塞。数字先记下来，迁移后用同一
   命令逐行对照。
2. **写声明。** 在 `stdlib/<m>.cml` 写 `module <m>` 与无函数体的 `export func`；`with` 参数写在
   声明上方；名字与 URI 不同时用 `#op<'...'>`；可选参数按元数拆重载、整数宽度按拼写拆重载
   （47 §2.1、§2.2）；`///` 文档、`@pure`/`@effect`/`@fails`/`@example` 跟着声明走。
3. **删 C++ 的签名。** 删掉声明名的 `StaticFuncTypeResolver` 与 `exportEntity`；若模块用算子表
   循环导出，把名字加进 `kDeclaredOperators`，并保留 `Tensor::<name>` 这类类型限定别名。内核、
   执行器、纯度、资源、语义、类型导出都不动。
4. **记计算签名。** 计算名逐条写进 `stdlib/signatures/<m>.cml`，模块条目加
   `computed: 'signatures/<m>.cml'`；不要写内联 `// dynamic func`（§3.4）。阻塞名也留在这里，
   文件头写清它是阻塞而不是计算。
5. **更新参考表。** `scripts/stdlib-reference.mjs` 的 `MODULES` 条目改成 `declaration: true`、
   `source: '<m>.cml'`、`computed: ...`；跑 `--write` 重生成 `docs/stdlib-reference.md`。
6. **过检查器。** `node scripts/stdlib-declarations.mjs --check` 与
   `node scripts/stdlib-reference.mjs --verify-ops` 都必须 0 问题；`--check` 的六条就是完成的
   定义（§3.5）。遇到没有 Camel 名的保留组，先加一条带理由的例外，不要把它硬写进声明。
7. **补用例与快照。** 在 `test/cases/compile/native/` 加导入、错元数、错类型三条；更新 AST
   快照与 `registry_all.cml` 的注册行；在 Linux 主机上跑 `feat/modules` 与 `smoke`
   （`camel-remote.ps1 -Jobs 8`），不在工作站构建。

## 6. 复核

| 命令 | 今天打印 |
|---|---|
| `node scripts/stdlib-declarations.mjs --check` | `26 declaration module(s), 112 computed signature(s) ... 0 problem(s)`，并确认 `524 registered operator overloads` |
| `node scripts/stdlib-declarations.mjs --check-ledger` | `the ledger agrees with the tree: ...`，本页每个数字都与树一致 |
| `node scripts/stdlib-reference.mjs --verify-ops` | `the declarations agree with the 524 registered operator overloads` |
| `camel inspect --ops test/cases/modules/stdlib/registry_all.cml` | `OPERATOR` 行 794 条；去掉 `::` 别名与 `__` 组后 354 个公开名（其中 `<builtin>` 50 个）/ 524 条公开重载（其中 `<builtin>` 96 条） |
| `node scripts/check-doc-counts.mjs` | `no stale count in ...` |
| `node scripts/check-doc-links.mjs --default-roots` | `no broken links in ...` |
