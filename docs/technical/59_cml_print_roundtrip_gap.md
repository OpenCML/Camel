# `std::cml` 往返缺口：`not_equivalent` 的分类与真实原因

`std::cml` 把当前的 `GCGraph` 打印回 Camel 源码，权威判据是 `test/tools/cml_roundtrip.py`（由
`test/tools/cml-sweep.mjs` 驱动）：打印出的源码重新编译成 v2 规范文档，与原程序的文档逐顶点比较。
判据本身未被放宽，本文只记录打印器（`src/passes/trans/cml/cml.cpp`）的缺口。

## 读数

主机 `out/latest`，`node test/tools/cml-sweep.mjs --jobs 8`，语料 3688 个文件：

| 版本 | files | printed | refused | frontend | checked | equivalent | not_equivalent | over_budget | unfaithful |
|------|-------|---------|---------|----------|---------|------------|----------------|-------------|------------|
| `97f7681b7`（基线） | 3688 | 738 | 1910 | 1040 | 731 | 682 | 49 | 0 | 7 |
| 打印器修复后（本页所在提交） | 3688 | 738 | 1910 | 1040 | 738 | 688 | 50 | 0 | 0 |

接口作者给的「51 条不等价」是历史读数（`docs/design/graph-store/plan-roundtrip.cn.md`，语料 3379 个文件、
`f7cff9227`：`equivalent=649 not_equivalent=51 unfaithful=7`）。语料增长到 3688 之后，同一读数在
`97f7681b7` 上是 49；本提交后是 50：`unfaithful` 的 7 条里有 6 条被修好、1 条
（`consistency/if_without_else/statement.cml`）从「打印出的源码根本不是文档」变成「文档不等价」，
所以它从 `unfaithful` 移到了 `not_equivalent`。下表按本提交后的 50 条分类。

## 判据怎么说话

`cml_roundtrip.py` 在不等价时给出差别的第一处：通常是

```
a structural class has N vertex/vertices on the left and 0 on the right
left:  <某个顶点>
```

`左` = 原程序的文档，`右` = 打印程序重新编译出的文档。也就是说：**打印出的程序少了某个顶点**
（一个类型、一个字面值、一次调用、一个端口、一个区域……）。下面的类别就按这个「少掉的顶点」
归类，每类给一条在代表文件上核实过的真实原因。

## 分类表（50 条）

| # | 类别（少掉的顶点） | 条数 | 真实原因（代表文件 + 症状） |
|---|-------------------|------|------------------------------|
| 1 | 类型顶点 `T` | 11 | 前端把「转成自己的类型」的 `as` 折叠掉了，图上没有这个转换，打印器只能写值：`compile/casts/same_type.cml` 的 `let r = s as S` 打印成 `let r: S = s`、`println('x' as string)` 打印成 `println('x')`，于是原文档里的 `bool` / `string` / `f32` 类型顶点在右边没有对应 |
| 2 | `DATA` 顶点 | 12 | 打印器用一段更短、语义相同的文本代替了原程序里的一个字面值/常量节点，右边就不再有那个 `DATA`：`compile/constas/constas_column.cml` 的 `column("w", a, b)` 是按 `constas` 展开后的图打印成 `(a.w, b.w)`（声明本身也随实例化消失），原文档里的结构体常量 `{w: 3, h: 4}` 在右边没有对应 |
| 3 | `OPER` 顶点 | 12 | 编译期展开（`static ... for j in indices t`、静态 `acc` 循环）在图上已经是展开后的算子链，打印器只能照图写：`compile/static_expansion/indices_graph.cml` 的 `static for j in indices t => j` 打印成 `(0, 1)`、`static loop acc = 0 for j in indices t do acc + t.[j]` 打印成 `0 + t.0 + t.1 + t.2`，`indices` 那一次调用（以及它的 `SACC`/`SFOR` 家族）在右边没有对应 |
| 4 | `PORT` 顶点 | 6 | 区域/闭包在图上捕获的是一个端口，打印器把「读到的值」直接写成了字面值：`loops/captures.cml` 的 `var bias = 100` 被循环体读，打印成 `s + x + 100`，循环区域的那个捕获端口在右边没有对应 |
| 5 | `CALL` 顶点（`callee`，函数值） | 3 | 一个函数值被绑到名字上，打印器只写它的调用形态：`compile/surface/dropped_function_name.cml` 的 `let step = len` + `step([10, 20, 30])` 打印成 `return len([10, 20, 30])`，原文档里那个函数值的 `DATA`/`CALL` 顶点在右边没有对应（`void_tail_plain` 因此打印成空体） |
| 6 | `JOIN` 顶点 | 1 | 函数体的 `return` 落在 `loop`/分支上时被当成语句打印，`return` 丢了：`compile/struct_join/join_arms.cml` 的 `func sum(ps: Point[]): int { return loop acc = 0 for p in ps do acc + p.x + p.y }` 打印成没有 `return` 的 `loop`，函数值那个 `JOIN` 在右边没有对应 |
| 7 | `BRCH` 顶点 | 1 | 值被丢弃的臂整条打印不出来：`consistency/if_without_else/statement.cml` 的 `if x > 100 then 'huge'` 在打印文本里消失（臂的值没人要，语句也就没写），右边的分支顶点跟着消失 |
| 8 | `FILL` 顶点 | 1 | 结构体做循环状态时被拆成字段元组：`loops/struct_state.cml` 的 `loop p = { x: 0, y: 1 } ... do { x: p.x + d, y: p.y * 2 }` 打印成 `loop cml2 = cml1.x, cml3 = cml1.y ... do (cml2 + d, cml3 * 2)` 再 `return { x: cml4.0, y: cml4.1 }`，循环里那个结构体构造的 `FILL` 在右边没有对应 |
| 9 | `R` 区域顶点（`arm`） | 1 | 空臂被写成内联的 `()`，臂区域本身没有对应：`opt/progen/findings/inline_dce_arm_effect_lost.cml` 的 `if false then sync { null; }` 打印成 `if false then { () }`，原文档 arm 区域里的顶点在右边没有对应 |
| 10 | 判据自己崩了 | 2 | `cml_roundtrip.py` 抛 Python 异常（不是文档不等价）：`feat/mm/writings/chains.cml`、`modules/rules/bench_target.cml` 走的是同一族形状，判据在配对一个顶点时抛 `Traceback`；判据不在本路可改范围，缺口记在这里 |

合计 11 + 12 + 12 + 6 + 3 + 1 + 1 + 1 + 1 + 2 = 50。

## 文件清单

1. 类型顶点（11）：`compile/array_shape/let_named_dimension`、`compile/casts/same_type`、
   `compile/equality/null_test_never_null`、`compile/rest_params/lambda_rest_forward`、
   `compile/rest_params/pack_expand`、`compile/surface/dropped_value_statement`、
   `compile/type_packs/rest_function`、`consistency/if_without_else/return_null_in_arm`、
   `linear/byref/lambda_capture_write`、`opt/progen/findings/inline_untaken_arm_effect`、
   `opt/progen/findings/simplify_arm_write_lost`。
2. `DATA`（12）：`compile/constas/constas_column`、`compile/return_type/accepted`、
   `compile/unused_values/loop_statements_used`、`linear/byref/lambda_capture_ignored`、
   `linear/runtime/order_edges_regions`、`linear/runtime/var_read_before_region`、
   `loops/hof_block_alloc`、`opt/progen/findings/inline_statement_effect_lost`、
   `opt/progen/findings/inline_wrong_arm_runs`、`opt/progen/findings/opt_lambda_effect_lost`、
   `trans/cpp/exec/composites`、`trans/cpp/fn_result_dropped`。
3. `OPER`（12）：`compile/comprehension_context/annotated`、`compile/lambda_inference/positions`、
   `compile/native/pyplot_declaration_import`、`compile/rest_params/rest_params`、
   `compile/static_expansion/indices_graph`、`compile/static_expansion/static_loop_sequence`、
   `compile/static_expansion/type_position_empty_tuple`、`compile/surface/if_without_else_ok`、
   `consistency/if_without_else/older_effect_place`、`linear/match/let_multi_binding`、
   `opt/generic/specialization_survives_the_round`、`opt/progen/findings/inline_dce_arm_value`。
4. `PORT`（6）：`compile/literals/comprehension_in_position`、
   `compile/static_expansion/static_tuple_function`、`compile/static_expansion/static_tuple_indices`、
   `format/indices`、`loops/captures`、`opt/adb67e081/rc00_seed27`。
5. `CALL`（3）：`compile/surface/dropped_function_name`、`linear/runtime/var_between_statements`、
   `opt/progen/findings/inline_fvm_arm_effect_lost`。
6. `JOIN`（1）：`compile/struct_join/join_arms`。
7. `BRCH`（1）：`consistency/if_without_else/statement`。
8. `FILL`（1）：`loops/struct_state`。
9. 区域 `arm`（1）：`opt/progen/findings/inline_dce_arm_effect_lost`。
10. 判据异常（2）：`feat/mm/writings/chains`、`modules/rules/bench_target`。

## 一句话结论

50 条里 48 条是「打印文本比原程序短、少了一个顶点」——类型拼写、字面值、编译期展开后的调用、
捕获端口、函数值、`return`、被丢的臂、结构体状态、空臂区域；只有 2 条是判据自己抛异常。
前九类都可以在打印器一侧继续收窄，第 10 类要等判据的配对逻辑（不在本路范围内）。