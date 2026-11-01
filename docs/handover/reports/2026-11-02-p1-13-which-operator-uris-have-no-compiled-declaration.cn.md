# P1-13 前置读数：今天哪些算子 URI 没有"被编译的 `#op` 声明"

作者裁定 P1-7 的键**从声明读**（`#op(':op/add_d')` 那一行给头与元数），并指定 **P1-13 在前**：先让每个算子都有一条被编译的 `#op` 声明。这一页就是那一步的读数——**只量，不动键**。

## 怎么量的

- **算子清单**：`camel inspect --ops <probe>`（`OPERATOR` 行里的 `uri`）。probe 一次导入 `array`、`tensor`、`bytes`、`str`、`io`、`graph`、`stream`，取 **585** 个不同 URI（只导入 `tensor` 时是 396，什么都不导入时是 359：模块没导入的算子不在表里，所以清单取并集）。
- **声明清单**：`#op('…')` 出现在 `stdlib/` 与 `modules/` 的 `.cml` 里，**排除 `stdlib/signatures/`**——那个目录自己的文件头写着"this file is for reference only (it is not compiled)"（6 个文件），里面的声明不算被编译的声明。
- 日期 2026-11-02，`lane-gs6`（HEAD 为合并 `origin/develop 48613f31b` 之后），`out/latest` 由该提交构建。

## 数

| | |
|---|---|
| 被编译的 `.cml` 文件 | 45 |
| 注册的算子（并集 probe） | 585 |
| 其中有被编译 `#op` 声明 | **238** |
| **没有被编译 `#op` 声明** | **347**（59%） |
| 用 `func operator X` 写出写法的声明 | **5** |

| 协议 | 注册 | 无声明 |
|---|---|---|
| `:op` | 255 | **185** |
| `tensor` | 86 | **86** |
| `graph` | 44 | 42 |
| `var` | 6 | 6 |
| `array` | 33 | 4 |
| `io` | 8 | 5 |
| `bytes` | 4 | 4 |
| `str` | 9 | 4 |
| `coll` | 20 | 3 |
| `enum` / `struct` | 3 / 13 | 3 / 3 |
| `macro` / `mark` | 1 / 6 | 1 / 1 |
| `math` | 96 | 0 |
| `stream` | 1 | 0 |

## 对 P1-7 最要紧的两条

**1. 五个算术族 34 个算子只有 4 个有声明。** `:op/add_d`、`:op/add_f`、`:op/sub_d`、`:op/sub_f` 是前奏里仅有的四条（`stdlib/prelude.cml:84,89,94,99`）。`:op/mul_*` 0/8、`:op/div_*` 0/8、`:op/neg_*` 0/8。所以"键从声明读"今天只能读出 `+`/2 与 `-`/2 各两个类型，**`*`、`/`、一元 `-` 一条都读不出来**。

**2. `tensor` 协议 86 个算子一条被编译的声明都没有。** 包括批量族（`rulesets.vmap`）今天挂了规则的 21 个：`tensor:multiply`、`matmul`、`sum`、`sum_axis`、`mean`、`mean_axis`、`max`、`max_axis`、`min`、`min_axis`、`argmax`、`argmax_axis`、`argmin`、`argmin_axis`、`reshape`、`unsqueeze`、`transpose`、`permute`、`concat`、`dim`、`where`。`stdlib/tensor.cml` 里有这些名字的固定签名声明（`export func sum_axis::<T>(…)`），**但没有 `#op('tensor:sum_axis')` 那一行**；`stdlib/signatures/tensor.cml` 有 17 条 `#op`，而那个文件不被编译。

把三条读法要用的算子放在一起（32 个：五个算术族的十个、`:op/idx_arr`、21 个张量算子），**有被编译声明的只有 4 个**——正好是前奏里那四个。

**两个旧例外也没有声明**：`:op/idx_arr` 与 `tensor:multiply` 都不在任何被编译的 `#op` 里。作者的裁定要求 `xs[i]` 在声明处写成 `operator []`、`mul_tensor` 这个名字消失，两件事都得先有这一行声明。

## 3. 待修：33 条声明指向一个不存在的算子

**这一条不是补声明能解决的**——它是反方向：**有声明，没有算子**。在一个"全导入"程序里也没有注册的 URI：

| 协议 | 条数 | 例子 |
|---|---|---|
| `rewrite:` | 12 | `rewrite:rewrite`（`stdlib/rewrite.cml:98`）、`rewrite:saturate`、`rewrite:extract` |
| `rand:` | 9 | `rand:philox`（`stdlib/random.cml:40`）、`rand:uniform_array` |
| `ckpt:` | 2 | `ckpt:save_tree`、`ckpt:load_tree` |
| `re:` | 2 | `re:test`、`re:find` |
| `test_support:` | 2 | `test_support:with_probe_array`、`test_support:arity_two` |
| `:mark/` | 2 | `:mark/unordered_foreach_arr`、`:mark/unordered_reduce_arr` |
| `hash:` / `onnx:` / `:tangent/` | 各 1 | `hash:sha256_bytes`、`onnx:export_model`、`:tangent/scatter_add_arr` |

两种可能，都值得一条自己的记录：声明写在一个**没有被这个程序导入**的模块里（那是"分母怎么取"的问题），或者声明指向的算子**真的没有实现**（那是缺实现）。`--verify-ops`（`scripts/stdlib-reference.mjs`）盯的是同一对方向，但它按 `stdlib/signatures/` 的签名文件对，不按 `#op` 行，所以这 33 条它看不见。

**这一轮不修**，记在这里，免得下一次量"哪些算子没声明"时把它们混进分子。

## 4. `func operator X` 今天就能用，只是几乎没人用

5 条声明用了这个写法：`:op/add_f`→`+`、`:op/add_d`→`+`、`:op/sub_f`→`-`、`:op/sub_d`→`-`（`stdlib/prelude.cml:84,89,94,99`）与 `graph:eq`→`==`（`stdlib/graph.cml:317`）。所以 P1-13 要的不是新机制，是把已有写法补齐。

## 全清单

按协议分组、字典序列出 347 个没有被编译 `#op` 声明的算子。

## :op: 185 of 255

- `:op/add_bf`
- `:op/add_bytes`
- `:op/add_c`
- `:op/add_f6`
- `:op/add_i`
- `:op/add_l`
- `:op/add_s`
- `:op/add_z`
- `:op/and`
- `:op/assn`
- `:op/assn_add_bf`
- `:op/assn_add_c`
- `:op/assn_add_d`
- `:op/assn_add_f`
- `:op/assn_add_f6`
- `:op/assn_add_i`
- `:op/assn_add_l`
- `:op/assn_add_s`
- `:op/assn_add_z`
- `:op/assn_and`
- `:op/assn_b`
- `:op/assn_d`
- `:op/assn_div_bf`
- `:op/assn_div_c`
- `:op/assn_div_d`
- `:op/assn_div_f`
- `:op/assn_div_f6`
- `:op/assn_div_i`
- `:op/assn_div_l`
- `:op/assn_div_z`
- `:op/assn_f`
- `:op/assn_i`
- `:op/assn_l`
- `:op/assn_mod_i`
- `:op/assn_mod_l`
- `:op/assn_mul_bf`
- `:op/assn_mul_c`
- `:op/assn_mul_d`
- `:op/assn_mul_f`
- `:op/assn_mul_f6`
- `:op/assn_mul_i`
- `:op/assn_mul_l`
- `:op/assn_mul_z`
- `:op/assn_or`
- `:op/assn_owned`
- `:op/assn_pow_bf`
- `:op/assn_pow_c`
- `:op/assn_pow_c_i`
- `:op/assn_pow_c_l`
- `:op/assn_pow_d`
- `:op/assn_pow_f`
- `:op/assn_pow_f6`
- `:op/assn_pow_i`
- `:op/assn_pow_l`
- `:op/assn_pow_z`
- `:op/assn_pow_z_i`
- `:op/assn_pow_z_l`
- `:op/assn_s`
- `:op/assn_sub_bf`
- `:op/assn_sub_c`
- `:op/assn_sub_d`
- `:op/assn_sub_f`
- `:op/assn_sub_f6`
- `:op/assn_sub_i`
- `:op/assn_sub_l`
- `:op/assn_sub_z`
- `:op/atos`
- `:op/ctos`
- `:op/div_bf`
- `:op/div_c`
- `:op/div_d`
- `:op/div_f`
- `:op/div_f6`
- `:op/div_i`
- `:op/div_l`
- `:op/div_z`
- `:op/dtos`
- `:op/eq`
- `:op/eq_b`
- `:op/eq_bf`
- `:op/eq_bytes`
- `:op/eq_c`
- `:op/eq_d`
- `:op/eq_f`
- `:op/eq_f6`
- `:op/eq_i`
- `:op/eq_l`
- `:op/eq_s`
- `:op/eq_z`
- `:op/ftos`
- `:op/ge_bf`
- `:op/ge_d`
- `:op/ge_f`
- `:op/ge_f6`
- `:op/ge_i`
- `:op/ge_l`
- `:op/ge_s`
- `:op/gt_bf`
- `:op/gt_d`
- `:op/gt_f`
- `:op/gt_f6`
- `:op/gt_i`
- `:op/gt_l`
- `:op/gt_s`
- `:op/idx_arr`
- `:op/idx_str`
- `:op/idx_take`
- `:op/inv_i`
- `:op/inv_l`
- `:op/itos`
- `:op/le_bf`
- `:op/le_d`
- `:op/le_f`
- `:op/le_f6`
- `:op/le_i`
- `:op/le_l`
- `:op/le_s`
- `:op/lt_bf`
- `:op/lt_d`
- `:op/lt_f`
- `:op/lt_f6`
- `:op/lt_i`
- `:op/lt_l`
- `:op/lt_s`
- `:op/ltos`
- `:op/mod_i`
- `:op/mod_l`
- `:op/mul_bf`
- `:op/mul_c`
- `:op/mul_d`
- `:op/mul_f`
- `:op/mul_f6`
- `:op/mul_i`
- `:op/mul_l`
- `:op/mul_z`
- `:op/ne`
- `:op/ne_b`
- `:op/ne_bf`
- `:op/ne_bytes`
- `:op/ne_c`
- `:op/ne_d`
- `:op/ne_f`
- `:op/ne_f6`
- `:op/ne_i`
- `:op/ne_l`
- `:op/ne_s`
- `:op/ne_z`
- `:op/neg_bf`
- `:op/neg_c`
- `:op/neg_d`
- `:op/neg_f`
- `:op/neg_f6`
- `:op/neg_i`
- `:op/neg_l`
- `:op/neg_z`
- `:op/not`
- `:op/or`
- `:op/pow_bf`
- `:op/pow_c`
- `:op/pow_c_i`
- `:op/pow_c_l`
- `:op/pow_d`
- `:op/pow_f`
- `:op/pow_f6`
- `:op/pow_i`
- `:op/pow_l`
- `:op/pow_z`
- `:op/pow_z_i`
- `:op/pow_z_l`
- `:op/set_field`
- `:op/set_index`
- `:op/stos`
- `:op/sub_bf`
- `:op/sub_c`
- `:op/sub_f6`
- `:op/sub_i`
- `:op/sub_l`
- `:op/sub_z`
- `:op/tag_error_info`
- `:op/tag_error_value`
- `:op/tag_is_error`
- `:op/tag_is_null`
- `:op/tag_null_value`
- `:op/tag_throw`
- `:op/ztos`

## array: 4 of 33

- `array:dim`
- `array:numel`
- `array:shape`
- `array:to_bytes`

## bytes: 4 of 4

- `bytes:decode`
- `bytes:encode`
- `bytes:from_array`
- `bytes:to_array`

## coll: 3 of 20

- `:coll/eq`
- `:coll/map_idx`
- `:coll/neq`

## enum: 3 of 3

- `:enum/field`
- `:enum/make`
- `:enum/tag`

## graph: 42 of 44

- `graph:abort`
- `graph:add_constant`
- `graph:callee`
- `graph:commit`
- `graph:constant`
- `graph:declaration_tags`
- `graph:effects`
- `graph:erase`
- `graph:eval`
- `graph:graph_of`
- `graph:graphs_of`
- `graph:inputs`
- `graph:is_anchor`
- `graph:is_constant`
- `graph:is_pure`
- `graph:is_written_slot`
- `graph:kind`
- `graph:name`
- `graph:ne`
- `graph:nodes`
- `graph:op`
- `graph:op_name`
- `graph:open`
- `graph:order_inputs`
- `graph:origin`
- `graph:permissions`
- `graph:region_kind`
- `graph:region_of`
- `graph:region_parent`
- `graph:replace`
- `graph:rule_of`
- `graph:shape_text`
- `graph:show`
- `graph:sync_inputs`
- `graph:tag_graphs`
- `graph:tag_name`
- `graph:tags`
- `graph:type_name`
- `graph:type_of`
- `graph:users`
- `graph:value_users`
- `graph:with_inputs`

## io: 5 of 8

- `io:clear_input_buffer`
- `io:get_chars`
- `io:has_input`
- `io:input`
- `io:set_terminal_raw_mode`

## macro: 1 of 1

- `:macro/cmp`

## mark: 1 of 6

- `:mark/isolate`

## str: 4 of 9

- `str:codepoints`
- `str:from_codepoints`
- `str:to_lower`
- `str:to_upper`

## struct: 3 of 13

- `:struct/append_arr_owned`
- `:struct/copy_arr`
- `:struct/iter_str`

## tensor: 86 of 86

- `tensor:abs`
- `tensor:add`
- `tensor:argmax`
- `tensor:argmax_axis`
- `tensor:argmin`
- `tensor:argmin_axis`
- `tensor:concat`
- `tensor:dim`
- `tensor:divide`
- `tensor:erf`
- `tensor:erfc`
- `tensor:exp`
- `tensor:eye`
- `tensor:from_bytes_aggregate`
- `tensor:from_bytes_bf16`
- `tensor:from_bytes_bool`
- `tensor:from_bytes_f16`
- `tensor:from_bytes_f32`
- `tensor:from_bytes_f64`
- `tensor:from_bytes_i32`
- `tensor:from_bytes_i64`
- `tensor:full`
- `tensor:ge`
- `tensor:gelu`
- `tensor:gemm_backend`
- `tensor:gemm_library`
- `tensor:gemm_library_threads`
- `tensor:gemm_threads`
- `tensor:gt`
- `tensor:idx`
- `tensor:idx2d`
- `tensor:idx_take`
- `tensor:layer_norm`
- `tensor:le`
- `tensor:linear`
- `tensor:load_npy_bool`
- `tensor:load_npy_f32`
- `tensor:load_npy_f64`
- `tensor:load_npy_i32`
- `tensor:load_npy_i64`
- `tensor:log`
- `tensor:log_softmax`
- `tensor:lt`
- `tensor:matmul`
- `tensor:max`
- `tensor:max_axis`
- `tensor:maximum`
- `tensor:mean`
- `tensor:mean_axis`
- `tensor:min`
- `tensor:min_axis`
- `tensor:minimum`
- `tensor:mod`
- `tensor:multiply`
- `tensor:neg`
- `tensor:num_threads`
- `tensor:numel`
- `tensor:one_hot`
- `tensor:ones`
- `tensor:ones_like`
- `tensor:permute`
- `tensor:pow`
- `tensor:relu`
- `tensor:reshape`
- `tensor:rsqrt`
- `tensor:save_npy`
- `tensor:set_num_threads`
- `tensor:shape`
- `tensor:show`
- `tensor:sigmoid`
- `tensor:softmax`
- `tensor:softmax_grad`
- `tensor:sqrt`
- `tensor:strict_eq`
- `tensor:strict_neq`
- `tensor:subtract`
- `tensor:sum`
- `tensor:sum_axis`
- `tensor:take`
- `tensor:tanh`
- `tensor:to_bytes`
- `tensor:transpose`
- `tensor:unsqueeze`
- `tensor:where`
- `tensor:zeros`
- `tensor:zeros_like`

## var: 6 of 6

- `:var/after`
- `:var/get`
- `:var/new`
- `:var/new/empty`
- `:var/peek`
- `:var/take`
