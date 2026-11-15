# 红基线：`closurediag`（`81fa0bac9`）起

## 这是一份什么文件

`81fa0bac9` 把 3.5 的规则从「计划」变成三条编译错误，**主干因此多出 174 条红**。这 174 条不是回归，
是这一步的账；但**账不列出来，下一笔的真红就会淹在里面**。

所以每一道闸门报告都多一行：

```
新红 vs 基线 = N      （N = 本次红集 − 本文件的 174 条）
```

**判据**：`N > 0` 就是新红，**必须当场解释或停手**；`N = 0` 才允许说「没有新红」。**没有基线，红集不是读数，是噪声。**

## 兑现记录（只增不删：基线不是删行，是记下它已经兑现了）

| 日期 | 谁 | 兑现了什么 | 读数 |
|---|---|---|---|
| 2026-11-16 | `lane-byref`（`24ffae0d6`） | **`feat.linear` 那四族 98 行**（`var_reference` 67、`captured_writes` 24、`toplevel_var` 6、`recursion_and_calls` 1） | **`feat.linear` 850 / 846 / fail 0**（882−32=850，走掉的是重复不是读数） |
| 2026-11-16 | `lane-contextdiag` | **`feat.consistency.partial_application` 那两族 8 条**（`var_parameter` 4、`var_parameter_effect` 4）：拒绝从闭包那条规则的「下一张图」移到绑定点，消息里的 `'$tagged0'` 换成程序写的名字 | `feat/opt`+`feat/consistency` 合跑 **2743 / 2664 / fail 79 → 2723 / 2668 / fail 55**；走掉的 24 行是本表的 8 条加 `feat.opt.splice_place` 的 16 条（那 16 条不在本账内）。4+4 条行为断言各换成 1 条拒绝断言（`VarContextParameterCannotBeBound`），另加 1 条模块级单元的新用例 |

**同一批量到的两行**（`feat.mm`，`lane-byref` 说「可能也绿了，我没跑」——**现在跑了**）：
`poison:linear.captured_writes:captured_write_order.default` 与
`poison:linear.var_reference:var_tensor.default`，在合并 tip 上**均已转绿**，因为它们的底层程序已经改了。
`poison:modules.resources:core` 仍红，**是既有红，不在基线内**。

**基线剩余**：`174 − 98 − 8 = 68`（`feat.compile` 30、`feat.consistency` 28、`feat.trans` 5、
`feat.docs_links` 1、`feat.mm` 4），四舍五入到下表逐行。

## 计数规则：按「程序 + 断言」，不按后端行数

一个程序在十几个后端/优化组合下各算一条是**实现细节的倍数**，不是覆盖的倍数。下表按程序列，
「条数」是该程序在后端矩阵里的形态数。**174 = 下表条数之和**，可直接用来做减法。

## 三类，以及各自等哪一步

| 类 | 含义 | 等哪一步 |
|---|---|---|
| **甲** | 靠**已退役语义**才合法的载体：闭包靠共享 cell 写外层名字 | 第 3 步改写成 `Ref`（或按作者裁定改成新诊断的期望） |
| **乙** | `var_closure` 的 `transitional.*`：明确标注为「迁移期接受」的条目 | 第 3 步随迁移**删除** |
| **丙** | — | **裁定后已无丙类**（见下） |

### 裁定 2026-11-16：`state_escape` 那 5 条不是「优先级问题」，是「旧规则已撤回」

作者原文：**「`ClosureCaptureEscapes` 已经不是有效规则。」** 它是 2026-11-14 规则二「离开的闭包不能按引用
捕获 `var`」的检查；11-15 定案「闭包捕获的是值」，**规则二整条撤回**（3.5 写着「取代 2026-11-14 的规则二」）。
**闭包捕获的是值，带着值离开作用域没有任何问题。**

所以 `state_escape` 的 5 条按第 3 步的第 2 件办：**期望改成 D1（`ClosureAssignsCapturedName`）**，
而不是「改载体」。

## 第 3 步的五件事（第五件是裁定追加的）

1. 甲类 120 条逐条改成 `Ref`（或改正 `ok_` 前缀）；
2. `state_escape` 的 5 条**期望改成 D1**；
3. 另加 5 条对照：同样的离开方式（返回 / 放进字段 / 放进数组 / 交给会存它的函数 / 放进另一个闭包），
   **闭包只读**捕获的 `var` ——合法、能跑、读到的是闭包创建时的值；
4. 乙类 3 条删除；
5. **删 `SemanticDiag::ClosureCaptureEscapes`**、`builder_state.cpp` 里携出的判定（`carryOutOf` / `carriedOffAt`
   一族）、以及它在 `docs/diagnostics.md` 的条目。

## 清单

### `feat.compile.effects_syntax` — 16 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `effect_through_parameter` | 1 | 闭包写经形参交来的 `var`；第 3 步改成写 `Ref` |
| `err_pure_parameter_write` | 1 | 同上，`pure` 形参 |
| `err_closure_capture_pure` | 1 | 闭包捕获后写 |
| `ok_capture_local_var` | 1 | **`ok_` 前缀在新规则下已错**，改成 `err.*` |
| `ok_capture_local_var_as_pure_value` | 1 | 同上 |
| `err_capture_escapes` | 1 | 闭包写捕获 |
| `callback_write_through_parameter` | 4 | 四后端；回调经形参写调用方的变量 |
| `callback_write_through_parameter_opt` | 4 | 同上，多一遍 `std::opt` |
| `callback_writer_table_through_callee` | 2 | 写者表经被调函数写捕获 |

### `feat.compile.function_values` — 3 条（甲）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `captured_var_in_two_arms` | 3（nvm/fvm/jit） | 同一捕获在两个臂里写；改成分支状态或 `Ref` |

### `feat.compile.nullable_assign` — 3 条（甲）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `nullable_assign.enters` | 3（nvm/fvm/jit） | 可空赋值经闭包进入 |

### `feat.compile.state_escape` — 5 条（甲，**期望改成 D1**）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `escape_return` | 1 | 期望改成 `ClosureAssignsCapturedName` |
| `escape_field` | 1 | 同上 |
| `escape_array` | 1 | 同上 |
| `escape_stored_by_callee` | 1 | 同上 |
| `escape_into_closure` | 1 | 同上 |

### `feat.compile.var_closure` — 3 条（乙，第 3 步删除）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `transitional.by_reference_capture_write_is_accepted` | 1 | 迁移期接受的按引用写 |
| `transitional.write_after_the_closure_is_accepted` | 1 | 闭包后写仍被接受 |
| `transitional.program_closure_still_seen_where_the_rule_would_ask` | 1 | 程序自己的闭包与生成器体的区分 |

### `feat.linear.captured_writes` — 24 条（甲）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `captured_write_order` | 17 | 捕获写的顺序；改成 `Ref` 后顺序由句柄给 |
| `lambda_capture_write` | 4 | 闭包写捕获 |
| `lambda_capture_ignored` | 3 | 写被忽略的捕获 |

### `feat.linear.recursion_and_calls` — 1 条（甲）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `fib_nested_sync` | 1（nvm） | 嵌套 `sync` 里写捕获 |

### `feat.linear.toplevel_var` — 6 条（甲）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `argument_order` | 6 | 实参顺序与捕获写交错 |

### `feat.linear.var_reference` — 67 条（甲）

| 程序 | 条数 | 等什么 |
|---|---|---|
| `var_capture` | 33 | 后端 × 优化组合 |
| `var_tensor` | 33 | 同上 |
| `var_alias_captured` | 1 | 别名捕获 |

### 本轮闸门新见、车道未跑的四族 —— 46 条（甲）

**这一族的红与上面同因**：闸门在 `feat/consistency`、`feat/trans`、`feat/docs_links`、`feat/mm` 里选到了
同一程序的其它形态。**车道只量了 `feat/compile` 与 `feat/linear`，规则的作用面比它量的宽**——这是「窄范围
是最常见缺陷」的又一例，所以它们进基线，而不是留着当「新红」。

| 计划 | 程序 | 条数 | 等什么 |
|---|---|---|---|
| `feat.consistency.partial_application` | `var_parameter` | 4 | 闭包写经形参交来的 `var` |
| `feat.consistency.partial_application` | `var_parameter_effect` | 4 | 同上 |
| `feat.consistency.new_values` | `rebinding_loop` | 14 | 循环里重新绑定的名字被闭包写 |
| `feat.consistency.new_values` | `peek_loop` | 5 | 同上（`peek` 形态） |
| `feat.consistency.new_values` | `sort_rebinding` | 9 | 同上（排序原语形态） |
| `feat.trans.cpp` | `agree_with_fvm` | 1 | 175 个程序里 2 个因新诊断被拒，读数随之变 |
| `feat.trans.cpp` | `every_expect_program_links_no_runtime` | 1 | 同上 |
| `feat.trans.cml` | `print_then_run_agrees_with_the_origin` | 1 | `effects/writer_table_through_callee` 被 `std::cml` 拒 |
| `feat.trans.cml` | `print_then_run_negative_swap_is_seen` | 1 | 同上 |
| `feat.trans.print_determinism` | `gir_prints_one_hash` | 1 | 被拒程序的 GIR 读数 |
| `feat.docs_links` | `docs.snippets_compile` | 1 | **文档示例本身在写捕获的 `var`**（`04_execution_model.md:192/212`、`tutorial.cn.md:518`），第 3 步随文档改 |
| `feat.mm.capacity` | `capacity.perm_query_types` | 1 | 闭包写捕获 |
| `feat.mm.gc_poison_sweep` | `poison:compile.nullable_assign:…`、`poison:linear.captured_writes:…`、`poison:linear.var_reference:…` | 3 | 上面三条的 poison 形态，**同一程序的第二行**，不是新缺陷 |

## 不计入基线的既有红（8 条）

这些**不是**本次新增，报闸门时用「未计入」的四种说法标：

| 计划 | 用例 | 说法 |
|---|---|---|
| `feat.parse.frontend` | `frontend.ast_snapshot` | ① 已修复（同一批的尾巴笔记录 3 个新 `.cml` 的哈希） |
| `feat.consistency.runner` | `runner.backend_matrix` | ② 既有（负载相关） |
| `feat.trans.cml` | `print_and_recompile` | ② 既有 |
| `feat.consistency.new_values` | `new_values.macro_append.{nvm,fvm,macro}` ×3 | ② 既有 |
| `feat.docs_links` | `docs.links.citations` | ② 既有（`plan.cn.md:51` 的旧账） |
| `feat.mm.gc_poison_sweep` | `poison:modules.resources:core` | ② 既有 |

## 合计

```
compile 30 + linear 98                                  = 128
consistency(new_values 28 + partial_application 8) 36 + trans 5 + docs 1 + mm 4  =  46
                                                    基线总计 = 174
乙 3 / 甲 171（其中 state_escape 5 改期望、13 条是 poison 与 cpp 的第二形态）
不计入基线 8  →  闸门红集 182 = 174 + 8
```

## 谁按这份文件干活

第 3 步的迁移车道按上表干活：**甲类逐条改 `Ref` 或改期望，乙类删除，第 5 件删 `ClosureCaptureEscapes`**。
每改完一条，**本文件对应行删除**，`新红 vs 基线` 的分母随之下降——**基线只能缩，不能靠「再解释一遍」变大。**