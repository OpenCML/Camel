# 遍声明 → `#op` 映射表（只读研究）

口径：`export func X::<T>(p: T) => T!` 且声明窗口内有 `when is_function::<T>()`，扫 `src/stdlib/**/*.cml`。
共 **77 条**；每条的出处是它那一族的 `const PassKernel` 定义行（或 `registerModulePass` 行）。

判据补充（`c1-measure` 尖端实测）：给 `tensor.cml:399 fuse::<F>(f: F) => F!` 只加 `#op('tensor:fuse')`，`native_declarations` 计划 120/120 全绿、零诊断；同一行在 integration 上必红。
⇒ 本表"可直接替换的写法"一列只要与 `const PassKernel("…")` 的路径一致，就能加载。

| 文件 | 行 | 遍名（声明） | 可直接替换的写法 | 出处 | `task-228` 红 |
|---|---|---|---|---|---|
| src/stdlib/std.cml | 21 | std::nop | `#op('std::nop')` | src/stdlib/std/nop/nop.cpp:29 (NopPass::kName) | — |
| src/stdlib/std.cml | 30 | std::macro | `#op('std::macro')` | src/stdlib/std/macro/macro.cpp:170 | — |
| src/stdlib/std.cml | 48 | std::defunctionalize | `#op('std::defunctionalize')` | src/stdlib/std/cxx/opt/defun/defunctionalize.cpp:2021 | — |
| src/stdlib/std.cml | 55 | std::gir | `#op('std::gir')` | src/stdlib/std/gir/dot/graphviz.cpp:945 | — |
| src/stdlib/std.cml | 58 | std::rgir | `#op('std::rgir')` | src/stdlib/std/gir/dot/graphviz.cpp:946 | — |
| src/stdlib/std.cml | 62 | std::gir_regions | `#op('std::gir_regions')` | src/stdlib/std/gir/dot/graphviz.cpp:947 | — |
| src/stdlib/std.cml | 66 | std::rgir_regions | `#op('std::rgir_regions')` | src/stdlib/std/gir/dot/graphviz.cpp:950 | — |
| src/stdlib/std.cml | 77 | std::jgir | `#op('std::jgir')` | src/stdlib/std/gir/json/gir.cpp:538 | — |
| src/stdlib/std.cml | 83 | std::cml | `#op('std::cml')` | src/stdlib/std/cml/cml.cpp:4942 | — |
| src/stdlib/std.cml | 86 | std::agir | `#op('std::agir')` | src/stdlib/std/gir/dot/graphviz.cpp:953 | — |
| src/stdlib/std.cml | 89 | std::argir | `#op('std::argir')` | src/stdlib/std/gir/dot/graphviz.cpp:956 | — |
| src/stdlib/std.cml | 92 | std::stats | `#op('std::stats')` | src/stdlib/std/stats/stats.cpp:165 | — |
| src/stdlib/std.cml | 112 | std::rulesets | `#op('std::rulesets')` | src/stdlib/std/rulesets/rulesets.cpp:179 | — |
| src/stdlib/std.cml | 142 | std::profile | `#op('std::profile')` | src/stdlib/std/profile/profile.cpp:269 | — |
| src/stdlib/std.cml | 145 | std::cpp | `#op('std::cpp')` | src/stdlib/std/cpp/cpp_export.cpp:64 | — |
| src/stdlib/std.cml | 148 | std::topo_node_seq | `#op('std::topo_node_seq')` | src/stdlib/std/tns/topo_node_seq.cpp:238 | — |
| src/stdlib/std.cml | 151 | std::tfdump | `#op('std::tfdump')` | src/stdlib/std/prl/tfdump.cpp:173 | — |
| src/stdlib/std.cml | 154 | std::arraytype | `#op('std::arraytype')` | src/stdlib/std/selftest/selftest_passes.cpp:48 | — |
| src/stdlib/std.cml | 157 | std::identitytype | `#op('std::identitytype')` | src/stdlib/std/selftest/selftest_passes.cpp:49 | — |
| src/stdlib/std.cml | 161 | std::module_file | `#op('std::cpp::module_file')` | src/stdlib/std/cpp/cpp_export.cpp:65 | — |
| src/stdlib/std.cml | 164 | std::inspect | `#op('std::cpp::inspect')` | src/stdlib/std/cpp/cpp_export.cpp:66 | — |
| src/stdlib/std.cml | 173 | std::snapshot | `#op('std::gc::snapshot')` | src/stdlib/std/gc/gc_passes.cpp:259 | — |
| src/stdlib/std.cml | 176 | std::verify | `#op('std::gc::verify')` | src/stdlib/std/gc/gc_passes.cpp:260 | — |
| src/stdlib/std.cml | 179 | std::minor | `#op('std::gc::minor')` | src/stdlib/std/gc/gc_passes.cpp:261 | — |
| src/stdlib/std.cml | 182 | std::major | `#op('std::gc::major')` | src/stdlib/std/gc/gc_passes.cpp:262 | — |
| src/stdlib/std.cml | 185 | std::config | `#op('std::gc::config')` | src/stdlib/std/gc/gc_passes.cpp:263 | — |
| src/stdlib/std.cml | 188 | std::summary | `#op('std::gc::summary')` | src/stdlib/std/gc/gc_passes.cpp:264 | — |
| src/stdlib/std.cml | 191 | std::blocks | `#op('std::gc::blocks')` | src/stdlib/std/gc/gc_passes.cpp:265 | — |
| src/stdlib/std.cml | 194 | std::views | `#op('std::gc::views')` | src/stdlib/std/gc/gc_passes.cpp:266 | — |
| src/stdlib/std.cml | 197 | std::views_escape | `#op('std::gc::views_escape')` | src/stdlib/std/gc/gc_passes.cpp:267 | — |
| src/stdlib/std.cml | 200 | std::remembered_set | `#op('std::gc::remembered_set')` | src/stdlib/std/gc/gc_passes.cpp:268 | — |
| src/stdlib/std.cml | 203 | std::foreign_resource | `#op('std::gc::foreign_resource')` | src/stdlib/std/gc/gc_passes.cpp:270 | — |
| src/stdlib/std.cml | 206 | std::poison | `#op('std::gc::poison')` | src/stdlib/std/gc/gc_passes.cpp:272 | — |
| src/stdlib/std.cml | 212 | std::tuple_members | `#op('std::gc::tuple_members')` | src/stdlib/std/gc/gc_passes.cpp:273 | — |
| src/stdlib/std.cml | 213 | std::tuple_members_agree | `#op('std::gc::tuple_members_agree')` | src/stdlib/std/gc/gc_passes.cpp:274 | — |
| src/stdlib/std.cml | 221 | std::nvm | `#op('std::nvm')` | src/stdlib/std/nvm/nodevm.cpp:1258 | — |
| src/stdlib/std.cml | 233 | std::fvm | `#op('std::fvm')` | src/stdlib/std/fvm/fastvm.cpp:1606 | — |
| src/stdlib/std.cml | 237 | std::bytecode | `#op('std::fastvm::bytecode')` | src/stdlib/std/fvm/bcdump.cpp:79 | — |
| src/stdlib/std.cml | 240 | std::linked_bytecode | `#op('std::fastvm::linked_bytecode')` | src/stdlib/std/fvm/bcdump.cpp:80 | — |
| src/stdlib/std.cml | 252 | std::jit | `#op('std::fastvm::jit')` | src/stdlib/std/fvm/fastvm.cpp:1614 | — |
| src/stdlib/std.cml | 255 | std::jit_asm | `#op('std::fastvm::jit_asm')` | src/stdlib/std/fvm/jit/dump/asmdump.cpp:110 | — |
| src/stdlib/std.cml | 258 | std::jit_bin | `#op('std::fastvm::jit_bin')` | src/stdlib/std/fvm/jit/dump/bindump.cpp:189 | — |
| src/stdlib/std.cml | 261 | std::jit_rmir | `#op('std::fastvm::jit_rmir')` | src/stdlib/std/fvm/jit/dump/mirdump.cpp:139 | — |
| src/stdlib/std.cml | 264 | std::jit_mir | `#op('std::fastvm::jit_mir')` | src/stdlib/std/fvm/jit/dump/mirdump.cpp:140 | — |
| src/stdlib/std.cml | 296 | std::taskflow | `#op('std::taskflow')` | src/stdlib/std/prl/taskflow.cpp:3275 | — |
| src/stdlib/std.cml | 341 | std::opt | `#op('std::cxx::opt')` | src/stdlib/std/cxx/opt/opt/opt.cpp:44 | — |
| src/stdlib/std.cml | 347 | std::inline | `#op('std::cxx::inline')` | src/stdlib/std/cxx/opt/inline/inline.cpp:52 | — |
| src/stdlib/std.cml | 351 | std::small | `#op('std::cxx::small')` | src/stdlib/std/cxx/opt/inline/inline.cpp:53 | — |
| src/stdlib/std.cml | 355 | std::arm | `#op('std::cxx::arm')` | src/stdlib/std/cxx/opt/inline/inline.cpp:56 | — |
| src/stdlib/std.cml | 359 | std::hybrid | `#op('std::cxx::hybrid')` | src/stdlib/std/cxx/opt/inline/inline.cpp:59 | — |
| src/stdlib/std.cml | 363 | std::devirtualize | `#op('std::cxx::devirtualize')` | src/stdlib/std/cxx/opt/devirtualize/devirtualize.cpp:33 | — |
| src/stdlib/std.cml | 367 | std::specialize | `#op('std::cxx::specialize')` | src/stdlib/std/cxx/opt/specialize/specialize.cpp:33 | — |
| src/stdlib/std.cml | 373 | std::fold | `#op('std::cxx::fold')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2190 | — |
| src/stdlib/std.cml | 385 | std::algebra | `#op('std::cxx::algebra')` | src/stdlib/std/cxx/opt/generic/algebra.cpp:322 | — |
| src/stdlib/std.cml | 399 | std::saturate | `#op('std::cxx::saturate')` | src/stdlib/std/cxx/opt/opt/opt.cpp:59 | — |
| src/stdlib/std.cml | 402 | std::cse | `#op('std::cxx::cse')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2195 | — |
| src/stdlib/std.cml | 407 | std::dce | `#op('std::cxx::dce')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2197 | — |
| src/stdlib/std.cml | 412 | std::order | `#op('std::cxx::order')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2202 | — |
| src/stdlib/graph.cml | 526 | graph::probe_handle | `#op('graph::probe_handle')` | src/stdlib/graph/probe.cpp:81 | graph : 3 |
| src/stdlib/onnx.cml | 68 | onnx::export_model | `#op('onnx::export_model')` | src/stdlib/onnx/passes/export.cpp:54,831 | — |
| src/stdlib/opt.cml | 168 | opt::cse | `（不补，见第三节）` |  | — |
| src/stdlib/opt.cml | 220 | opt::dead_values | `（不补，见第三节）` |  | — |
| src/stdlib/opt.cml | 228 | opt::dce | `（不补，见第三节）` |  | — |
| src/stdlib/opt.cml | 333 | opt::fold | `（不补，见第三节）` |  | — |
| src/stdlib/opt.cml | 345 | opt::saturate | `（不补，见第三节）` |  | — |
| src/stdlib/rewrite.cml | 110 | rewrite::rewrite | `（不补，见第三节）` |  | rewrite : 3 |
| src/stdlib/rewrite.cml | 125 | rewrite::saturate | `（不补，见第三节）` |  | rewrite : 3 |
| src/stdlib/rewrite.cml | 135 | rewrite::extract | `#op('rewrite::extract')` | src/stdlib/rewrite/module.cpp:1869 | rewrite : 3 |
| src/stdlib/rules.cml | 398 | rules::simplify | `（不补，见第三节）` |  | — |
| src/stdlib/rules.cml | 408 | rules::saturate_all | `（不补，见第三节）` |  | — |
| src/stdlib/rules.cml | 414 | rules::accuracy | `（不补，见第三节）` |  | — |
| src/stdlib/rules.cml | 421 | rules::fusion | `（不补，见第三节）` |  | — |
| src/stdlib/tensor.cml | 400 | tensor::fuse | `#op('tensor::fuse')` | src/stdlib/tensor/passes/fuse.cpp:284 | tensor : 3 |
| src/stdlib/tiles.cml | 54 | tiles::compile | `#op('tiles::compile')` | src/stdlib/tiles/src/region_pass.cpp:87 | tiles : 3 |
| src/stdlib/tiles.cml | 64 | tiles::census | `#op('tiles::census')` | src/stdlib/tiles/src/census.cpp:97 | tiles : 3 |
| src/stdlib/tiles.cml | 74 | tiles::test_outline_compiled | `#op('tiles::test_outline_compiled')` | src/stdlib/tiles/src/region_test_pass.cpp:434 | tiles : 3 |
| src/stdlib/transforms.cml | 25 | transforms::reinterp | `#op('transforms::reinterp')` | src/stdlib/transforms/module.cpp:368 | — |
> **写法警示（待实测确认前请勿机械照抄）**：下表的"可直接替换的写法"里的 `#op(...)` 给的是**实现路径（双冒号，如 `std::cxx::opt`）**；
> 而算子身份的规范形是 **`module:function`（单冒号）**（`src/compile/src/gir/builder_modules.cpp:355` 的注释、
> `resourceIdentity()` 的 `m::r`→`m:r` 归并）。`#op` 的值被原样当 operator uri 使用（同文件 `nativeOperatorUri()`），
> `OperatorTable::find` 按文本查、不会自动归一 —— 所以"双冒号"能否查到，必须用一行实验定，不能照抄。
> 已实测的形状：`tensor` 族用 `#op(''tensor:fuse'')`（单冒号，`lane-kinds-oper` 的 120/120 实验）。
> 待定：`std` 族（例：`std::gir` 该写 `#op(''std:gir'')` 还是别的），以及 `std::cxx::opt` 这类第二个分隔符要不要保留。
## 边界两条（不计入 77，附签名与原因）

| 文件:行 | 声明原文 | 为什么不算遍形状 |
|---|---|---|
| src/stdlib/opt.cml:320 | `func fold_rounds::<F>(f: F, budget: int) => F! when is_function::<F>()` | 两个参数；它是遍的助手（被 `opt::fold` 调用），不是遍本身 |
| src/stdlib/prelude.cml | `export func attempt::<F>(f: F) => F!` | 没有 `when is_function::<F>()`：不是"接受一个函数"的遍形状 |

## 无指向：11 条（按"能不能补 `#op`"重排）

### A. 组合遍：9 条 —— **不补 `#op`，理由是它不是算子**

函数体只是调用别的遍（或自己的实现），没有内核可指；下表给出它调用的那个遍，供写卡人判断要不要往下追。

| 遍名 | 位置 | 函数体里的调用（原文） | 它调用的那个遍 | 结论 |
|---|---|---|---|---|
| opt::cse | src/stdlib/opt.cml:168 | `return if graphs_of(f)->any<graph_has_hit> then merge_hits(f)->cse else f` | `cse`（见下注） | 不补 |
| opt::dead_values | src/stdlib/opt.cml:220 | `… erased::<F>(d, hits)->dead_values` | 自身（尾调用）；`abandoned(d, f)` | 不补（内部遍，非 export） |
| opt::dce | src/stdlib/opt.cml:228 | `return dead_values(f)` | `opt::dead_values` | 不补 |
| opt::fold | src/stdlib/opt.cml:333 | `return if all_nodes(graphs_of(f))->any<evaluated> then fold_rounds(f, constant_budget) else f` | `fold_rounds`（边界两条之一） | 不补 |
| opt::saturate | src/stdlib/opt.cml:345 | `return f->saturate_rules<concat(algebra, distribution), rounds: rounds, strict: strict>` | `saturate_rules`；语义引擎是 `std::cxx::saturate`（`src/stdlib/rules.cml:402-404` 原文："`std::cxx::saturate` is the saturation alone, which keeps every writing"） | 不补遍自身；若决策者要指向引擎，候选是 `#op('std::cxx::saturate')` |
| rules::simplify | src/stdlib/rules.cml:398 | `return f->rewrite<concat(algebra, accuracy_laws), strict: strict>` | `rewrite::rewrite` | 不补 |
| rules::saturate_all | src/stdlib/rules.cml:408 | `return f->saturate<concat(algebra, distribution), rounds: rounds, strict: strict>->extract<nodes>` | `rewrite::saturate` → `rewrite::extract` | 不补 |
| rules::accuracy | src/stdlib/rules.cml:414 | `return f->rewrite<accuracy_laws>` | `rewrite::rewrite` | 不补 |
| rules::fusion | src/stdlib/rules.cml:421 | `return f->rewrite<fusion_laws>` | `rewrite::rewrite` | 不补 |

注：`opt::cse` 体里写出的 `cse` 落在 `opt` 模块自己的遍命名空间（`src/stdlib/opt.cml` 的 `cse`），不是 `std::cxx::cse`；两者同名不同物，写代码那格不要顺手替换。

### B. 模块函数，不是遍：2 条 —— **不补 `#op`，依据是函数表原文**

| 遍名 | 位置 | 判断依据（原文） | 结论 |
|---|---|---|---|
| rewrite::rewrite | src/stdlib/rewrite.cml:110 | `src/stdlib/rewrite/module.cpp:4717` 模块函数表 `{"rewrite", rewriteKernel}`；全 src 只有 `"rewrite::extract"` 走 `registerPassKernel`（`module.cpp:1868-1872`） | 不补（除非决策者把模块函数也算遍） |
| rewrite::saturate | src/stdlib/rewrite.cml:125 | 同上：`module.cpp:4722` `{"saturate", saturateKernel}` | 同上 |

## 第三判据：`task-228` 实测红名单（34 条，逐字，来源：`120 pass / 86 pass / 34 fail` 那次运行）

**映射口径（重要，别误读）：** 34 条是**测试用例名**，不是遍名；它们按**模块**分组，一条红＝该模块的某条声明被当 operator 查、查不到就拒。
本表能覆盖的只有 12 条红（4 个模块，各有遍形状声明）：`tensor` 3、`tiles` 3、`rewrite` 3、`graph` 3 —— 已在主表"`task-228` 红"列标出。
其余 22 条红的模块（`random` 3、`array` 3、`array_sort` 2、`declaration_type_argument` 1、`tensor_show` 5、`nn` 1、`autodiff` 1、`rule_reader` 3、`graph_constant` 3）在本表**没有遍形状声明**，属另一类声明（普通 native 声明），补 `#op` 的活不覆盖它们。

| 模块 | 条数 | 用例名（逐字） |
|---|---|---|
| random | 3 | native.random_declaration_import · native.random_wrong_arity · native.random_wrong_type |
| array | 3 | native.array_declaration_import · native.array_wrong_arity · native.array_wrong_type |
| array_sort | 2 | native.array_sort_wrong_arity · native.array_sort_wrong_type |
| （声明类型参数） | 1 | native.declaration_type_argument |
| tiles | 3 | native.tiles_declaration_import · native.tiles_wrong_arity · native.tiles_result_type |
| tensor | 3 | native.tensor_declaration_import · native.tensor_wrong_arity · native.tensor_wrong_type |
| tensor_show | 5 | native.tensor_show_declaration_import · native.tensor_show_wrong_arity · native.tensor_show_wrong_type · native.tensor_show_empty_array · native.tensor_show_empty_array_typed |
| nn | 1 | native.nn_declaration_import |
| autodiff | 1 | native.autodiff_declaration_import |
| rewrite | 3 | native.rewrite_declaration_import · native.rewrite_wrong_arity · native.rewrite_wrong_type |
| rule_reader | 3 | native.rule_reader_declaration_import · native.rule_reader_wrong_arity · native.rule_reader_wrong_type |
| graph_constant | 3 | native.graph_constant_import · native.graph_constant_wrong_arity · native.graph_constant_wrong_type |
| graph | 3 | native.graph_declaration_import · native.graph_wrong_arity · native.graph_wrong_type |

合计 **34**。原文形如：`(tensor)/…/stdlib/tensor.cml:399:1: [Error]: The declaration 'fuse' names the operator 'tensor:fuse', which no loaded module implements.`

