# 第 7 节第 9 步的续篇：`std::cml` 往返（S3 之后的剩余工作）——实施计划

**这是第二份计划。** 第一份是 [plan.cn.md](plan.cn.md)（决策者 2026-10-22 已批复，答复见 [`reviews/std-cml-plan.md`](../../handover/reviews/std-cml-plan.md)），它已经定下：`std::cml` 是翻译遍、打印的是当前的图而不是原始源码；`canon(P)` 的等价定义（§2.2）；S0–S5 的划分；§7 七问的答案（`let` 名字用源码名、声明标签用程序级事实表、`#tag { }` 建 `Block` 区域、泛型实例打印成具体函数、v1 一个文件摊平、大常量超限拒绝、打印抽取后的可执行图）。本文件不重复那些结论，只写**剩余工作**、**随仓库变化的修订**与**实测证据**；与第一份冲突的地方以本文件为准并写明原因。

实测基线：`develop 102dbc5d1`（本计划的提交基底），在 88zjwei 上构建后的 `out/latest/bin/camel`。下面每个数字、每条诊断都是这一版跑出来的；测量脚本是本次临时加入、**不提交**的 `test/cases/trans/cml_probe.py`（11 个探针），命令形式是 `camel <file> std::cml [passes]`。反例程序都写在下文里，可逐条重跑。

**基线与移动的目标**：写这份文件时 `origin/develop` 已经前进到 `38fbbb7b1`（H242 的数据边与顺序边语义、两份裁定——《一元算子比 `->` 结合得松》与《改写保持源码程序运行的每个效应》、数组归约内核，以及 `h242a-*` 两份问题文件）。本计划依据的文件在这段区间里**只有一处变化**：`src/compile/gir/builder_expr.cpp#Builder::orderAfterLastWriter` 的注释（把待办指向裁定后的说明），行为没有变；`src/passes/trans/json/**`、`src/passes/trans/cml/**`、`test/tools/cml_roundtrip.py`、`include/camel/runtime/graph.h` 都没有改动。所以下面的数字与缺口在新基线上同样成立；实施 S3a 时以当时的 `develop` 为准，并注意 §8 第 5 问已经由那份裁定回答了一半。

**第一份计划里已经过期的一条事实**：`docs/handover/README.cn.md:218` 的表格仍写「S0（规范转储与等价匹配器，不改语言与流水线）在跑」。S0 已经合入 develop：`77bb8b124`（canonical GIR 与独立等价检查器）、`5815e6802` + `684ecc6ca`（S1 打印机）、`14b43005b`（分支、循环、闭包、`with`/`var` 参数），测试在 `test/plans/feat/trans/cml.plan.toml`。那行是作者的表，本计划不改。

## 0. 结论摘要

1. **今天能打印的只是少数**：`test/cases/**/*.cml` 共 **2387** 个文件，`std::cml` 打印 **348** 个，以 `CmlUnsupportedConstruct`/`CmlBigConstant` 拒绝 **1338** 个，其余 701 个本来就编译不过（诊断用例）。能打印的集中在 `compile` 122、`consistency` 50、`opt` 39、`modules` 38、`linear` 23、`trans` 21。
2. **往返性质今天就有反例**：打印成功且能重新编译的 348 个里，**309 个等价、35 个不等价、4 个的打印结果连 `std::jgir canonical=true` 都跑不出来**。这 35 个不是「看起来不整齐」，是打印器**丢节点**或**改接线**，见 §3.9 与 §7。
3. **最大的两类缺口是声明与类型**：拒绝理由归并后 131 类，最多的是「算子没有源码形式」463 个、「参数的类型打不出来」273 个、「`with` 参数是命名类型」66 个、「图名不是源码标识符」58 个、「宏图」38 个、「参数是命名类型」35 个、「返回类型是带标签/可空/单元组」31 个、「间接调用少了函数值」26 个。
4. **优化过的图打不出来**：`camel x.cml std::opt std::cml` 与 `std::opt::inline std::cml` 今天都拒绝，理由是 `Inline` 区域（`cml.cpp#validateGraph`）。第一份计划把这当成给人用的附带功能；按裁决 9.7（改写必须保持源码程序运行的每个效应）它其实是**必须**能打的一条路。
5. **等价判定器看不见第三种边**：canonical v2 的节点输入只有 `normal`/`context`/`control` 三种（`gir_canonical.cpp#CanonicalDocument::inputsJson`），**没有顺序边**（实测：一个带 `var` 写与同资源效应的程序，整个 canonical 文档里 `order` 出现 0 次；节点字段只有 `callee/id/inputs/kind/payload/type/value`）。H242 落地后每个同资源效应对与每个 `var` 写写对都有顺序边，判定器却看不见——**这是假绿的来源**。
6. **判定器也看不见 `Block` 区域**：canonical 主动**抹掉** `Inline` 与 `Block`（`gir_canonical.cpp#CanonicalDocument::canonicalRegionOf`），而标签是程序的一部分（设计原则 2.5 明说「含区域、写法、标签」）。`Inline` 抹掉是对的（内联体不是另一个程序），`Block` 必须留下。
7. **与 H242 的接口是 8 处读今天表示的地方**：`cml.cpp#findEntry`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#bindLoopCond`、`cml.cpp#expression`（`Gate`/`Sync` 节点）与 `cml.cpp#buildTopo`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#regionNeedsSync`（`ctrlInputsOf`）。H242 删掉 `Ctrl` 这一种类与这两种节点（`edges.cn.md:15, 25, 48`；`README.cn.md:402, 409`）。**打印机今天就是按这些写的**，本计划的第一步就是让它不按这些写（§5）。
8. **顺序**：先做一个**一个提交就能看证据的竖切片**（三种边 + 端口角色进判定器与打印器视图，§6 的 S3a），再做优化过的图（S3b）、声明（S4）、其余算子与整程序（S5）、与 H242 合流（S6）。每一步的验收都是「跑一条命令、看三份输出」，不是「看起来更好了」。

## 1. 今天已经有什么（事实，带提交与位置）

| 有什么 | 在哪 |
|---|---|
| 打印机 | `src/passes/trans/cml/cml.cpp`（2433 行）、`include/camel/passes/trans/cml/cml.h`；`CmlDumpPass` 是 `RuntimeGraphTranslatePass`，`acceptsWritings()` 为假（`cml.h#CmlDumpPass`） |
| 注册与声明 | 内核注册 `pass.cpp#registerCoreKernels`；遍的声明（名字、说明、参数）在 `stdlib/std.cml#cml` |
| 诊断码 | `CmlUnsupportedConstruct = 0x1A5`、`CmlBigConstant = 0x1A6`（`semantic.h#CmlUnsupportedConstruct`） |
| 参数 | `maxLiteralBytes` 默认 64 KiB（`cml.h#CmlOptions`），写出为 `std::cml max_literal_bytes=...` |
| 抽取写法 | 有写法状态时，驱动器挑一个写法再交给消费者（`pass.cpp#applyPassesDetailed`）；不知道状态的遍拒绝（同一函数里的 `RuntimeDiag::PassNeedsExtraction`） |
| 规范转储（normalizer） | `std::jgir canonical=true`，版本 2，实现在 `src/passes/trans/json/gir_canonical.cpp`，规格在 `docs/technical/gir-interchange.md`「Canonical documents」 |
| 等价检查器 | `test/tools/cml_roundtrip.py`（互异个体化细化，预算 20000 节点 / 200000 步；预算用尽返回 2，**绝不报等价**；`cml_roundtrip.py#equivalent`） |
| 共享比较工具 | `test/tools/gir-dot.mjs#compareGirDotIsomorphic` + `test/tools/normalizers.mjs#applyNormalizers`，用于 `graph_equal.mjs#GOLDENS` 记的那份语料 |
| 已钉住的合同 | `test/plans/feat/trans/cml.plan.toml`、`test/cases/trans/cml_contract.py`（语料 8 个程序、执行对照、确定性、名字保留、5 类拒绝、大常量拒绝） |
| S1/S2 覆盖的语法 | 模块值、函数、字面量、访问、CAST/COPY/ACCS、算子语法（`:io/*`、`:op/*`）、直接调用、`let`/`var`、赋值、`return`、直行 `sync`、`if`/`match`、循环（`for`/`while`/`for ... while`）、闭包、`with`/`var` 参数（`cml.cpp#CmlPrinter` 的自述） |

## 2. 「任何图」的范围：逐项实测

设计原则 2.5 要求「任何合法的图都能写成源码，写回再编译得到同一张图（名字、源码位置不计）」。下表把「任何图」拆成构造，逐项给出**模型里有没有**、**源码能不能写**、**今天打印器怎么做**、**实测到的证据**。

| 构造 | 模型里的位置 | 源码形式 | 今天 | 实测证据 |
|---|---|---|---|---|
| 根/体区域、臂、循环条件与体 | `GCRegionKind`（`graph.h#GCRegionKind`） | `if`/`match`/`loop` | **能** | 2 sync 块、分支与循环语料打印并等价（`cml_contract.py#CORPUS`） |
| `Inline` 区域 | 同上（`graph.h#GCRegionKind` 的 `Inline`） | 无（内联是遍的产物） | **拒绝** | `camel x.cml std::opt std::cml` → `a 'inline' region, which is not a branch or loop region`（`cml.cpp#validateGraph`） |
| `Block` 区域（带标签的块） | 同上（`graph.h#GCRegionKind` 的 `Block`）；前端会建：`builder.cpp#Builder::visitExecNode`（`setRegionAttrs`） | `#tag { ... }` | **拒绝**（区域循环只放行 Arm/LoopCond/LoopBody） | 与上一行同一条诊断；节点的标签另在 `cml.cpp#validateGraph` 拒绝 |
| 节点标签/权限掩码 | `GCGraph::hasNodeAttrs()`（`graph.h#GCGraph::hasNodeAttrs`） | `#strict`、`#kernel` | **拒绝** | `trans/cml/refuse/tagged_node.cml` → `std::cml cannot print f: it has node attributes or tags.` |
| 声明标签（函数/类型/字段/参数） | 运行时图里**没有存放处**；只有前端服务层 `model.h#DeclFacts` 的 `declFacts`。命名一张图的声明标签有：`kGraphRefRule`（`graph.h#GCGraphRefRole`，`#vjp(rule)`） | `#strict`、`#macro`、`#shape`、`#width(32)` | **拒绝/丢失** | 第一份计划 §1.1 实测 `std::jgir` 里这些标签完全不出现；今天 `#macro` 图被 `cml.cpp#validateGraph` 拒绝（38 个用例） |
| 端口与角色（普通/上下文） | `GCNode` 的输入区（Norm 后 With，`graph.h#GCNode`）；目标模型是「一列带角色的端口」（`edges.cn.md:23-31`） | 形参与 `with <...>` | **能，但按今天的表示读** | `cml.cpp#buildTopo` 分别遍历 `normInputsOf`/`withInputsOf`；canonical 文档里 `context` 角色已存在（实测 `{normal 12, context 1, control 6}`） |
| 同步边 | 今天：`Ctrl` 输入 + `Gate`/`Sync` 节点（`cml.cpp#findEntry`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#bindLoopCond`、`cml.cpp#expression`） | `sync { ... }`、函数体后的 `sync` | **能，但从控制边反推** | `regionNeedsSync`（`cml.cpp#regionNeedsSync`）与 `regionReturnsValue`（`cml.cpp#regionReturnsValue`）都读同区域内的控制边；`std::gir` 实测该探针图有 `GATE: 2` 节点、3 条控制边 |
| 顺序边 | `GCGraphOrderEdge`、`orderPredsOf`/`orderEdges`（`graph.h#GCGraphOrderEdge`、`graph.h#GCGraph::orderPredsOf`、`graph.h#GCGraph::orderEdges`、`graph.h#GCGraphNativePayload`）；已参与节点身份（`GCGraph::nodeContentHash`，`graph.cpp#GCGraph::nodeContentHash`） | **没有**（设计稿 `edges.cn.md:42`：从 `sync` 与资源重新推出） | **不打印，也不检查** | 实测：一个 `var x = 1; x = 2; x = 3` + 两个 `println` 的程序，canonical 文档里 `order` 出现 **0** 次；打印器输出 2 个 sync 块、往返判为等价、两侧 stdout 都是 `3\|1\|2` |
| 分支与臂归属 | `BRCH` + `Arm` 区域（`edges.cn.md:47`：臂只靠区域归属，`BRCH` 与臂之间没有边） | `if`/`match` | **能** | 分支语料打印并等价 |
| 闭包与函数值 | 函数常量 + `FILL` + `capturePorts` | lambda、被捕获的名字 | **能**（有边界） | 共享的 lambda（一个函数值多处用）与匿名捕获名被拒绝（`cml.cpp#validateGraph`） |
| 调用（直接/间接） | `FUNC`/`CALL` | `f(x)`、函数值调用 | **直接能，间接常拒绝** | 26 个用例 `an indirect call without one function value`（`cml.cpp#callExpression`） |
| 效应与资源 | 效应的资源顺序来自 `sync` 与资源（`edges.cn.md:11`）；资源冲突由 `resources.cpp` 判定 | 效应就是普通调用 | **取决于 wn 算子是否有源码形式** | `test/cases/resources/effects.cml` → `the operator 'array:sort_in_place'`；`conflict_per_resource.cml` → `the operator 'rand:seed'` |
| 写法（egraph 等价类） | `hasWritings` + 抽取（`pass.cpp#applyPassesDetailed`） | **没有**（第一份计划 §7 已答：打印抽取后的图） | **拒绝，除非驱动器先抽取** | `std::cml` 的 `acceptsWritings()` 为假（`cml.h#CmlDumpPass::acceptsWritings`）；`std::opt::saturate std::cml` 实测打印成功（抽取后的可执行图） |
| 命名类型（struct/enum）与类型参数 | `StructType::named()`/`typeArgCount()` | `type Point = { x: int }`、`enum` | **拒绝** | `checkType` 的 `Struct` 分支（`cml.cpp#checkType`）；`Option<int>`、`Tensor`、`Mlp`、`Node`、`Program` 全走这里 |
| 带标签/可空/单元的元组与返回类型 | `TupleType::isTagged()/isNullable()/isCell()` | `T?`、`$tagged` 元组 | **拒绝** | `cml.cpp#checkType`（元组）、`cml.cpp#checkSignatureType`（返回类型；实测 31 个用例） |
| 数组、匿名 struct、元组、普通函数类型 | `TypeCode::Array/Struct/Tuple/Function` | 字面量 | **能** | `cml.cpp#checkType` 放行这些；其余落到同一函数的 `default` 分支 |
| 大常量 | 静态区 | 字面量 | **超限拒绝** | `camel big.cml std::cml max_literal_bytes=64` → `std::cml refused the constant 'xs' of type int[] in main: 128 bytes, over max_literal_bytes (64). Raise the limit with ...` |
| 收集循环 | `GCLoopRef::collectionCount`（`graph.h#GCLoopRef`） | 解释器的收集槽（源码写不出） | **拒绝** | `cml.cpp#loopExpression` → `a loop that collects values into arrays`；`std::cpp` 同样拒绝（`function_emitter.cpp#FunctionEmitter::lowerLoop`） |
| 宏图 | `kGraphFlagMacro` | `#macro` | **拒绝** | `cml.cpp#validateGraph`；`trans/effect_trace/macro_effect.cml` → `it is a macro graph` |
| 模块级捕获（库模块） | 图捕获表 + 顶层 `let` | 顶层 `let` | **拒绝** | `test/cases/trans/library_module.cml` → `std::cml cannot print shifted_square: it captures 'offset' from an enclosing function (a closure)`（`cml.cpp#validateGraph`） |
| 图名不是标识符 | `GCGraph::name()` | 无 | **拒绝** | `cml.cpp#validateGraph`，实测 58 个用例（例 `compile/array_shape/constant_dimension.cml`） |

## 3. 缺口清单（按名字、带复现、带「语法停在哪一行」）

**缺口 A：算子的源码形式表（463/1338，最大一类）。**
复现：`camel test/cases/compile/closure/function_values.cml std::cml` → `std::cml cannot print main: the operator ':str/format'. This version prints expressions, ...`。
停在哪：`cml.cpp#operatorExpression`（`:var/get`、`:var/*` 与其余算子 URI 都在这里 `fail`，`prefix` 里另有一条）。实测最常见的 URI：`:str/format` 71、`:mark/map_arr` 23、`env:get_env` 21、`tensor:reshape` 19、`:struct/len_str` 17、`:mark/reduce_arr` 9、`rand:seed` 8、`:struct/range` 8、`array:sort_in_place`、`rewrite:rule`。
代价：这是**表**的工作，不是语义的工作：每个 URI 要么给一条语法（`x->map<f>` 这类高阶形式），要么确认它是编译期/内部算子（`:mark/*`、`:struct/*`）而应当写成它降级前的源码形式。判断需要逐 URI 的证据，所以它是 S5 里最大的一块。

**缺口 B：命名类型与声明（273 + 66 + 35 + 31 = 405 个用例）。**
复现：`camel test/cases/trans/cml/refuse/named_type.cml std::cml` → `std::cml cannot print get: a parameter is the named type 'Point'.`；`camel test/cases/compile/enum/generic_tree.cml std::cml` → `... a parameter has the type 'Option<int>', which std::cml does not print.`；`camel test/cases/modules/onnx/export_train_step.cml std::cml` → `a 'with' parameter is the named type 'Mlp'`。
停在哪：`cml.cpp#checkType`（命名 struct 与 `default` 分支）、`cml.cpp#checkType` 的元组分支（带标签元组）。
代价：要打印声明本身（`type`/`enum` 声明、字段名、类型参数），还要一条**程序级声明表**（第一份计划 §7 问题 2 已答「程序级事实表，键为声明身份」）。`Tensor`、`Node`、`Program` 也是命名类型，所以 ONNX/autodiff/graph 三个模块语料的 38+ 个用例都挂在这一类上——它是**整程序**能不能打印的前提。

**缺口 C：优化过的图（`Inline` 区域）。**
复现：`camel test/cases/trans/cml/refuse/inline_region.cml std::opt::inline std::cml` → `std::cml cannot print main: a 'inline' region, which is not a branch or loop region`（`cml.cpp#validateGraph`）。
停在哪：`cml.cpp#validateGraph` 的区域循环只放行 `Arm`/`LoopCond`/`LoopBody`。
代价：内联体在源码里没有专门写法，唯一正确的做法是**把它的语句按它自己的顺序摊进外层区域**——也就是判定器已经在做的抹掉（`gir_canonical.cpp#CanonicalDocument::canonicalRegionOf`）。代价是「被内联的入口提升成虚拟 `main`」这条（`gir-interchange.md` 已列为「还没做，属于让打印器必要的那一步」）要同时落；否则原文的 `main` 图在重编译后没有调用者，canonical 的「只取可达图」会把两侧的图集比得不一样。**S3b 的第一件事就是量这一条**。

**缺口 D：顺序边（不打印可以，不检查不行）。**
复现：`camel probe.cml std::jgir canonical=true` 对
```camel
func f(): int sync { let a = println(1)
    let b = println(2)
    return b }
func main(): int sync { var x = 1
    x = 2
    x = 3
    println(x)
    return f() }
```
整个文档里 `order` 出现 0 次（节点字段只有 `callee/id/inputs/kind/payload/type/value`）。
代价：给 canonical v2 加 `syncPreds`/`orderPreds`（或把 `control` 拆成 `sync`/`order` 两种 kind），并让检查器比较它们。这**不改变源码表面**（`edges.cn.md:42`），只让判定器看得见第三种边。不做的话，打印机把顺序边写错/丢掉永远测不出来。

**缺口 E：`Block` 区域与标签（被 normalizer 抹掉）。**
复现：`camel test/cases/trans/cml/refuse/tagged_node.cml std::cml` → `it has node attributes or tags`；前端确实会建 `Block`（`builder.cpp#Builder::visitExecNode`）。
停在哪：打印器拒绝（`cml.cpp#validateGraph`）；判定器抹掉（`gir_canonical.cpp#CanonicalDocument::canonicalRegionOf`）。
代价：判定器要留下 `Block`（kind、parent、owner、attrs），`Inline` 继续抹掉；两者的理由不同（标签是程序内容，内联体不是另一个程序）。打印器要能打印 `#tag { }` 块；需要先有**标签声明的源码形式**（`tag finite = [kernel, heavy]` 这类今天在 `test/cases/compile/tags` 里就有）。

**缺口 F：模块级捕获（库模块）。**
复现：`camel test/cases/trans/library_module.cml std::cml` → `it captures 'offset' from an enclosing function (a closure)`（`cml.cpp#validateGraph`）。
代价：顶层 `let` 的值被函数捕获时，源码里就是读一个顶层名字。第一份计划 §7 已答「模块级捕获摊平成顶层 `let`」——今天只是没实现（`cml.cpp#validateGraph` 里读非顶层名字那条与捕获那条一起）。

**缺口 G：图名不是标识符（58 个）；间接调用（26 个）；大常量数据文件；收集循环；宏图；rest/constas 参数。**
各自的位置：`cml.cpp#validateGraph`、`cml.cpp#callExpression`、`cml.cpp#scanBigConstants`+`cml.h#CmlOptions::maxLiteralBytes`、`cml.cpp#loopExpression`、`cml.cpp#validateGraph`、`cml.cpp#checkSignatureType`。
其中「大常量」第一份计划已答「v1 超过 `max_literal_bytes` 拒绝并列出（实测消息见 §2 末行），数据引用的源码形式另行设计」——本计划把「另行设计」放到 S5，形状是**源码旁边的数据文件 + 一条读它的声明**，代价是要动 `test/data` 的组织与 `stdlib` 的读接口。

**缺口 H：参数种类（rest/`constas`/可变 lambda）。** 复现：`cml.cpp#checkSignatureType` 里的三条；`compile/constas/*` 与 `compile/rest_params/*` 里能被打印的两个用例**打印了但往返不等价**（见 §7），说明这一类还有第二种缺陷。

## 4. 往返性质与它怎么度量

**性质（沿用第一份计划 §2.2，不再改）**：对程序 `P`，`canon(P)` 与 `canon(compile(P std::cml))` 必须等价；检查器判等价，**不判文本**。文本只要求确定性：同一个图打印两次逐字节相同。

- **normalizer**：`std::jgir canonical=true`（版本 2）。它只留等价定义涉及的东西：可达图、活节点、节点种类与按种类和槽位的输入、算子身份（语义键）、静态值（按值）、区域（kind/parent/owner/index/result/ports/params/attrs）、类型表、声明原子、标签原子；名字、源码位置、编号都不在文档里（`docs/technical/gir-interchange.md`；`gir_canonical.cpp#CanonicalDocument`）。
- **检查器**：`test/tools/cml_roundtrip.py`。它对整份结构做颜色细化 + 互异个体化，比较结构类；预算用尽是 exit 2，**不会**被读成等价（`cml_roundtrip.py#equivalent`、`cml_roundtrip.py#main`）。
- **共享比较工具（不对往返负责）**：`test/tools/gir-dot.mjs#compareGirDotIsomorphic` + `test/tools/normalizers.mjs#applyNormalizers`，在 `graph_equal.mjs#GOLDENS` 记的那几条上用于「Camel 遍 vs C++ 遍」的语料对照（`graph_equal.mjs#NORMALIZERS` 是它用的规范化器）。它比的是 **GIR DOT**：节点签名是 `type|shape|style|in|out 度`，边签名是两端签名 + `linktype` + `label`（`gir-dot.mjs#nodeSignature`、`gir-dot.mjs#edgeType`），所以它**看不到算子身份、静态值、区域的 index/params/attrs**，而且它依赖显示转储的边类型——H242 正改这个（`gir.cpp#dumpJsonRuntimeGraph` 今天写 `control`）。结论：往返的判据仍是 v2 文档 + `cml_roundtrip.py`；`graph_equal.mjs` 只在它现有的语料里用，**不搬到往返上**，代价是同一个仓库里有两套图比较工具（比把它们合并便宜：合并要先让 DOT 携带算子身份与静态值，那是另一个任务）。
- **今天的实测结果**：348 个能打印的程序里 **309 等价 / 35 不等价 / 4 个打印不出 canonical**（`SWEEP files=2387 printed=348 refused=1338 frontend=701`、`ROUNDTRIP checked=348 equivalent=309 not_equivalent=35 unfaithful=4`）。
- **执行对照**：`cml_contract.py` 已经把「打印后重新编译的程序 stdout 与退出码相同」钉住（`cml_contract.py#check_round_trip`）。它与裁决 9.7（改写必须保持源码程序运行的每个效应，`design-principles.cn.md:556-558`）的关系：**必须在同一次构建里对照**。理由是 H242 自己的验收用例 `func f(): int { let a = println(1); let b = println(2); return b }` 在重构前打印 `1`、重构后只打印 `2`（`reviews/questions-2026-10-23.md` 的 h241-limits 第 2 条）——同一段源码的执行结果会随边的模型变，所以「和昨天记录的 stdout 一样」不是判据，「这一次构建里原程序与打印后程序一样」才是。
- **名字**：不进等价（第一份计划 §2.3 第 5 条），由 `cml_contract.py#check_names` 的名字检查单独钉。

## 5. 三种边与 H242：怎么做到不针对今天的表示

**今天打印机从哪里读边（8 处，逐处都会在 H242 后失效）**：

| 位置 | 读什么 | H242 之后 |
|---|---|---|
| `cml.cpp#findEntry`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#bindLoopCond`、`cml.cpp#expression` | `GCNodeKind::Gate` / `GCNodeKind::Sync` | 两种节点删除（`edges.cn.md:48`） |
| `cml.cpp#buildTopo`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#regionNeedsSync` | `ctrlInputsOf(ref)` | `Ctrl` 种类删除，存活只沿数据端口与同步边（`edges.cn.md:15, 19`） |
| `cml.cpp#buildTopo` | `normInputsOf` / `withInputsOf` 两个列表 | 合成**一列带角色的端口**（`edges.cn.md:25`） |

**做法（一个视图层，两个实现）**：在 `src/passes/trans/cml/` 里加一个只读视图 `PrintedGraph`，它把运行时图翻译成打印器唯一认识的东西：`ports(node)`（有序，带 `Normal`/`Context` 角色）、`syncPreds(node)`、`orderPreds(node)`、`regionOf(node)`、区域表；打印器其余部分只调视图，不再直接碰 `normInputsOf`/`withInputsOf`/`ctrlInputsOf`/`Gate`/`Sync`。

- 过渡期：视图有两个实现（今天的一套；H242 落地后的一套），用现有测试钉住两者对同一批语料的输出逐字节相同。代价：一个约 200 行的适配文件与一段"两份实现并存"的窗口；收益是不用在 H242 落地当天重写 2433 行里的 8 处深浅不一的读法。
- **同步边必须复原成 `sync`**：它是效应"被用到"的唯一载体，漏掉一个 `sync` 块，重编译后的程序就不再运行原程序运行的效应，直接违反 9.7。所以 `sync` 的复原判据要从「同区域内的控制边」换成视图的 `syncPreds`。
- **顺序边不打印**（设计稿 `edges.cn.md:42`：从 `sync` 与资源重新推出），但**必须进判定器**（缺口 D），否则"打印机把顺序边丢了"这种错误永远是绿的。
- **`BRCH` 与臂、`JOIN` 的端口**按设计稿第 5 节由节点种类定（`edges.cn.md:46`），视图不改这一层。

## 6. 工作顺序（每步的产物、验收、代价）

**S3a（先做的小竖切片，一个提交能看证据）——三种边与端口角色进判定器与视图。**
1. canonical v2 增加 `syncPreds`/`orderPreds`（缺口 D）；`cml_roundtrip.py` 比较它们。
2. 加 `PrintedGraph` 视图，打印器改调视图（8 处）。
3. 用例三条：`func f(): int sync { let a = println(1) ... }`（同步边 + 效应）、`var x = 1; x = 2; x = 3`（写写顺序边）、一个带 `with` 参数与一个 lambda 的程序（两种端口角色）。
4. 验收（可复现）：`std::jgir canonical=true` 的文档里出现两种前驱；把其中一条顺序边从图里删掉后**检查器必须报不等价**（今天报等价，这是这一步唯一能证明"看得见"的实验）；`std::cml` 打印后往返等价、同一次构建里 stdout 相同。
代价：v2 文档多两个字段（读者是自家检查器，版本号不动，因为 v2 尚未对外承诺；若决策者要求，改 v3，代价是多一个版本判定）。这一步**不动语言表面**。

**S3b——优化过的图。** 摊平 `Inline` 区域（缺口 C）+ 量「重编译后没有调用者的图算不算程序的一部分」；`camel x.cml std::opt std::cml` 必须能跑。代价：虚拟 `main` 的提升，以及一次关于"孤立图"的规范决定（建议：canonical 只取从入口沿调用可达的图，并在同一提交里把这条写进 `gir-interchange.md`——那份是技术文档，不是作者的目录）。

**S4——声明。** 命名类型与 enum、类型参数、程序级声明表、`Block`/标签、非标识符图名、带标签/可空元组。代价最大（405 个用例），但它是 `Tensor`/`Node`/`Program` 语料的前置，所以排在算子表之前。

**S5——其余算子与整程序。** 算子源码形式表（463 个用例，逐 URI 给语法或给"内部算子"的判定）、模块级捕获、间接调用、收集循环、宏图、大常量的数据文件、随机程序扫描（第一份计划 S5 的原文要求）。代价：表与逐 URI 证据，最耗时。

**S6——与 H242 合流。** 视图切到新实现、三种边的合流验收（`README.cn.md:398-410` 的 7 条）、四个后端一致、整语料扫描的通过数与"允许拒绝"清单一起交决策者裁定。

## 7. 验收用例（今天的结果是基线，不是目标）

| 用例 | 断言 | 今天 |
|---|---|---|
| `test/cases/trans/cml/{linear,branch,loop,closure,with_param,var_param}.cml` | 往返等价 + stdout + 确定性 + 名字 | **全绿**（`trans.cml` 计划） |
| 5 个 `refuse/*.cml` | 精确诊断、不产生半份输出 | **全绿**（`cml_contract.py#check_refusal`） |
| `std::opt::saturate std::cml` | 打印抽取后的可执行图 | **绿**（实测：`let x: int = 5`） |
| `std::opt std::cml` / `std::opt::inline std::cml` | 打印并往返等价 | **红**：`a 'inline' region ...` |
| `compile/constas/constas_column.cml` | 往返等价 | **红**：打印出 `func main(): int sync { return 0 + 1 + 2 + 3 }`，而原图多 3 个 DATA 节点与 3 个二元 OPER 节点（打印器**丢节点**） |
| `compile/rest_params/rest_inline.cml`、`rest_params.cml`、`constas_forward_rest.cml`、`constas_with_rest.cml` | 往返等价 | **红**（4 条同族） |
| `compile/return_type/accepted.cml` | 往返等价 | **红**：打印出 `func a(x: int): int { return x }`，原图多 1 个 DATA 节点 |
| `compile/diag_json/warnings_visible.cml` | 往返等价 | **红**：节点种类多重集相同、接线不同（检查器第一条差异：`a structural class has 1 vertex/vertices on the left and 0 on the right`） |
| `compile/static_expansion/indices_graph.cml` | 往返等价 | **红** |
| `modules/rules/check_rules.cml`、`trans/macro/*` | 打印 `rule`/`#macro` | **红**：`the operator 'rewrite:rule'`、`it is a macro graph` |
| `trans/library_module.cml` | 库模块打印 | **红**：`it captures 'offset' from an enclosing function` |
| 整语料 | 每一个能编译的用例要么往返等价要么在允许拒绝清单里 | **348/2387 打印，35 不等价，4 打印不出 canonical** |

**这 35 个不等价是这一步最有价值的资产**：它们证明检查器真的会红，也给出打印器"悄悄丢节点"的具体形态。S3a 的第一件事是把它们逐个定性（丢节点 / 改接线 / canonical 自身的问题），因为「打印器丢了节点」与「规范转储漏了东西」是两种完全不同的缺陷。

## 8. 要裁定的问题（本计划不擅自决定）

1. canonical v2 是否直接加 `syncPreds`/`orderPreds`，还是升 v3？（我倾向直接加：读者只有自家检查器，且 v2 尚未对外承诺。代价：若已有外部读者，会静默少两个字段。）
2. `Block` 留在 canonical，`Inline` 抹掉——是否照此裁定？（依据：标签是程序内容，内联体不是另一个程序。代价：`Block` 的 attrs 要参与等价，含标签的图会比较得更严。）
3. 大常量的数据文件形式（第一份计划把形状留到现在）：源码旁一个数据文件 + 一条读它的声明，还是别的形状？
4. 算子源码形式表里哪些 URI 判为"编译期/内部算子"（`:mark/*`、`:struct/*`）而永久拒绝？（这直接决定 §7 的"允许拒绝清单"。）
5. 裁决 9.7 与 H242 的合流验收里，「同一段源码在重构前后执行不同」是否需要语言侧的说明（教程/语言参考），以免用户把它当成往返的缺陷。

## 9. 本计划明确不做

不把源码或文本存进图；不为打印在图里加"源形状"字段；不因为打印不出就改语言语义；不把 `std::cml` 做成部署格式；不合并 `graph_equal.mjs` 与 `cml_roundtrip.py`；不在本计划里动 `src/runtime/**`（边的实现）、类型系统、`modules/tensor` 与 runner——那些是别的任务；GPU 与 `std::cml` 的交互（`#gpu` 块）留给 H49/H50 落地后另立计划。