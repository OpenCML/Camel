# `panic` 的写入者：清点、检查与合入清单

本页是一张**合入检查单**，不是缺陷报告。要合入的那一半（`operator_record.h` + `facts.h`，`6489e1ca6`）把 `panic` 收成 `Stated<bool>`，两个入口都还没人写；而「不写」在这一版里被读成**承诺不会故障**。所以合入这一半，等于让库里每个算子同时作一次承诺——这不是重构，是改语义。

## 一、两个入口，今天一边也没有写入者

```
$ git grep -n 'panic' -- 'lib/**/*.cml'                          # 声明侧：零写入者
$ git grep -n 'CppRegistration' -- lib                            # 登记侧：零写入者
```

`exf` 车道在 `391d7b2ce` 里记下的是同一件事：

> 唯一的 `:op/copy` 声明不写 `panic`，且**整棵树没有任何 `panic` 写入者**（`compile/` 与 `CppRegistration` 皆无，返工版也没有），所以今天 `mayFault()` 对任何记录都读出保守的 `true`——对 COPY 与现有 `false` **不同**。

（`nodeMayFault` 今天读的是另一张表，`OperatorTable::traitsOf` 从 `sem::OperatorSemantics` 的故障表推出 `mayFault`；记录里的 `mayFault()` 才是 `!panic.stated() || panic.value`。两条读数今天分家，见第三节。）

## 二、原则怎么说的

`docs/design-principles.cn.md` 第 165 行：

> **算子是否可能故障由它的声明说出**：没有函数体的原生算子用 `panic` 声明它可能故障（3.9），不写即承诺不会故障；仓库内的标准库由检查核对实现里有故障路径的算子都写了 `panic`。

3.9（第 252、253 行）：

> **`panic` 声明一个原生算子可能故障**（负责人 2026-11-22）：写在没有函数体的声明上，位置与 `pure`、`uses` 相同，如 `export func div(a: int, b: int): int pure panic`；不带种类列表（故障与错误一样以 Kind 区分）。不写即承诺不会故障。C++ 登记算子时"是否可能 panic"是必填的一项，与 Camel 声明是同一项信息的两个入口，两边都写了就必须一致。
>
> **故障只从故障源产生**：声明了 `panic` 的算子（除零、越界，以及 `fail` 这个内建算子），和运行时的资源极限（递归过深、内存耗尽）。

同节第 243 行说清了两者不是一回事：

> **错误与故障是两回事。** 只有声明为可能失败的操作（读文件、解析、网络等）才返回错误值。除零、越界、递归过深是故障，不变成错误值，否则每个除法和下标都要处理错误。

所以 `T!`（错误值，内建 `enum`）与 `panic`（可能故障）**不比较、不互推**：算子可以返回 `T!` 而从不故障，也可以结果类型是 `int` 却故障（除零）。返工版 `operator_record.h` 里那句「`T!` 是写在类型里的 panic，完成时比较两者」已按此删除，`Signature::fallible` 保留为「签名写了 `T!`」这一条返回类型事实，注释写明它不蕴含 `panic`。

**后果**：返工合入后，库里每个没写 `panic` 的算子都承诺不会故障。这条承诺会被优化器当真——除法可以挪出守卫，下标可以提前算。所以**写 `panic` 与改读取方必须是同一批**。

## 三、清点：实现里有故障路径的算子

### 怎么量的

`scripts/check-operator-panic.mjs`（本批新增）从 21 张实现映射（`{"op/div_i", __builtin__div__}` 这样 378 个不同 URI）找到每个算子的 kernel，在**同一个翻译单元**里做调用闭包，读实现里的故障点：

- 记故障：`throwRuntimeFault(RuntimeDiag::X)`、`RuntimeFault::make(X)`、`throw RuntimeFault(X)`；
- 不记：`ProcessExit`（`exit` 是程序离开，调度器接住它并设退出码，不是故障）；
- 记「别的 C++ 异常」：其余 `throw`——算子没交出值，`panic` 得盖住；而运行今天根本不把它报成故障；
- 不计：`OperandTypeUnsupported`、`NullSlotUnwrapped`，两个都说编译器生成了类型规则不会接受的调用，是编译器缺陷；单独计数并打印，不藏起来；
- 调用方传了枚举常量、被调方按它 `switch` 时，只走选中的那条臂。少了这一步，共用 `complexCompute` 的 `mul` 会读出 `Div` 臂的除零，一个不会故障的算子会被记成会。

原始 grep 佐证：`lib` 里 224 处 `throwRuntimeFault`（`lib/std/builtin` 61 处），`.cml` 里 41 处 `fail<'…'>`，`DivisionByZero` 44 处、`IndexOutOfRange` 38 处。

### 读数

- **378 个算子**（不是 376：旧裸键把 `find`（re + str）与 `get`（json + state）各折成一对，见「五、两次数错」）；写 `panic` 之前 **92 个**的实现里有故障路径、且没写 `panic`（红），284 个没有，**0 个**声明了实现里没有的故障。
- 算子可达的故障点 348 处，去重后 **103 处**：其他运行时故障 58、别的 C++ 异常 21、除零 13、越界 10、`fail` 1。
- 另有 38 个算子只抛编译器缺陷，不计。

### 要写 `panic` 的 92 个（按文件）——其中 42 个已写，余 50

| 文件 | 算子 | 故障类别 |
|---|---|---|
| `lib/ckpt/src/checkpoint.cpp` | `load_tree`, `save_tree` | 别的 C++ 异常 |
| `lib/fs/operators.cpp` | `read_chunk` | 参数不合法 |
| `lib/io/operators.cpp` | `get_chars` | 参数不合法 |
| `lib/json/operators.cpp` | `encode`, `get` | 参数不合法 |
| `lib/process/operators.cpp` | `sleep` | 参数不合法、运行时故障 |
| `lib/pyplot/operators.cpp` | `plot` | 运行时故障、别的 C++ 异常 |
| `lib/python/operators.cpp` | `attr`, `call`, `eval`, `exec`, `from_py`, `import_module`, `run_file`, `to_py`, `to_string` | 运行时故障、别的 C++ 异常 |
| `lib/re/operators.cpp`、`lib/str/operators.cpp` | `find` | 参数不合法、正则不合法 |
| `lib/rewrite/module.cpp` | `check`, `check_over`, `rewrite`, `run`, `saturate`, `why_not` | 别的 C++ 异常 |
| `lib/sim/operators.cpp` | `asian_call_price`, `pi_hits` | 运行时故障 |
| `lib/state/module.cpp` | `set`, `update` | 参数不合法 |
| `lib/std/builtin/executors/builtin.cpp` | `enum/tag`, `io/eprintln`, `io/print`, `io/println`, `op/assn_div_*`（8 个）, `op/assn_mod_i`, `op/assn_mod_l`, `op/div`, `op/div_*`（8 个）, `op/fail`, `op/idx_arr`, `op/idx_str`, `op/idx_take`, `op/mod`, `op/mod_i`, `op/mod_l`, `op/set_field`, `op/set_index`, `op/set_index_in_place`, `op/slice_bytes`, `op/slice_str`, `op/slice_sub`, `str/format`, `str/join`, `struct/last_arr`, `struct/range`, `struct/zip`, `tangent/*`（7 个） | 除零、越界、`fail`、参数不合法、格式化失败、字段不存在、切片越界、运行时故障 |
| `lib/std/builtin/operators/array.cpp` | `dim`, `max_d`, `max_f`, `max_i`, `max_l`, `min_d`, `min_f`, `min_i`, `min_l`, `to_bytes` | 参数不合法 |
| `lib/str/operators.cpp` | `from_codepoints`, `repeat` | 参数不合法 |
| `lib/test_support/operators.cpp` | `rendezvous`, `thread_count` | 参数不合法、运行时故障 |
| `lib/time/operators.cpp` | `strftime` | 时间格式 |

`min`/`max` 在空数组上是故障（`array_axis.cpp` 的 `refuse`），不是返回 `null`；`to_bytes` 在元素不是平凡数据上是故障。

## 四、检查

```powershell
node scripts/check-operator-panic.mjs          # 报告，不一致 exit 1
node scripts/check-operator-panic.mjs --list   # 每个算子的读数，不只是红的
```

**今天的读数：exit 1，红 92 行。红是预期的**——`panic` 还没写。这条检查不许为了绿而放松：它找不到某个算子的实现时报 exit 2（拒答），不当作「没抛异常」。

它现在**不进 `scripts/release-gate.mjs`**：一道按构造为红的检查进了闸门，只会挡住与它无关的发布。写 `panic` 的那一批把它加进闸门，判据是同一次提交里 exit 0。

它看不见的东西写在脚本头里：跨文件的 helper 不跟（名字不唯一，跟下去会让共用工具函数的算子全部变红）。这是会**漏**故障的方向，所以矩阵里只有 kernel 与它同一个文件里的 helper；补这一块要真的前端，不是正则。

## 五、合入检查单

`operator_record.h` + `facts.h` 合入时，**同一批**必须做完：

1. 给第三节表里每个算子写 `panic`：Camel 侧写在 `export func` 行上（与 `pure`、`uses` 同位置），C++ 侧 `CppRegistration` 第二个参数填 `true`。
2. 表外的 286 个（378 − 92）**不写** `panic`（不写 = `false`）。C++ 侧必填，填 `false`——包括 `lib/std/builtin/executors/builtin.cpp:747` 现在 `registry.set(":op/copy", {.mayFault = false})` 的那一处，`copy` 不故障。
3. 把 `kernel/runtime/graph.cpp:79-95` 每臂改成读 `camel::ops::Registry::instance().find(oper)->mayFault()`，**并出一张表：改前改后每个算子的 `mayFault` 取值**。改前是今天的 `nodeMayFault`（`copy` 按形状已是 `false`；`Oper` 读 `OperatorSemantics` 的故障表，没人声明的按 `true`），改后是记录的 `mayFault()`。**必须一项不变**；变了的逐项说明。最可能的变法是今天没声明故障表、被默认读成 `true` 的算子——它们正是第 2 条要盯的。
4. `node scripts/check-operator-panic.mjs` exit 0。
5. 跑 `feat/compile`、`feat/runtime`、`feat/opt`、`feat/consistency`，四次 `[SUMMARY]` 的 `fail` 全为 0。

## 六、本批不做什么

`lib/**/*.cml` 一个 `panic` 也没动（那是单独一批，等清单点了再做）。本批只有三样东西：`operator_record.h` 的两处注释、这一页、`scripts/check-operator-panic.mjs`。

所以**还欠 50 个算子的 `panic`**（92 − 42 已写），检查今天红 50 行，红的原因只是它们还没写；那 47 个纯 C++ 的写上也没用，因为 `CppRegistration` 到 `Registry` 没有路：`Registry` 的 `instance`/`intern`/`find`/`declare`/`registerOperator`/`complete`/`epoch` 与 `Record::effect`/`external` 全树只有声明、零定义，填进登记处也到不了任何读数。

## 七、一句话

**一个「必填」，只有在有人真去填的时候才有约束力。** 今天两侧都空着，读取方又对空取值取了保守默认——于是「必填」这件事在库代码里一次也没有发生过。返工把 `panic` 收成 `Stated<bool>` 是对的，但它要的是一次真实写入：不是给 `copy` 填一个 `false` 就算完，而是把**每个实现里有故障路径的算子**都写出来，让「没写」重新等于「不会故障」。