# 红基线：`closurediag`（`81fa0bac9`）起

## 这是一份什么文件

`81fa0bac9` 把 3.5 的规则从「计划」变成三条编译错误，**主干因此多出 128 条红**。这 128 条不是回归，
是这一步的账；但**账不列出来，下一笔的真红就会淹在里面**。

所以每一道闸门报告都多一行：

```
新红 vs 基线 = N      （N = 本次红集 − 本文件的 128 条）
```

**判据**：`N > 0` 就是新红，**必须当场解释或停手**；`N = 0` 才允许说「没有新红」。**没有基线，红集不是读数，是噪声。**

## 计数规则：按「程序 + 断言」，不按后端行数

一个程序在十几个后端/优化组合下各算一条是**实现细节的倍数**，不是覆盖的倍数。下表按程序列，
「条数」是该程序在后端矩阵里的形态数。**128 = 下表条数之和**，可直接用来做减法。

## 三类，以及各自等哪一步

| 类 | 含义 | 等哪一步 |
|---|---|---|
| **甲** | 靠**已退役语义**才合法的载体：闭包靠共享 cell 写外层名字 | 第 3 步（17 组迁移）改写成 `Ref` 或改成 `err.*` |
| **乙** | `var_closure` 的 `transitional.*`：明确标注为「迁移期接受」的条目 | 第 3 步随迁移**删除** |
| **丙** | `state_escape` 的 5 条：载体按问答页默认改 | 第 4 步（删按引用捕获口）收口 |

## 清单

### `feat.compile.effects_syntax` — 16 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `effect_through_parameter` | 1 | 闭包写经形参交来的 `var`；第 3 步改成写 `Ref` |
| `err_pure_parameter_write` | 1 | 同上，`pure` 形参；第 3 步随迁移改写 |
| `err_closure_capture_pure` | 1 | 闭包捕获后写；第 3 步改 `Ref` |
| `ok_capture_local_var` | 1 | **`ok_` 前缀在新规则下已错**；第 3 步改成 `err.*` |
| `ok_capture_local_var_as_pure_value` | 1 | 同上 |
| `err_capture_escapes` | 1 | 闭包写捕获；第 3 步改 `Ref` |
| `callback_write_through_parameter` | 4 | 四后端；回调经形参写调用方的变量，第 3 步改 `Ref` |
| `callback_write_through_parameter_opt` | 4 | 同上，多一遍 `std::opt` |
| `callback_writer_table_through_callee` | 2 | 写者表经被调函数写捕获；第 3 步改 `Ref` |

### `feat.compile.function_values` — 3 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `captured_var_in_two_arms` | 3（nvm/fvm/jit） | 同一捕获在两个臂里写；第 3 步改成循环/分支状态或 `Ref` |

### `feat.compile.nullable_assign` — 3 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `nullable_assign.enters` | 3（nvm/fvm/jit） | 可空赋值经闭包进入；第 3 步改写 |

### `feat.compile.state_escape` — 5 条（丙）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `escape_return` | 1 | 转义经返回值；第 4 步删按引用捕获口后收口 |
| `escape_field` | 1 | 经字段转义；同上 |
| `escape_array` | 1 | 经数组转义；同上 |
| `escape_stored_by_callee` | 1 | 被调函数存下；同上 |
| `escape_into_closure` | 1 | 存进闭包；同上 |

### `feat.compile.var_closure` — 3 条（乙，第 3 步删除）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `transitional.by_reference_capture_write_is_accepted` | 1 | 迁移期接受的按引用写；迁移落地时删 |
| `transitional.write_after_the_closure_is_accepted` | 1 | 同上：闭包后写仍被接受 |
| `transitional.program_closure_still_seen_where_the_rule_would_ask` | 1 | 同上：程序自己的闭包与生成器体的区分 |

### `feat.linear.captured_writes` — 24 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `captured_write_order` | 17 | 捕获写的顺序；第 3 步改成 `Ref` 后顺序由句柄给 |
| `lambda_capture_write` | 4 | 闭包写捕获；第 3 步改写 |
| `lambda_capture_ignored` | 3 | 写被忽略的捕获；第 3 步改写（它与 `IgnoredSideEffect` 的剩余一支同源） |

### `feat.linear.recursion_and_calls` — 1 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `fib_nested_sync` | 1（nvm） | 嵌套 `sync` 里写捕获；第 3 步改写 |

### `feat.linear.toplevel_var` — 6 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `argument_order` | 6 | 实参顺序与捕获写交错；第 3 步改写 |

### `feat.linear.var_reference` — 67 条（甲）

| 程序 | 条数 | 一条「它等什么」 |
|---|---|---|
| `var_capture` | 33 | 后端 × 优化组合；第 3 步改成 `Ref` 后由这一族重跑 |
| `var_tensor` | 33 | 同上（张量形状下的同一属性） |
| `var_alias_captured` | 1 | 别名捕获；第 3 步改写 |

## 合计

```
effects_syntax 16 + function_values 3 + nullable_assign 3 + state_escape 5 + var_closure 3
+ captured_writes 24 + recursion_and_calls 1 + toplevel_var 6 + var_reference 67  = 128
甲 120 / 乙 3 / 丙 5
```

## 谁按这份文件干活

第 3 步的迁移车道：**甲类 120 条逐条改成 `Ref` 或改正前缀；乙类 3 条删除**；丙类 5 条等第 4 步。
每改完一条，**本文件对应行删除**，`新红 vs 基线` 的分母随之下降——**基线只能缩，不能靠「再解释一遍」变大。**