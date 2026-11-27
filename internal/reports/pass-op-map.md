# 遍声明 → `#op` 映射表

## 数字（决策者 D-1127-103 的最终口径）

**79 = 4 已有 `#op` + 65 要补 + 9 组合遍（有体，不补）+ 1 助手（`fold_rounds`，不补）**

- 4 已有：`rewrite:rewrite`、`rewrite:saturate`、`rewrite:extract`（`src/stdlib/rewrite.cml:108/123/133`）、`attempt`（`src/stdlib/prelude.cml:1387` `#op(':mark/attempt')`）。
- 65 要补：`std.cml` 58 + `tiles.cml` 3 + `tensor.cml` 1 + `transforms.cml` 1 + `graph.cml` 1 + `onnx.cml` 1。
- 9 组合遍：`opt` 5（cse / dead_values / dce / fold / saturate）+ `rules` 4（simplify / saturate_all / accuracy / fusion）。
- 1 助手：`opt.cml:320 fold_rounds::<F>(f: F, budget: int) => F!`（两个参数，不是遍形状）。

**决策者的一句裁定（据此定稿）：有函数体的组合遍是普通函数，不补 `#op`；`opt::saturate` 也不指引擎 —— 有体就是函数，指向引擎等于改了它的意思。**
（我上一轮转的“opt::saturate 指向引擎”候选**撤销**，表里不留：有体就是函数，指向引擎等于改它的意思。）

## 先例清点结论（两个尖端逐条核过）

- `origin/integration` 与 `c1-measure @ 3d7928a388` 上的 `#op(` 都是 **456 条**；逐笔核过 `c1-measure` 的 9 笔提交：**新增 0 条**。
- 456 条**全是单冒号** `module:name`（`array:dim`、`rewrite:rewrite`、`onnx:export_model`、`tensor:sum_axis`…）；**双冒号零先例**；**`std:` 族零先例**（核心算子走 `:op/add_l`、`:mark/map_arr` 这类 `:` 前缀内建族）。
- 通配先例：`#op('tensor:load_npy_*')`、`#op('tensor:from_bytes_*')`（`src/stdlib/tensor/signatures/tensor.cml:431/448`）。
- ⇒ **模块族照先例直接写 `#op('module:name')`；只有 std 族 58 条必须实测**（双冒号零先例，`std:` 族零先例）。

## 判据（下一个人照这个复核）

一条声明“已有 `#op`”的判据：**从声明行往上，跳过 `///` 注释、`with <…>` 行与空行之后，第一个非注释行必须是 `#op('…')`。**“前 10 行内出现过 `#op`”这种粗判会误判。

## 易误判点（别人最容易重犯的一处）

`src/stdlib/graph.cml:526 export func probe_handle::<F>(f: F) => F!` —— 它上方第 516 行有 `#op('graph:eq')`，但那条属于另一条声明（`operator ==(a: Tag, b: Tag) => bool`）。粗判会把它误算“已有”，按上面的判据它是**缺**的（属 65 要补里的 `graph` 1 条）。

## 第一节：4 条已有 `#op`（勿动）

| 文件 | 行 | 遍名 | 已有的 `#op` | 为什么它真在（依据） |
|---|---|---|---|---|
| src/stdlib/graph.cml | 526 | graph::probe_handle | `#op('graph:probe_handle')` | src/stdlib/graph/probe.cpp:81 |
| src/stdlib/onnx.cml | 68 | onnx::export_model | `#op('onnx:export_model')` | src/stdlib/onnx/passes/export.cpp:54,831 |
| src/stdlib/tensor.cml | 400 | tensor::fuse | `#op('tensor:fuse')` | src/stdlib/tensor/passes/fuse.cpp:284 |
| src/stdlib/tiles.cml | 54 | tiles::compile | `#op('tiles:compile')` | src/stdlib/tiles/src/region_pass.cpp:87 |
| src/stdlib/tiles.cml | 64 | tiles::census | `#op('tiles:census')` | src/stdlib/tiles/src/census.cpp:97 |
| src/stdlib/tiles.cml | 74 | tiles::test_outline_compiled | `#op('tiles:test_outline_compiled')` | src/stdlib/tiles/src/region_test_pass.cpp:434 |
| src/stdlib/transforms.cml | 25 | transforms::reinterp | `#op('transforms:reinterp')` | src/stdlib/transforms/module.cpp:368 |

| src/stdlib/rewrite.cml | 110 | rewrite::rewrite | `#op('rewrite:rewrite')`（同文件 :108） | 算子由模块函数表回答：`src/stdlib/rewrite/module.cpp:4717` `{"rewrite", rewriteKernel}` |
| src/stdlib/rewrite.cml | 125 | rewrite::saturate | `#op('rewrite:saturate')`（:123） | `module.cpp:4722` `{"saturate", saturateKernel}` |
| src/stdlib/rewrite.cml | 135 | rewrite::extract | `#op('rewrite:extract')`（:133） | 另有一条 PassKernel 登记：`registerPassKernel("rewrite::extract")`，`module.cpp:1868-1872` |
| src/stdlib/prelude.cml | 1388 | attempt | `#op(':mark/attempt')`（:1387） | 内建算子族 `:mark/*`（如 `#op(':mark/map_arr')`、`' :mark/filter_arr')`） |

**通用结论：模块函数也照样写 `#op('module:name')`**（`rewrite::rewrite` 就是模型：`#op('rewrite:rewrite')` 由模块函数表回答，不是例外）。

## 为什么 `std:gir` 今天会报错（这是**待改的现状**，不是规范）

`#op` 只有一种写法：`#op('<模块>:<模块内路径>')`，模块内命名空间仍用 `::` —— `std:gir`、`std:cxx::cse`、`onnx:model`。

今天 `#op('std:gir')` 报 `Unrecognized … pass 'std::gir'`，原因不是"std 族另有写法"，而是：
**std 的遍内核现在在过渡表里按字符串 `'std::gir'` 登记**（`registerModulePass(path)` 原样 intern 成键）。
C1 第 2 步把登记方式改成按 `#op` 的 URI 驻留成 `OperId` 之后，单冒号写法即通。

⇒ 所以"模块族单冒号 / std 族双冒号"是**缺陷**，不是两条规则；同一类事不能有两套写法。
⇒ 表里不保留"逐字双冒号"作为备选；也不要因为单冒号今天报错就写"不可用"。

`native.pass_shaped_not_loaded` 不归本卡：决策者已裁**不改断言**，是 C1 漏了"带 `#op` 但无实现要报 no loaded module implements it"的检查。

## 第二节：65 条要补 —— 模块族 7 条（`module:name`，一条规则）
| 文件 | 行 | 遍名（声明） | 可直接替换的写法 | 出处 |
|---|---|---|---|---|
## 第三节：65 条要补 —— std 族 58 条（`std:<模块内路径>`，模块内 `::` 保留）

写法（唯一一种）：`#op('<模块>:<模块内路径>')` —— 模块内命名空间仍用 `::`，只把**最外那一个** `::` 换成 `:`。

| 文件 | 行 | 遍名（声明） | 可直接替换的写法 | 实现路径出处 |
|---|---|---|---|---|
| src/stdlib/std.cml | 21 | std::nop | `#op('std:nop')` | src/stdlib/std/nop/nop.cpp:29 (NopPass::kName) |
| src/stdlib/std.cml | 30 | std::macro | `#op('std:macro')` | src/stdlib/std/macro/macro.cpp:170 |
| src/stdlib/std.cml | 48 | std::defunctionalize | `#op('std:defunctionalize')` | src/stdlib/std/cxx/opt/defun/defunctionalize.cpp:2021 |
| src/stdlib/std.cml | 55 | std::gir | `#op('std:gir')` | src/stdlib/std/gir/dot/graphviz.cpp:945 |
| src/stdlib/std.cml | 58 | std::rgir | `#op('std:rgir')` | src/stdlib/std/gir/dot/graphviz.cpp:946 |
| src/stdlib/std.cml | 62 | std::gir_regions | `#op('std:gir_regions')` | src/stdlib/std/gir/dot/graphviz.cpp:947 |
| src/stdlib/std.cml | 66 | std::rgir_regions | `#op('std:rgir_regions')` | src/stdlib/std/gir/dot/graphviz.cpp:950 |
| src/stdlib/std.cml | 77 | std::jgir | `#op('std:jgir')` | src/stdlib/std/gir/json/gir.cpp:538 |
| src/stdlib/std.cml | 83 | std::cml | `#op('std:cml')` | src/stdlib/std/cml/cml.cpp:4942 |
| src/stdlib/std.cml | 86 | std::agir | `#op('std:agir')` | src/stdlib/std/gir/dot/graphviz.cpp:953 |
| src/stdlib/std.cml | 89 | std::argir | `#op('std:argir')` | src/stdlib/std/gir/dot/graphviz.cpp:956 |
| src/stdlib/std.cml | 92 | std::stats | `#op('std:stats')` | src/stdlib/std/stats/stats.cpp:165 |
| src/stdlib/std.cml | 112 | std::rulesets | `#op('std:rulesets')` | src/stdlib/std/rulesets/rulesets.cpp:179 |
| src/stdlib/std.cml | 142 | std::profile | `#op('std:profile')` | src/stdlib/std/profile/profile.cpp:269 |
| src/stdlib/std.cml | 145 | std::cpp | `#op('std:cpp')` | src/stdlib/std/cpp/cpp_export.cpp:64 |
| src/stdlib/std.cml | 148 | std::topo_node_seq | `#op('std:topo_node_seq')` | src/stdlib/std/tns/topo_node_seq.cpp:238 |
| src/stdlib/std.cml | 151 | std::tfdump | `#op('std:tfdump')` | src/stdlib/std/prl/tfdump.cpp:173 |
| src/stdlib/std.cml | 154 | std::arraytype | `#op('std:arraytype')` | src/stdlib/std/selftest/selftest_passes.cpp:48 |
| src/stdlib/std.cml | 157 | std::identitytype | `#op('std:identitytype')` | src/stdlib/std/selftest/selftest_passes.cpp:49 |
| src/stdlib/std.cml | 161 | std::module_file | `#op('std:cpp::module_file')` | src/stdlib/std/cpp/cpp_export.cpp:65 |
| src/stdlib/std.cml | 164 | std::inspect | `#op('std:cpp::inspect')` | src/stdlib/std/cpp/cpp_export.cpp:66 |
| src/stdlib/std.cml | 173 | std::snapshot | `#op('std:gc::snapshot')` | src/stdlib/std/gc/gc_passes.cpp:259 |
| src/stdlib/std.cml | 176 | std::verify | `#op('std:gc::verify')` | src/stdlib/std/gc/gc_passes.cpp:260 |
| src/stdlib/std.cml | 179 | std::minor | `#op('std:gc::minor')` | src/stdlib/std/gc/gc_passes.cpp:261 |
| src/stdlib/std.cml | 182 | std::major | `#op('std:gc::major')` | src/stdlib/std/gc/gc_passes.cpp:262 |
| src/stdlib/std.cml | 185 | std::config | `#op('std:gc::config')` | src/stdlib/std/gc/gc_passes.cpp:263 |
| src/stdlib/std.cml | 188 | std::summary | `#op('std:gc::summary')` | src/stdlib/std/gc/gc_passes.cpp:264 |
| src/stdlib/std.cml | 191 | std::blocks | `#op('std:gc::blocks')` | src/stdlib/std/gc/gc_passes.cpp:265 |
| src/stdlib/std.cml | 194 | std::views | `#op('std:gc::views')` | src/stdlib/std/gc/gc_passes.cpp:266 |
| src/stdlib/std.cml | 197 | std::views_escape | `#op('std:gc::views_escape')` | src/stdlib/std/gc/gc_passes.cpp:267 |
| src/stdlib/std.cml | 200 | std::remembered_set | `#op('std:gc::remembered_set')` | src/stdlib/std/gc/gc_passes.cpp:268 |
| src/stdlib/std.cml | 203 | std::foreign_resource | `#op('std:gc::foreign_resource')` | src/stdlib/std/gc/gc_passes.cpp:270 |
| src/stdlib/std.cml | 206 | std::poison | `#op('std:gc::poison')` | src/stdlib/std/gc/gc_passes.cpp:272 |
| src/stdlib/std.cml | 212 | std::tuple_members | `#op('std:gc::tuple_members')` | src/stdlib/std/gc/gc_passes.cpp:273 |
| src/stdlib/std.cml | 213 | std::tuple_members_agree | `#op('std:gc::tuple_members_agree')` | src/stdlib/std/gc/gc_passes.cpp:274 |
| src/stdlib/std.cml | 221 | std::nvm | `#op('std:nvm')` | src/stdlib/std/nvm/nodevm.cpp:1258 |
| src/stdlib/std.cml | 233 | std::fvm | `#op('std:fvm')` | src/stdlib/std/fvm/fastvm.cpp:1606 |
| src/stdlib/std.cml | 237 | std::bytecode | `#op('std:fastvm::bytecode')` | src/stdlib/std/fvm/bcdump.cpp:79 |
| src/stdlib/std.cml | 240 | std::linked_bytecode | `#op('std:fastvm::linked_bytecode')` | src/stdlib/std/fvm/bcdump.cpp:80 |
| src/stdlib/std.cml | 252 | std::jit | `#op('std:fastvm::jit')` | src/stdlib/std/fvm/fastvm.cpp:1614 |
| src/stdlib/std.cml | 255 | std::jit_asm | `#op('std:fastvm::jit_asm')` | src/stdlib/std/fvm/jit/dump/asmdump.cpp:110 |
| src/stdlib/std.cml | 258 | std::jit_bin | `#op('std:fastvm::jit_bin')` | src/stdlib/std/fvm/jit/dump/bindump.cpp:189 |
| src/stdlib/std.cml | 261 | std::jit_rmir | `#op('std:fastvm::jit_rmir')` | src/stdlib/std/fvm/jit/dump/mirdump.cpp:139 |
| src/stdlib/std.cml | 264 | std::jit_mir | `#op('std:fastvm::jit_mir')` | src/stdlib/std/fvm/jit/dump/mirdump.cpp:140 |
| src/stdlib/std.cml | 296 | std::taskflow | `#op('std:taskflow')` | src/stdlib/std/prl/taskflow.cpp:3275 |
| src/stdlib/std.cml | 341 | std::opt | `#op('std:cxx::opt')` | src/stdlib/std/cxx/opt/opt/opt.cpp:44 |
| src/stdlib/std.cml | 347 | std::inline | `#op('std:cxx::inline')` | src/stdlib/std/cxx/opt/inline/inline.cpp:52 |
| src/stdlib/std.cml | 351 | std::small | `#op('std:cxx::small')` | src/stdlib/std/cxx/opt/inline/inline.cpp:53 |
| src/stdlib/std.cml | 355 | std::arm | `#op('std:cxx::arm')` | src/stdlib/std/cxx/opt/inline/inline.cpp:56 |
| src/stdlib/std.cml | 359 | std::hybrid | `#op('std:cxx::hybrid')` | src/stdlib/std/cxx/opt/inline/inline.cpp:59 |
| src/stdlib/std.cml | 363 | std::devirtualize | `#op('std:cxx::devirtualize')` | src/stdlib/std/cxx/opt/devirtualize/devirtualize.cpp:33 |
| src/stdlib/std.cml | 367 | std::specialize | `#op('std:cxx::specialize')` | src/stdlib/std/cxx/opt/specialize/specialize.cpp:33 |
| src/stdlib/std.cml | 373 | std::fold | `#op('std:cxx::fold')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2190 |
| src/stdlib/std.cml | 385 | std::algebra | `#op('std:cxx::algebra')` | src/stdlib/std/cxx/opt/generic/algebra.cpp:322 |
| src/stdlib/std.cml | 399 | std::saturate | `#op('std:cxx::saturate')` | src/stdlib/std/cxx/opt/opt/opt.cpp:59 |
| src/stdlib/std.cml | 402 | std::cse | `#op('std:cxx::cse')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2195 |
| src/stdlib/std.cml | 407 | std::dce | `#op('std:cxx::dce')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2197 |
| src/stdlib/std.cml | 412 | std::order | `#op('std:cxx::order')` | src/stdlib/std/cxx/opt/generic/generic.cpp:2202 |

`std::opt` 那一行不是例外：裁定要求**同一笔**里删 `declareSpelling("std::opt","std::cxx::opt")`（`src/kernel/execute/pass.cpp`）并在 `std.cml` 给 `opt` 写 `#op('std:cxx::opt')`；"整表失效"是两边同时声明同一个名字造成的。

## 第四节：9 条组合遍 —— **不补**（有函数体 ⇒ 是普通函数，不是算子）

| 遍名 | 位置 | 函数体里的调用（原文） | 结论 |
|---|---|---|---|
| opt::cse | src/stdlib/opt.cml:168 | `return if graphs_of(f)->any<graph_has_hit> then merge_hits(f)->cse else f` | 不补 `#op`（决策者 D-1127-103） |
| opt::dead_values | src/stdlib/opt.cml:220 | `… erased::<F>(d, hits)->dead_values（内部遍，非 export）` | 不补 `#op`（决策者 D-1127-103） |
| opt::dce | src/stdlib/opt.cml:228 | `return dead_values(f)` | 不补 `#op`（决策者 D-1127-103） |
| opt::fold | src/stdlib/opt.cml:333 | `return if all_nodes(graphs_of(f))->any<evaluated> then fold_rounds(f, constant_budget) else f` | 不补 `#op`（决策者 D-1127-103） |
| opt::saturate | src/stdlib/opt.cml:345 | `return f->saturate_rules<concat(algebra, distribution), rounds: rounds, strict: strict>` | 不补 `#op`（决策者 D-1127-103） |
| rules::simplify | src/stdlib/rules.cml:398 | `return f->rewrite<concat(algebra, accuracy_laws), strict: strict>` | 不补 `#op`（决策者 D-1127-103） |
| rules::saturate_all | src/stdlib/rules.cml:408 | `return f->saturate<concat(algebra, distribution), rounds: rounds, strict: strict>->extract<nodes>` | 不补 `#op`（决策者 D-1127-103） |
| rules::accuracy | src/stdlib/rules.cml:414 | `return f->rewrite<accuracy_laws>` | 不补 `#op`（决策者 D-1127-103） |
| rules::fusion | src/stdlib/rules.cml:421 | `return f->rewrite<fusion_laws>` | 不补 `#op`（决策者 D-1127-103） |

注：`opt::cse` 体里写出的 `cse` 落在 `opt` 模块自己的遍命名空间，**不是** `std::cxx::cse`；两者同名不同物。

## 第五节：1 条助手 —— **不补**

| 文件:行 | 声明原文 | 结论 |
|---|---|---|
| src/stdlib/opt.cml:320 | `func fold_rounds::<F>(f: F, budget: int) => F! when is_function::<F>()` | 两个参数，是 `opt::fold` 的助手，不是遍形状；不补 `#op` |
## 本表覆盖不到的那 22 条（34 条红名 − 本表覆盖的 12 条）

`task-228` 的 34 条红里，本表只覆盖 12 条（tensor 3 / tiles 3 / rewrite 3 / graph 3）。剩下 22 条**不在本表**，原因是它们**不是遍形状**（没有 `(f: F) => F!` + `when is_function::<F>()` 这一对），补 `#op` 不覆盖它们——它们是另一件事。

| 模块 | 用例名（逐字） | 本表为什么没有它（依据） |
|---|---|---|
| random | native.random_declaration_import · native.random_wrong_arity · native.random_wrong_type | `src/stdlib/random.cml` 的声明是普通函数，例如 `func philox(k: Key, counter: int) => int[]`（不是"接受一个函数"的遍形状） |
| array | native.array_declaration_import · native.array_wrong_arity · native.array_wrong_type | `src/stdlib/array.cml` 的声明是数组算子签名，例如 `export func reverse::<T>(xs: T[]) => T[]`；该文件里已有的 `#op(''array:dim'')`/`#op(''array:argsort_i'')` 说明 array 族普通算子已经带了 `#op`，与遍无关 |
| array_sort | native.array_sort_wrong_arity · native.array_sort_wrong_type | 同上（`array` 的排序算子） |
| （声明类型参数） | native.declaration_type_argument | 不是某个模块的遍声明，是一条关于"声明带类型参数"的用例 |
| tensor_show | native.tensor_show_declaration_import · native.tensor_show_wrong_arity · native.tensor_show_wrong_type · native.tensor_show_empty_array · native.tensor_show_empty_array_typed | `src/stdlib/tensor.cml:351 export func show::<A>(t: A[...]) => void when is_numeric::<A>() || is_same::<A, bool>()` —— 返回 `void`，不是遍 |
| nn | native.nn_declaration_import | `src/stdlib/nn.cml` 的 `export func save_params::<P>(params: P, dir: string) => void`；`src/stdlib/nn/signatures/nn.cml` 是 `conv2d(input, kernel, bias) => f32[...]` 这类算子签名 —— 都不是遍 |
| autodiff | native.autodiff_declaration_import | `src/stdlib/autodiff/signatures/autodiff.cml:52 export func grad::<F>(f: F) => Gradient<F> when is_function::<F>()` —— 接受函数但返回 `Gradient<F>`，是变换不是遍（本表口径要求 `=> F!`） |
| rule_reader | native.rule_reader_declaration_import · native.rule_reader_wrong_arity · native.rule_reader_wrong_type | `src/stdlib/rewrite.cml:234/240/246/252` 的 `rule_name(r: Rule) => string`、`rule_two_way`、`rule_needs`、`rule_consts` —— 读规则字段的函数 |
| graph_constant | native.graph_constant_import · native.graph_constant_wrong_arity · native.graph_constant_wrong_type | `src/stdlib/graph.cml:307 export func constant::<T>(n: Node) => T? when is_ordered::<T>()`、`:226 constant_bytes(n: Node) => int` —— 返回 `T?`/`int`，不是遍 |

合计 **22**。结论一句话：**补 `#op` 只解决 34 条红里的 12 条；另 22 条是"普通 native 声明没有算子"的另一件事，需要单独一张卡。**
## 边界两条（不计入 77，附签名与原因）

| 文件:行 | 声明原文 | 为什么不算遍形状 |
|---|---|---|
| src/stdlib/opt.cml:320 | `func fold_rounds::<F>(f: F, budget: int) => F! when is_function::<F>()` | 两个参数；它是遍的助手（被 `opt::fold` 调用），不是遍本身 |
| src/stdlib/prelude.cml | `export func attempt::<F>(f: F) => F!` | 没有 `when is_function::<F>()`：不是"接受一个函数"的遍形状 |

## 无指向：9 条组合遍（原 11 条里的 2 条 `rewrite::*` 已带 `#op`，移到第二节“已补好”）

### A. 组合遍：9 条 —— **不补 `#op`，理由是它不是算子**

函数体只是调用别的遍（或自己的实现），没有内核可指；下表给出它调用的那个遍，供写卡人判断要不要往下追。

| 遍名 | 位置 | 函数体里的调用（原文） | 它调用的那个遍 | 结论 |
|---|---|---|---|---|
| opt::cse | src/stdlib/opt.cml:168 | `return if graphs_of(f)->any<graph_has_hit> then merge_hits(f)->cse else f` | `cse`（见下注） | 不补 |
| opt::dead_values | src/stdlib/opt.cml:220 | `… erased::<F>(d, hits)->dead_values` | 自身（尾调用）；`abandoned(d, f)` | 不补（内部遍，非 export） |
| opt::dce | src/stdlib/opt.cml:228 | `return dead_values(f)` | `opt::dead_values` | 不补 |
| opt::fold | src/stdlib/opt.cml:333 | `return if all_nodes(graphs_of(f))->any<evaluated> then fold_rounds(f, constant_budget) else f` | `fold_rounds`（边界两条之一） | 不补 |
| opt::saturate | src/stdlib/opt.cml:345 | `return f->saturate_rules<concat(algebra, distribution), rounds: rounds, strict: strict>` | 不补 |
| rules::simplify | src/stdlib/rules.cml:398 | `return f->rewrite<concat(algebra, accuracy_laws), strict: strict>` | `rewrite::rewrite` | 不补 |
| rules::saturate_all | src/stdlib/rules.cml:408 | `return f->saturate<concat(algebra, distribution), rounds: rounds, strict: strict>->extract<nodes>` | `rewrite::saturate` → `rewrite::extract` | 不补 |
| rules::accuracy | src/stdlib/rules.cml:414 | `return f->rewrite<accuracy_laws>` | `rewrite::rewrite` | 不补 |
| rules::fusion | src/stdlib/rules.cml:421 | `return f->rewrite<fusion_laws>` | `rewrite::rewrite` | 不补 |

注：`opt::cse` 体里写出的 `cse` 落在 `opt` 模块自己的遍命名空间（`src/stdlib/opt.cml` 的 `cse`），不是 `std::cxx::cse`；两者同名不同物，写代码那格不要顺手替换。

### B. 模块函数：**已补好（勿动）** —— 依据是模块函数表原文，且 `#op` 写法与普通模块遍相同

| 遍名 | 位置 | 判断依据（原文） | 结论 |
|---|---|---|---|
| rewrite::rewrite | src/stdlib/rewrite.cml:110 | 声明上方已有 `#op('rewrite:rewrite')`（`rewrite.cml:108`）；算子由模块函数表 `{"rewrite", rewriteKernel}`（`module.cpp:4717`）回答 | **已补好，勿动**；结论：模块函数也照样写 `#op('module:name')` |
| rewrite::saturate | src/stdlib/rewrite.cml:125 | 声明上方已有 `#op('rewrite:saturate')`（`rewrite.cml:123`）；`module.cpp:4722` `{"saturate", saturateKernel}` | **已补好，勿动** |

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









