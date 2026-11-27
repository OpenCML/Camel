# 遍声明 → `#op`：哪些需要显式写

## 规则（D-1127-112，实现落在 `src/compile/src/gir/builder_modules.cpp:456`）

一条无体声明的算子 URI：**写了 `#op` 就用它；没写就是 `'<模块>:<声明名>'`**，命名空间里的声明名带命名空间（`std:cxx::cse`）。

- ⇒ **名字与内核 URI 一致的，一个 `#op` 都不用写**；C1 第 2 步按 URI 登记内核之后自然解析对。
- ⇒ **同名声明不需要写 `#op`**；只有内核在别处的才需要写。
- 命名空间算进声明名：`std.cml` 里 `namespace cxx { export func opt::<F>(f: F) => F! }` 的声明名是 `cxx::opt`，
  默认 URI 就是 `std:cxx::opt`，与该内核的登记名一致。

## 三个计数（主表 65 行）

- 主表行数：**65**
- 取值分布：**不写 65 / 写 0 / 待定 0**
- **“写”的名单：空。** 65 行的 `PassKernel` 登记路径与 `'<模块>:<声明名>'` 逐行比对，全部相等 ⇒ 一条都不用写。

## 主表（65 行；全表结论：默认即正确，无需显式 `#op`）

| 文件 | 行 | 声明（命名空间限定） | 默认算子 URI | 内核登记路径 | 要不要显式 `#op` | 备注 |
|---|---|---|---|---|---|---|
| src/stdlib/graph.cml | 526 | probe_handle | `graph:probe_handle` | `graph:probe_handle` | **不写** | 默认即 `graph:probe_handle` |
| src/stdlib/onnx.cml | 68 | export_model | `onnx:export_model` | `onnx:export_model` | **不写** | 默认即 `onnx:export_model` |
| src/stdlib/std.cml | 21 | nop | `std:nop` | `std:nop` | **不写** | 默认即 `std:nop` |
| src/stdlib/std.cml | 30 | macro | `std:macro` | `std:macro` | **不写** | 默认即 `std:macro` |
| src/stdlib/std.cml | 48 | defunctionalize | `std:defunctionalize` | `std:defunctionalize` | **不写** | 默认即 `std:defunctionalize` |
| src/stdlib/std.cml | 55 | gir | `std:gir` | `std:gir` | **不写** | 默认即 `std:gir` |
| src/stdlib/std.cml | 58 | rgir | `std:rgir` | `std:rgir` | **不写** | 默认即 `std:rgir` |
| src/stdlib/std.cml | 62 | gir_regions | `std:gir_regions` | `std:gir_regions` | **不写** | 默认即 `std:gir_regions` |
| src/stdlib/std.cml | 66 | rgir_regions | `std:rgir_regions` | `std:rgir_regions` | **不写** | 默认即 `std:rgir_regions` |
| src/stdlib/std.cml | 77 | jgir | `std:jgir` | `std:jgir` | **不写** | 默认即 `std:jgir` |
| src/stdlib/std.cml | 83 | cml | `std:cml` | `std:cml` | **不写** | 默认即 `std:cml` |
| src/stdlib/std.cml | 86 | agir | `std:agir` | `std:agir` | **不写** | 默认即 `std:agir` |
| src/stdlib/std.cml | 89 | argir | `std:argir` | `std:argir` | **不写** | 默认即 `std:argir` |
| src/stdlib/std.cml | 92 | stats | `std:stats` | `std:stats` | **不写** | 默认即 `std:stats` |
| src/stdlib/std.cml | 112 | rulesets | `std:rulesets` | `std:rulesets` | **不写** | 默认即 `std:rulesets` |
| src/stdlib/std.cml | 142 | profile | `std:profile` | `std:profile` | **不写** | 默认即 `std:profile` |
| src/stdlib/std.cml | 145 | cpp | `std:cpp` | `std:cpp` | **不写** | 默认即 `std:cpp` |
| src/stdlib/std.cml | 148 | topo_node_seq | `std:topo_node_seq` | `std:topo_node_seq` | **不写** | 默认即 `std:topo_node_seq` |
| src/stdlib/std.cml | 151 | tfdump | `std:tfdump` | `std:tfdump` | **不写** | 默认即 `std:tfdump` |
| src/stdlib/std.cml | 154 | arraytype | `std:arraytype` | `std:arraytype` | **不写** | 默认即 `std:arraytype` |
| src/stdlib/std.cml | 157 | identitytype | `std:identitytype` | `std:identitytype` | **不写** | 默认即 `std:identitytype` |
| src/stdlib/std.cml | 161 | cpp::module_file | `std:cpp::module_file` | `std:cpp::module_file` | **不写** | 默认即 `std:cpp::module_file` |
| src/stdlib/std.cml | 164 | cpp::inspect | `std:cpp::inspect` | `std:cpp::inspect` | **不写** | 默认即 `std:cpp::inspect` |
| src/stdlib/std.cml | 173 | gc::snapshot | `std:gc::snapshot` | `std:gc::snapshot` | **不写** | 默认即 `std:gc::snapshot` |
| src/stdlib/std.cml | 176 | gc::verify | `std:gc::verify` | `std:gc::verify` | **不写** | 默认即 `std:gc::verify` |
| src/stdlib/std.cml | 179 | gc::minor | `std:gc::minor` | `std:gc::minor` | **不写** | 默认即 `std:gc::minor` |
| src/stdlib/std.cml | 182 | gc::major | `std:gc::major` | `std:gc::major` | **不写** | 默认即 `std:gc::major` |
| src/stdlib/std.cml | 185 | gc::config | `std:gc::config` | `std:gc::config` | **不写** | 默认即 `std:gc::config` |
| src/stdlib/std.cml | 188 | gc::summary | `std:gc::summary` | `std:gc::summary` | **不写** | 默认即 `std:gc::summary` |
| src/stdlib/std.cml | 191 | gc::blocks | `std:gc::blocks` | `std:gc::blocks` | **不写** | 默认即 `std:gc::blocks` |
| src/stdlib/std.cml | 194 | gc::views | `std:gc::views` | `std:gc::views` | **不写** | 默认即 `std:gc::views` |
| src/stdlib/std.cml | 197 | gc::views_escape | `std:gc::views_escape` | `std:gc::views_escape` | **不写** | 默认即 `std:gc::views_escape` |
| src/stdlib/std.cml | 200 | gc::remembered_set | `std:gc::remembered_set` | `std:gc::remembered_set` | **不写** | 默认即 `std:gc::remembered_set` |
| src/stdlib/std.cml | 203 | gc::foreign_resource | `std:gc::foreign_resource` | `std:gc::foreign_resource` | **不写** | 默认即 `std:gc::foreign_resource` |
| src/stdlib/std.cml | 206 | gc::poison | `std:gc::poison` | `std:gc::poison` | **不写** | 默认即 `std:gc::poison` |
| src/stdlib/std.cml | 212 | gc::tuple_members | `std:gc::tuple_members` | `std:gc::tuple_members` | **不写** | 默认即 `std:gc::tuple_members` |
| src/stdlib/std.cml | 213 | gc::tuple_members_agree | `std:gc::tuple_members_agree` | `std:gc::tuple_members_agree` | **不写** | 默认即 `std:gc::tuple_members_agree` |
| src/stdlib/std.cml | 221 | nvm | `std:nvm` | `std:nvm` | **不写** | 默认即 `std:nvm` |
| src/stdlib/std.cml | 233 | fvm | `std:fvm` | `std:fvm` | **不写** | 默认即 `std:fvm` |
| src/stdlib/std.cml | 237 | fastvm::bytecode | `std:fastvm::bytecode` | `std:fastvm::bytecode` | **不写** | 默认即 `std:fastvm::bytecode` |
| src/stdlib/std.cml | 240 | fastvm::linked_bytecode | `std:fastvm::linked_bytecode` | `std:fastvm::linked_bytecode` | **不写** | 默认即 `std:fastvm::linked_bytecode` |
| src/stdlib/std.cml | 252 | fastvm::jit | `std:fastvm::jit` | `std:fastvm::jit` | **不写** | 默认即 `std:fastvm::jit` |
| src/stdlib/std.cml | 255 | fastvm::jit_asm | `std:fastvm::jit_asm` | `std:fastvm::jit_asm` | **不写** | 默认即 `std:fastvm::jit_asm` |
| src/stdlib/std.cml | 258 | fastvm::jit_bin | `std:fastvm::jit_bin` | `std:fastvm::jit_bin` | **不写** | 默认即 `std:fastvm::jit_bin` |
| src/stdlib/std.cml | 261 | fastvm::jit_rmir | `std:fastvm::jit_rmir` | `std:fastvm::jit_rmir` | **不写** | 默认即 `std:fastvm::jit_rmir` |
| src/stdlib/std.cml | 264 | fastvm::jit_mir | `std:fastvm::jit_mir` | `std:fastvm::jit_mir` | **不写** | 默认即 `std:fastvm::jit_mir` |
| src/stdlib/std.cml | 296 | taskflow | `std:taskflow` | `std:taskflow` | **不写** | 默认即 `std:taskflow` |
| src/stdlib/std.cml | 341 | cxx::opt | `std:cxx::opt` | `std:cxx::opt` | **不写** | 默认即 `std:cxx::opt` |
| src/stdlib/std.cml | 347 | cxx::inline | `std:cxx::inline` | `std:cxx::inline` | **不写** | 默认即 `std:cxx::inline` |
| src/stdlib/std.cml | 351 | cxx::small | `std:cxx::small` | `std:cxx::small` | **不写** | 默认即 `std:cxx::small` |
| src/stdlib/std.cml | 355 | cxx::arm | `std:cxx::arm` | `std:cxx::arm` | **不写** | 默认即 `std:cxx::arm` |
| src/stdlib/std.cml | 359 | cxx::hybrid | `std:cxx::hybrid` | `std:cxx::hybrid` | **不写** | 默认即 `std:cxx::hybrid` |
| src/stdlib/std.cml | 363 | cxx::devirtualize | `std:cxx::devirtualize` | `std:cxx::devirtualize` | **不写** | 默认即 `std:cxx::devirtualize` |
| src/stdlib/std.cml | 367 | cxx::specialize | `std:cxx::specialize` | `std:cxx::specialize` | **不写** | 默认即 `std:cxx::specialize` |
| src/stdlib/std.cml | 373 | cxx::fold | `std:cxx::fold` | `std:cxx::fold` | **不写** | 默认即 `std:cxx::fold` |
| src/stdlib/std.cml | 385 | cxx::algebra | `std:cxx::algebra` | `std:cxx::algebra` | **不写** | 默认即 `std:cxx::algebra` |
| src/stdlib/std.cml | 399 | cxx::saturate | `std:cxx::saturate` | `std:cxx::saturate` | **不写** | 默认即 `std:cxx::saturate` |
| src/stdlib/std.cml | 402 | cxx::cse | `std:cxx::cse` | `std:cxx::cse` | **不写** | 默认即 `std:cxx::cse` |
| src/stdlib/std.cml | 407 | cxx::dce | `std:cxx::dce` | `std:cxx::dce` | **不写** | 默认即 `std:cxx::dce` |
| src/stdlib/std.cml | 412 | cxx::order | `std:cxx::order` | `std:cxx::order` | **不写** | 默认即 `std:cxx::order` |
| src/stdlib/tensor.cml | 400 | fuse | `tensor:fuse` | `tensor:fuse` | **不写** | 默认即 `tensor:fuse` |
| src/stdlib/tiles.cml | 54 | compile | `tiles:compile` | `tiles:compile` | **不写** | 默认即 `tiles:compile` |
| src/stdlib/tiles.cml | 64 | census | `tiles:census` | `tiles:census` | **不写** | 默认即 `tiles:census` |
| src/stdlib/tiles.cml | 74 | test_outline_compiled | `tiles:test_outline_compiled` | `tiles:test_outline_compiled` | **不写** | 默认即 `tiles:test_outline_compiled` |
| src/stdlib/transforms.cml | 25 | reinterp | `transforms:reinterp` | `transforms:reinterp` | **不写** | 默认即 `transforms:reinterp` |

## 一处与决策者口径不符，已按证据写、请你判

`D-1127-112` 的要点里提到"已知一例：顶层 `opt` → `#op('std:cxx::opt')`"。本表把它判成**不写**，依据是它在源码里不是顶层：

```
src/stdlib/std.cml:319   namespace cxx {
src/stdlib/std.cml:341       export func opt::<F>(f: F) => F! when is_function::<F>()
src/stdlib/std.cml:413   }                     ← cxx 块在这里闭合，两行之间没有别的顶层声明
```
- ⇒ 声明名是 `cxx::opt`，默认 URI `std:cxx::opt`，与该遍内核 `const PassKernel("std::cxx::opt", …)`（`src/stdlib/std/cxx/opt/opt/opt.cpp:44`）相同 ⇒ 按规则不写。
- 用户写 `std::opt` 仍可用，是因为有**通行名别名**：`declareSpelling("std::opt", "std::cxx::opt")`（`src/kernel/execute/pass.cpp:111`，同组还有 `std::default`→`std::nvm`、`std::tf`→`std::taskflow` 等 10 余条）。
  **（这一句只在 `c1-measure` 枝上有；`origin/integration` 上没有 `declareSpelling`。引它是为了说明别名从哪来，不要当成现存代码读。）**
  别名是**给通行名用的**，不是声明缺 `#op` 的证据。
- 若决策者仍要求显式写，写上 `#op('std:cxx::opt')` 也无害，但与"同名不写"这条规则不一致；本表按规则判**不写**，等你一句话改。

## 已有 `#op` 的 4 条（勿动）

| 文件 | 行 | 声明 | 已有的 `#op` | 为什么它真在 |
|---|---|---|---|---|
| src/stdlib/rewrite.cml | 110 | rewrite | `#op('rewrite:rewrite')`（:108） | 模块函数表 `{"rewrite", rewriteKernel}`（`src/stdlib/rewrite/module.cpp:4717`） |
| src/stdlib/rewrite.cml | 125 | saturate | `#op('rewrite:saturate')`（:123） | `module.cpp:4722` `{"saturate", saturateKernel}` |
| src/stdlib/rewrite.cml | 135 | extract | `#op('rewrite:extract')`（:133） | `registerPassKernel("rewrite::extract")`（`module.cpp:1868-1872`） |
| src/stdlib/prelude.cml | 1388 | attempt | `#op(':mark/attempt')`（:1387） | 内建算子族 `:mark/*` |

## 9 条组合遍 —— 不补（有函数体 ⇒ 是普通函数，不是算子；D-1127-103）

| 遍名 | 位置 | 函数体里的调用（原文） |
|---|---|---|
| opt::cse | src/stdlib/opt.cml:168 | `return if graphs_of(f)->any<graph_has_hit> then merge_hits(f)->cse else f` |
| opt::dead_values | src/stdlib/opt.cml:220 | `… erased::<F>(d, hits)->dead_values（内部遍，非 export）` |
| opt::dce | src/stdlib/opt.cml:228 | `return dead_values(f)` |
| opt::fold | src/stdlib/opt.cml:333 | `return if all_nodes(graphs_of(f))->any<evaluated> then fold_rounds(f, constant_budget) else f` |
| opt::saturate | src/stdlib/opt.cml:345 | `return f->saturate_rules<concat(algebra, distribution), rounds: rounds, strict: strict>` |
| rules::simplify | src/stdlib/rules.cml:398 | `return f->rewrite<concat(algebra, accuracy_laws), strict: strict>` |
| rules::saturate_all | src/stdlib/rules.cml:408 | `return f->saturate<concat(algebra, distribution), rounds: rounds, strict: strict>->extract<nodes>` |
| rules::accuracy | src/stdlib/rules.cml:414 | `return f->rewrite<accuracy_laws>` |
| rules::fusion | src/stdlib/rules.cml:421 | `return f->rewrite<fusion_laws>` |

注：`opt::cse` 体里的 `cse` 落在 `opt` 模块自己的遍命名空间，**不是** `std::cxx::cse`；两者同名不同物。

## 1 条助手 —— 不补

| 文件:行 | 声明原文 | 结论 |
|---|---|---|
| src/stdlib/opt.cml:320 | `func fold_rounds::<F>(f: F, budget: int) => F! when is_function::<F>()` | 两个参数，是 `opt::fold` 的助手，不是遍形状；不补 `#op` |

## 判据（复核用）

“已有 `#op`”的判据：**从声明行往上，跳过 `///` 注释、`with <…>` 行与空行之后，第一个非注释行必须是 `#op('…')`。**
“默认 URI”的判据：**模块名（`std.cml` → `std`，`tiles/tensor/...` 取文件名）+ `:` + 声明名（含 `namespace` 前缀）。**
易误判点：`src/stdlib/graph.cml:526 probe_handle` 上方第 516 行有 `#op('graph:eq')`，那条属另一条声明（`operator ==`）；粗判会把它误算“已有”。

## 先例清点（两个尖端逐条核过，供写法参考）

- `origin/integration` 与 `c1-measure @ 3d7928a388` 上 `#op(` 都是 456 条，`c1-measure` 的 9 笔提交**新增 0 条**。
- 456 条全为 `module:name`（单冒号）写法；`std:` 族此前 0 条，是因为 std 的声明名带命名空间、而它们此前一条 `#op` 都没写。

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










