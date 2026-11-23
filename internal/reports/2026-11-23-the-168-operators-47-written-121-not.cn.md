# 168 个算子:47 个写 `panic`,121 个写明不写

合入时 `node scripts/check-operator-panic.mjs` 读到的 382 个登记算子里,168 个连一句可读陈述都没有:
47 个"实现会抛、没人说",121 个"实现不抛、也没人说"。这一笔把 47 个说上,121 个逐项说不写,
并给出补齐后的逐项对照。

## 一 读数

改前:

```
[operator-panic] 214 of 382 registered operators state `panic` in a declaration this scan can read; the C++ entrance writes 2 statements as calls and 96 in tables. The rest state nothing, which reads as "no statement", not as "answered".
[operator-panic] 48 operators raise and state no `panic`; 331 agree; 3 state a fault their implementation does not raise
```

改后:

```
[operator-panic] 261 of 382 registered operators state `panic` in a declaration this scan can read; the C++ entrance writes 2 statements as calls and 117 in tables. The rest state nothing, which reads as "no statement", not as "answered".
[operator-panic] 1 operators raise and state no `panic`; 378 agree; 3 state a fault their implementation does not raise
```

不动的是另外两行:350 处可达抛出(163 次其它运行时故障 / 90 次除零 / 67 次非预期抛出 / 28 次越界 / 2 次
`fail`)、104 个不同的抛出点;38 个只抛编译器缺陷(`OperandTypeUnsupported`、`NullSlotUnwrapped`)的算子
仍然不计 —— 那不是程序的故障。

261 = 214 + 47,`48` 剩 1,`331 + 47 = 378`。121 个没有写上,理由在第三节;它们若写上,`48` 会变成 0
却换来 121 条"声明了却不抛",这正是这个检查要防的假承诺。

## 二 47 条:写在哪、属于哪一类

前 21 条只有 C++ 入口 —— 它们是核心语法形式(`:op/assn_*`、`:op/div`、`:op/mod`、`:op/set_*`)、
`enum:tag`,以及 autodiff 自己插的 `:tangent/*_arr`。Camel 侧没有可以挂 `#op` 的声明,所以在
`builtin_semantics.cpp` 里加第二张表 `kCppPanicStatementsFaulting`(第一张 96 条的 `kCppPanicStatements`
一行没动)。这张表只登记记录、不写故障表:算术类算子的故障表由 `declare` 侧给出,其余留在保守的默认;
读回仍然比一次 `mayFault` 与记录,答得不一样就拒绝加载并点名,和第一张表同一个规矩。

后 26 条是 13 个模块里的 26 个算子(33 行声明):加 `#op` 标记(名字与算子名不同,或模块前缀不同,标记是让检查读得到的唯一途径)
并补 `panic` 子句。`panic` 写在 `uses` 之前 —— 语法(`parser_type.cpp`)要求这个次序,写成 `uses ... panic`
会被读成一个叫 panic 的资源(`:ckpt/save_tree` 与 `:ckpt/load_tree` 就是这个次序)。

| 算子 | 实现里读到的故障类别 | 陈述写在哪 |
|---|---|---|
| `:enum/tag` | InvalidArgument | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_bf` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_c` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_d` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_f` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_f6` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_i` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_l` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_div_z` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_mod_i` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/assn_mod_l` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/div` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/mod` | DivisionByZero | `kCppPanicStatementsFaulting[]` |
| `:op/set_field` | IndexOutOfRange | `kCppPanicStatementsFaulting[]` |
| `:op/set_index` | IndexOutOfRange | `kCppPanicStatementsFaulting[]` |
| `:op/set_index_in_place` | IndexOutOfRange | `kCppPanicStatementsFaulting[]` |
| `:tangent/add_arr` | RuntimeError | `kCppPanicStatementsFaulting[]` |
| `:tangent/init_arr` | RuntimeError | `kCppPanicStatementsFaulting[]` |
| `:tangent/last_arr` | RuntimeError | `kCppPanicStatementsFaulting[]` |
| `:tangent/scatter_slice_arr` | InvalidArgument,RuntimeError,SliceOutOfRange | `kCppPanicStatementsFaulting[]` |
| `:tangent/zeros_arr` | RuntimeError | `kCppPanicStatementsFaulting[]` |
| `array:dim` | InvalidArgument | `array.cml` 的 `dim` |
| `array:to_bytes` | InvalidArgument | `array.cml` 的 `to_bytes` |
| `fs:read_chunk` | InvalidArgument | `fs.cml` 的 `read_chunk` |
| `io:get_chars` | InvalidArgument | `io.cml` 的 `get_chars` |
| `json:encode` | InvalidArgument | `json.cml` 的 `encode` |
| `process:sleep` | InvalidArgument,RuntimeError | `process.cml` 的 `sleep` |
| `pyplot:plot` | RuntimeError,unexpected-throw | `pyplot.cml` 的 `plot` |
| `python:attr` | RuntimeError | `python.cml` 的 `attr` |
| `python:call` | RuntimeError | `python.cml` 的 `call` |
| `python:eval` | RuntimeError | `python.cml` 的 `eval` |
| `python:exec` | RuntimeError | `python.cml` 的 `exec` |
| `python:from_py` | RuntimeError,unexpected-throw | `python.cml` 的 `from_py` |
| `python:import_module` | RuntimeError | `python.cml` 的 `import_module` |
| `python:run_file` | RuntimeError | `python.cml` 的 `run_file` |
| `python:to_py` | RuntimeError | `python.cml` 的 `to_py` |
| `python:to_string` | RuntimeError | `python.cml` 的 `to_string` |
| `rewrite:check` | unexpected-throw | `rewrite.cml` 的 `check` |
| `sim:asian_call_price` | RuntimeError | `sim.cml` 的 `asian_call_price` |
| `sim:pi_hits` | RuntimeError | `sim.cml` 的 `pi_hits` |
| `state:get` | InvalidArgument | `state.cml` 的 `get` |
| `state:set` | InvalidArgument | `state.cml` 的 `set` |
| `state:update` | InvalidArgument | `state.cml` 的 `update` |
| `str:from_codepoints` | InvalidArgument | `str.cml` 的 `from_codepoints` |
| `test_support:rendezvous` | InvalidArgument | `test_support.cml` 的 `rendezvous` |
| `test_support:thread_count` | RuntimeError | `test_support.cml` 的 `thread_count` |
| `time:strftime` | TimeFormatFailed | `time.cml` 的 `strftime` |

`pyplot:plot` 一个标记盖 8 个重载(它们本来就都落到 `pyplot:plot`),所以 26 个算子对应 26 个标记、33 行声明。

## 三 121 条:逐项为什么不写

依据是同一个检查读到的那一句:`(nothing raised)` —— 实现里没有可达的抛出点。写 `panic` 就是替一条
不会失败的路径许下"会失败",而设计原则 3.9 里 `panic` 的缺席才是不故障的承诺。

第三栏是"有没有一条已经存在的陈述":这 121 条里 43 条其实有一条正文之外的空声明(默认 URI 恰好就是
模块:名,只是没有 `#op` 标记,检查读不到),78 条根本没有任何声明 —— 它们是核心的语法形式,只有 C++ 入口。

| 算子 | 实现入口(检查读到的内核) | 已有的声明 |
|---|---|---|
| `:enum/field` | __enum_field__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:enum/make` | __enum_make__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:macro/cmp` | __cmp__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_bf` | __builtin__assn_add_bf__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_c` | __builtin__assn_add_c__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_d` | __builtin__assn_add_d__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_f` | __builtin__assn_add_f__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_f6` | __builtin__assn_add_f6__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_i` | __builtin__assn_add_i__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_l` | __builtin__assn_add_l__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_s` | __builtin__assn_add_s__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_add_z` | __builtin__assn_add_z__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_and` | __builtin__assn_and__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_b` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_c` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_d` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_f` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_i` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_l` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_bf` | __builtin__assn_mul_bf__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_c` | __builtin__assn_mul_c__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_d` | __builtin__assn_mul_d__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_f` | __builtin__assn_mul_f__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_f6` | __builtin__assn_mul_f6__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_i` | __builtin__assn_mul_i__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_l` | __builtin__assn_mul_l__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_mul_z` | __builtin__assn_mul_z__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_or` | __builtin__assn_or__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_owned` | __builtin__assn_owned__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_bf` | __builtin__assn_pow_bf__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_c` | __builtin__assn_pow_c__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_c_i` | __builtin__assn_pow_c__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_c_l` | __builtin__assn_pow_c__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_d` | __builtin__assn_pow_d__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_f` | __builtin__assn_pow_f__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_f6` | __builtin__assn_pow_f6__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_i` | __builtin__assn_pow_i__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_l` | __builtin__assn_pow_l__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_z` | __builtin__assn_pow_z__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_z_i` | __builtin__assn_pow_z__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_pow_z_l` | __builtin__assn_pow_z__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_s` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_bf` | __builtin__assn_sub_bf__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_c` | __builtin__assn_sub_c__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_d` | __builtin__assn_sub_d__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_f` | __builtin__assn_sub_f__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_f6` | __builtin__assn_sub_f6__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_i` | __builtin__assn_sub_i__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_l` | __builtin__assn_sub_l__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_sub_z` | __builtin__assn_sub_z__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/assn_z` | __builtin__assn__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/mul` | __builtin__mul__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/ne` | __builtin__ne__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/pow` | __builtin__pow__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_error_info` | __error_info__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_error_value` | __error_value__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_is_error` | __is_error__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_is_null` | __is_null__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_null_value` | __null_value__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_throw` | __throw__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:op/tag_unwrap` | __unwrap__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:profiler/begin` | __profiler_begin__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:profiler/enable` | __profiler_enable__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:profiler/end` | __profiler_end__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:profiler/instant` | __profiler_instant__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:struct/append_arr_in_place` | __append_in_place_arr__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:struct/append_arr_owned` | __append_owned_arr__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:struct/copy_arr` | __copy_arr__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:struct/iter_str` | __iter_str__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:var/after` | __builtin__var_after__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:var/get` | __builtin__var_get__ (src/stdlib/std/builtin/executors/builtin.cpp) | `prelude.cml` 的 `get` |
| `:var/new` | __builtin__var_new__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:var/new/empty` | __builtin__var_new_empty__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:var/peek` | __builtin__var_peek__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `:var/take` | __builtin__var_take__ (src/stdlib/std/builtin/executors/builtin.cpp) | — |
| `array:numel` | __array_numel__ (src/stdlib/std/builtin/operators/array.cpp) | `array.cml` 的 `numel` |
| `array:shape` | __array_shape__ (src/stdlib/std/builtin/operators/array.cpp) | `array.cml` 的 `shape` |
| `env:get_cwd` | __env_get_cwd__ (src/stdlib/env/operators.cpp) | `env.cml` 的 `get_cwd` |
| `env:get_env` | __env_get_env__ (src/stdlib/env/operators.cpp) | `env.cml` 的 `get_env` |
| `fs:append_bytes` | __fs_append_bytes__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `append_bytes` |
| `fs:file_size` | __fs_file_size__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `file_size` |
| `fs:is_dir` | __fs_is_dir__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `is_dir` |
| `fs:is_file` | __fs_is_file__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `is_file` |
| `fs:list_dir` | __fs_list_dir__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `list_dir` |
| `fs:mkdir` | __fs_mkdir__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `mkdir` |
| `fs:mkdirs` | __fs_mkdirs__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `mkdirs` |
| `fs:read_bytes` | __fs_read_bytes__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `read_bytes` |
| `fs:remove` | __fs_remove__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `remove` |
| `fs:remove_dir` | __fs_remove_dir__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `remove_dir` |
| `fs:write_bytes` | __fs_write_bytes__ (src/stdlib/fs/operators.cpp) | `fs.cml` 的 `write_bytes` |
| `io:clear_input_buffer` | __io_clear_input_buffer__ (src/stdlib/io/operators.cpp) | `io.cml` 的 `clear_input_buffer` |
| `io:has_input` | __io_has_input__ (src/stdlib/io/operators.cpp) | `io.cml` 的 `has_input` |
| `io:input` | __io_input__ (src/stdlib/io/operators.cpp) | `io.cml` 的 `input` |
| `io:set_terminal_raw_mode` | __io_set_terminal_raw_mode__ (src/stdlib/io/operators.cpp) | `io.cml` 的 `set_terminal_raw_mode` |
| `json:as_array` | __json_as_array__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `as_array` |
| `json:as_bool` | __json_as_bool__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `as_bool` |
| `json:as_float` | __json_as_float__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `as_float` |
| `json:as_int` | __json_as_int__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `as_int` |
| `json:as_object` | __json_as_object__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `as_object` |
| `json:as_string` | __json_as_string__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `as_string` |
| `json:decode` | __json_decode__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `decode` |
| `json:decode_as` | __json_decode_as__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `decode_as` |
| `json:get` | __json_get__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `get` |
| `json:kind` | __json_kind__ (src/stdlib/json/operators.cpp) | `json.cml` 的 `kind` |
| `process:cpu_count` | __process_cpu_count__ (src/stdlib/process/operators.cpp) | `process.cml` 的 `cpu_count` |
| `process:exit` | __process_exit__ (src/stdlib/process/operators.cpp) | `process.cml` 的 `exit` |
| `process:user_name` | __process_user_name__ (src/stdlib/process/operators.cpp) | `process.cml` 的 `user_name` |
| `re:validate` | __re_validate__ (src/stdlib/re/operators.cpp) | `re.cml` 的 `validate` |
| `rewrite:check_text` | checkTextKernel (src/stdlib/rewrite/module.cpp) | `rewrite.cml` 的 `check_text` |
| `rewrite:rule` | ruleKernel (src/stdlib/rewrite/module.cpp) | — |
| `rewrite:rule_generic` | genericRuleKernel (src/stdlib/rewrite/module.cpp) | — |
| `rewrite:rule_when` | ruleWhenKernel (src/stdlib/rewrite/module.cpp) | — |
| `state:ref` | refKernel (src/stdlib/state/module.cpp) | `state.cml` 的 `ref` |
| `str:codepoints` | __str_codepoints__ (src/stdlib/str/operators.cpp) | `str.cml` 的 `codepoints` |
| `str:to_lower` | __str_to_lower__ (src/stdlib/str/operators.cpp) | `str.cml` 的 `to_lower` |
| `str:to_upper` | __str_to_upper__ (src/stdlib/str/operators.cpp) | `str.cml` 的 `to_upper` |
| `test_support:with_probe` | __test_support_with_probe__ (src/stdlib/test_support/operators.cpp) | `test_support.cml` 的 `with_probe` |
| `this:zen` | __this_zen__ (src/stdlib/this/operators.cpp) | `this.cml` 的 `zen` |
| `time:now` | __time_now__ (src/stdlib/time/operators.cpp) | `time.cml` 的 `now` |
| `time:strptime` | __time_strptime__ (src/stdlib/time/operators.cpp) | `time.cml` 的 `strptime` |

补这 121 条可读陈述是另一笔、另一类改动,不在这一笔里:
* 43 条加一个 `#op` 标记就成为可读陈述(不改语义,但那是 43 行只为检查而存在的标记);
* 78 条要在 C++ 入口替它们写一条空故障表,那等于替核心语法形式作一次语义声明,必须单独审。

## 四 逐项对照:变的是哪几行

把 382 行按 URI、裁决(红/相符/不抛)、读法(`panic`/`nopanic`)、类别四栏逐行比:

* 变化的行:47,全是 `RED nopanic` → `GREEN panic`,类别一栏一字不变(同一句话,换了承诺);
* 不变的:335 行,逐字相同;
* 新增/消失的行:0(382 行前后都在);
* `:str/join` 仍是唯一的一红,仍是 `nopanic`。

`kCppPanicStatements` 那 96 项:该文件 `+53/−0`,96 行一个字都没动,读回的逻辑也没动;
检查读到的"表内陈述"由 96 变成 117 = 96 + 21。

一处读数被动到,是因为声明的行号变了:`stdlib.fault.zip_with` 两条 pin 的诊断行在本树上从 369 到 371 ——
`dim` 与 `to_bytes` 各插了一行标记在它上面。两条 pin 一起改,`desc` 里记下这次移动(它本来就记着
366 → 369 这一路)。诊断仍指向 `array.cml` 里 `zip` 那一句,只是那一句下移了两行。

## 五 剩下的那一条红

`:str/join` 的实现有一条 `InvalidArgument`(分隔符为 null),但声明 `with <sep: string>` 的
参数审计已经把那一路排除:类型规则不让一个 null 走到那个分支。所以它是"陈述存在、只是陈述为不抛"
唯一对不上的一个,这一笔不动它 —— 动它就是给一条走不到的路径补一句 `panic`。

## 六 验证

远程(`internal/tools/camel-remote.ps1`,`-Jobs 8`,不跑全量):

* `smoke` + `feat/compile` + `feat/opt` + `feat/modules` + `feat/linear`:
  `total: 7247, pass: 7213, fail: 11, skip: 9, known failures: 14`;
  11 红 = 基线就红的 6 条(`docs.*` 3、`plan_validation`、`opt.writings` 2)+ 本轮 `stdlib.fault.zip_with`
  的 5 条(诊断行那两行 pin),后者已随 pin 改完单跑 `feat/modules` 转绿;
* `gpu-plans` 只报不判,18 红与基线同名(模块租约与 outline 的既有读数),不改这一笔的结论;
* AST 记录:改过的 13 个 `.cml` 重记 13 行;另有 `src/stdlib/prelude.cml` 一行 —— 文件与 HEAD 逐字节相同,
  记录里的 `06d4fd27c3bfa3c5` 与今天算出的 `33d2c5cbb2677f06` 对不上,可见那一行在基线就是旧的,重记时一并对上;
* 本地:`conflict-markers` 0、`stdlib-reference --verify-doc` 通过、`check-doc-links --all` 4 条红
  都在基线同名(`docs/*.md` 指向不在本仓的 `editors/vscode`)。

## 七 提交

* `ops: the 21 core operators that fault state panic at the C++ entrance`
* `lib: 26 declarations that fault state panic; the 121 that do not are counted, not written`
* `docs: the readable-statement reading and the 168 operators, item by item`
