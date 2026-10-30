# 内置算子声明为“永不出故障”的每一行，核对实现之后

算子的语义记录可以带一张 `faults` 表（`include/camel/core/operator_semantics.h#FaultSpec`）。
这张表把“没有出错路径”写成空表（`[]`），把“可能出错”写成不写（`nullopt`，看 trait）
或写成具体的条目。这棵树里，内置标量算子的空表有两个写法，`OperatorTable::set` 把两者并成同一条事实：

- **trait**：`src/builtin/executors/builtin.cpp#kNeverFaults` 里的条目（`mayFault = false`），
  `src/core/operator_table.cpp#OperatorTable::set` 把它记成空表；
- **记录**：`src/builtin/executors/builtin_semantics.cpp#declareBuiltinOperatorSemantics` 传给
  `declare` 的 `none`，也就是语义记录里的空表。

表里“声明”一栏用四个写法：`trait`（只在 `kNeverFaults` 里）、`记录`（只写在 `declare` 的
`none` 里）、`trait＋记录`（两处都写）、`trait→记录`（只写在 trait 里，记录靠
`declareSemantics` 的合并拿到同一张空表）。

下面一行一个算子：它由哪个函数实现（`文件#符号`），那个函数里的出错路径（没有就写没有、
并说明为什么没有），这张表现在是哪种写法，以及钉住它的用例名。每一行都是读实现读出来的，
不是从算子的名字推出来的；实现里没有出错路径的，就留空表加这一行说明——空表加说明也是交付。

77 行是这棵树现在声明“永不出故障”的全部内置算子：73 条来自 `kNeverFaults`，4 条（复数的
`+` 和 `-`）只写在语义记录里。77 行全部核对过，没有一行与它的实现不符，所以这一笔没有改任何
一条声明。核对里唯一看上去不一致的一处，查下去是同一件事：

- `mul_i`、`mul_l`、`mul_f`、`mul_d`、`neg_i`、`neg_l`、`neg_f`、`neg_d` 在记录里传的是
  `std::nullopt`，而 `add`/`sub`/比较传的是 `none`，于是看起来“`+ - *` 与取负里只有 `+` 和
  `-` 写了空表”。其实两处写法在记录里是同一件事：
  `src/core/operator_table.cpp#OperatorTable::declareSemantics` 把旧记录的事实并进新记录，
  `if (!semantics.faults && old->faults) semantics.faults = old->faults;`，而这 8 个算子的
  trait 已经是 never-faults（`kNeverFaults`），旧记录里就是空表，于是记录照样读出 `[]`。
  我把它改成 `none` 再删回 `std::nullopt`，`camel inspect --ops` 的输出一字不差，
  `declared_records` 两次都绿——没有可观察的差别，就不该改，所以这一笔没改，只把这件事
  写进了 `builtin_semantics.cpp` 那段注释里。
- `kNeverFaults` 上面那段注释说的已经是同一件事（“实现里根本没有出错路径”），逐个算子核对
  之后成立；注释里补上了这份清单的位置，读代码的人不用再猜哪一部分核对过。

| 算子 | 实现 | 实现里的出错路径 | 声明 | 用例 |
|---|---|---|---|---|
| `:op/add_i` | `src/builtin/operators/ops.cpp#__builtin__add_ii__` | 整数加法按二补数回卷：溢出回卷，实现里既没有检查也没有别的抛点 | trait＋记录 | `declared_records`、`declared_traits` |
| `:op/add_l` | `src/builtin/operators/ops.cpp#__builtin__add_ll__` | 整数加法按二补数回卷：溢出回卷，实现里既没有检查也没有别的抛点 | trait＋记录 | `declared_records` |
| `:op/add_f` | `src/builtin/operators/ops.cpp#__builtin__add_ff__` | 一次正确舍入的 IEEE 运算：溢出得 ±inf、下溢得零或次正规数，都不报错 | trait＋记录 | `declared_records` |
| `:op/add_d` | `src/builtin/operators/ops.cpp#__builtin__add_dd__` | 一次正确舍入的 IEEE 运算：溢出得 ±inf、下溢得零或次正规数，都不报错 | trait＋记录 | `declared_records` |
| `:op/sub_i` | `src/builtin/operators/ops.cpp#__builtin__sub_ii__` | 整数减法按二补数回卷：溢出回卷，实现里既没有检查也没有别的抛点 | trait＋记录 | `declared_records`、`declared_traits` |
| `:op/sub_l` | `src/builtin/operators/ops.cpp#__builtin__sub_ll__` | 整数减法按二补数回卷：溢出回卷，实现里既没有检查也没有别的抛点 | trait＋记录 | `declared_records` |
| `:op/sub_f` | `src/builtin/operators/ops.cpp#__builtin__sub_ff__` | 一次正确舍入的 IEEE 运算：溢出得 ±inf、下溢得零或次正规数，都不报错 | trait＋记录 | `declared_records` |
| `:op/sub_d` | `src/builtin/operators/ops.cpp#__builtin__sub_dd__` | 一次正确舍入的 IEEE 运算：溢出得 ±inf、下溢得零或次正规数，都不报错 | trait＋记录 | `declared_records` |
| `:op/mul_i` | `src/builtin/operators/ops.cpp#__builtin__mul__` | 按二补数相乘、回卷；`src/builtin/operators/ops.cpp#__builtin__mul__` 里唯一的抛点是操作数类型分派的 `default`（`OperandTypeUnsupported`），而 URI 写定的类型与实参类型由类型检查保证一致，那一支不可达 | trait→记录 | `declared_records`、`declared_traits` |
| `:op/mul_l` | `src/builtin/operators/ops.cpp#__builtin__mul__` | 按二补数相乘、回卷；`src/builtin/operators/ops.cpp#__builtin__mul__` 里唯一的抛点是操作数类型分派的 `default`（`OperandTypeUnsupported`），而 URI 写定的类型与实参类型由类型检查保证一致，那一支不可达 | trait→记录 | `declared_records` |
| `:op/mul_f` | `src/builtin/operators/ops.cpp#__builtin__mul__` | 一次正确舍入的乘法；`src/builtin/operators/ops.cpp#__builtin__mul__` 里唯一的抛点同上，是类型分派的 `default`，对写定类型的 URI 不可达 | trait→记录 | `declared_records`、`declared_traits` |
| `:op/mul_d` | `src/builtin/operators/ops.cpp#__builtin__mul__` | 一次正确舍入的乘法；`src/builtin/operators/ops.cpp#__builtin__mul__` 里唯一的抛点同上，是类型分派的 `default`，对写定类型的 URI 不可达 | trait→记录 | `declared_records` |
| `:op/neg_i` | `src/builtin/operators/ops.cpp#__builtin__neg__` | 翻转符号，整数按二补数回卷；`src/builtin/operators/ops.cpp#__builtin__neg__` 里唯一的抛点是类型分派的 `default`，对写定类型的 URI 不可达 | trait→记录 | `declared_records`、`declared_traits` |
| `:op/neg_l` | `src/builtin/operators/ops.cpp#__builtin__neg__` | 翻转符号，整数按二补数回卷；`src/builtin/operators/ops.cpp#__builtin__neg__` 里唯一的抛点是类型分派的 `default`，对写定类型的 URI 不可达 | trait→记录 | `declared_records` |
| `:op/neg_f` | `src/builtin/operators/ops.cpp#__builtin__neg__` | 翻转符号，精确；`src/builtin/operators/ops.cpp#__builtin__neg__` 里唯一的抛点是类型分派的 `default`，对写定类型的 URI 不可达 | trait→记录 | `declared_records`、`declared_traits` |
| `:op/neg_d` | `src/builtin/operators/ops.cpp#__builtin__neg__` | 翻转符号，精确；`src/builtin/operators/ops.cpp#__builtin__neg__` 里唯一的抛点是类型分派的 `default`，对写定类型的 URI 不可达 | trait→记录 | `declared_records` |
| `:op/lt_i` | `src/builtin/operators/ops.h#__builtin__lt__<Int32>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records`、`declared_traits` |
| `:op/le_i` | `src/builtin/operators/ops.h#__builtin__le__<Int32>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/gt_i` | `src/builtin/operators/ops.h#__builtin__gt__<Int32>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/ge_i` | `src/builtin/operators/ops.h#__builtin__ge__<Int32>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/lt_l` | `src/builtin/operators/ops.h#__builtin__lt__<Int64>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/le_l` | `src/builtin/operators/ops.h#__builtin__le__<Int64>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/gt_l` | `src/builtin/operators/ops.h#__builtin__gt__<Int64>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/ge_l` | `src/builtin/operators/ops.h#__builtin__ge__<Int64>` | 比较两个值的大小，只读两个槽 | trait＋记录 | `declared_records` |
| `:op/lt_f` | `src/builtin/operators/ops.h#__builtin__lt__<Float32>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/le_f` | `src/builtin/operators/ops.h#__builtin__le__<Float32>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/gt_f` | `src/builtin/operators/ops.h#__builtin__gt__<Float32>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/ge_f` | `src/builtin/operators/ops.h#__builtin__ge__<Float32>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/lt_d` | `src/builtin/operators/ops.h#__builtin__lt__<Float64>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/le_d` | `src/builtin/operators/ops.h#__builtin__le__<Float64>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/gt_d` | `src/builtin/operators/ops.h#__builtin__gt__<Float64>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/ge_d` | `src/builtin/operators/ops.h#__builtin__ge__<Float64>` | IEEE 比较：任一侧是 NaN 时结果为假，本身不报错 | trait＋记录 | `declared_records` |
| `:op/lt_s` | `src/builtin/operators/ops.cpp#__builtin__lt_s__` | 按字节比较两个字符串（`src/builtin/operators/ops.cpp#compareStrings`）；长度差、前缀关系都只是次序 | trait | `declared_traits` |
| `:op/le_s` | `src/builtin/operators/ops.cpp#__builtin__le_s__` | 按字节比较两个字符串（`src/builtin/operators/ops.cpp#compareStrings`）；长度差、前缀关系都只是次序 | trait | — |
| `:op/gt_s` | `src/builtin/operators/ops.cpp#__builtin__gt_s__` | 按字节比较两个字符串（`src/builtin/operators/ops.cpp#compareStrings`）；长度差、前缀关系都只是次序 | trait | — |
| `:op/ge_s` | `src/builtin/operators/ops.cpp#__builtin__ge_s__` | 按字节比较两个字符串（`src/builtin/operators/ops.cpp#compareStrings`）；长度差、前缀关系都只是次序 | trait | — |
| `:op/and` | `src/builtin/operators/ops.cpp#__builtin__and__` | 只读它的 bool 操作数：`and`/`or` 读两个（短路只决定读不读右操作数），`not` 读一个；没有抛点 | trait | — |
| `:op/or` | `src/builtin/operators/ops.cpp#__builtin__or__` | 只读它的 bool 操作数：`and`/`or` 读两个（短路只决定读不读右操作数），`not` 读一个；没有抛点 | trait | — |
| `:op/not` | `src/builtin/operators/ops.cpp#__builtin__not__` | 只读它的 bool 操作数：`and`/`or` 读两个（短路只决定读不读右操作数），`not` 读一个；没有抛点 | trait | — |
| `:op/eq_i` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | `declared_traits` |
| `:op/ne_i` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/eq_l` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/ne_l` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/eq_f` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/ne_f` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/eq_d` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/ne_d` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/eq_b` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/ne_b` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：标量直接比：浮点按数值比（NaN 不等于自身、两个零相等），bool 比它的值 | trait | — |
| `:op/eq_s` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：两个字符串比字节内容 | trait | `declared_traits` |
| `:op/ne_s` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent`）：两个字符串比字节内容 | trait | `declared_traits` |
| `:op/eq_c` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent` → `src/core/rtdata/complex.h#Complex::equals`）：两个部分按数值各比一次 | trait | — |
| `:op/ne_c` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent` → `src/core/rtdata/complex.h#Complex::equals`）：两个部分按数值各比一次 | trait | — |
| `:op/eq_z` | `src/builtin/operators/ops.cpp#__builtin__eq__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent` → `src/core/rtdata/complex.h#Complex::equals`）：两个部分按数值各比一次 | trait | — |
| `:op/ne_z` | `src/builtin/operators/ops.cpp#__builtin__ne__` | 按内容比较（`src/core/rtdata/equality.cpp#valuesEqualByContent` → `src/core/rtdata/complex.h#Complex::equals`）：两个部分按数值各比一次 | trait | — |
| `:op/add_c` | `src/builtin/operators/ops.cpp#__builtin__add_cc__` | 两个部分各做一次正确舍入的加/减（`src/builtin/operators/ops.cpp#complexBinary`），与标量浮点同样的舍入 | 记录 | `declared_records` |
| `:op/add_z` | `src/builtin/operators/ops.cpp#__builtin__add_zz__` | 两个部分各做一次正确舍入的加/减（`src/builtin/operators/ops.cpp#complexBinary`），与标量浮点同样的舍入 | 记录 | `declared_records` |
| `:op/sub_c` | `src/builtin/operators/ops.cpp#__builtin__sub_cc__` | 两个部分各做一次正确舍入的加/减（`src/builtin/operators/ops.cpp#complexBinary`），与标量浮点同样的舍入 | 记录 | `declared_records` |
| `:op/sub_z` | `src/builtin/operators/ops.cpp#__builtin__sub_zz__` | 两个部分各做一次正确舍入的加/减（`src/builtin/operators/ops.cpp#complexBinary`），与标量浮点同样的舍入 | 记录 | `declared_records` |
| `:op/add_bf` | `src/builtin/operators/half_ops.cpp#__builtin__add_bfbf__` | 加宽到 f32 计算、结果一次舍入回元素类型（`src/builtin/operators/half_ops.cpp#binary`）；溢出得 inf，不报错 | trait | `declared_traits` |
| `:op/sub_bf` | `src/builtin/operators/half_ops.cpp#__builtin__sub_bfbf__` | 加宽到 f32 计算、结果一次舍入回元素类型（`src/builtin/operators/half_ops.cpp#binary`）；溢出得 inf，不报错 | trait | — |
| `:op/mul_bf` | `src/builtin/operators/half_ops.cpp#__builtin__mul_bfbf__` | 加宽到 f32 计算、结果一次舍入回元素类型（`src/builtin/operators/half_ops.cpp#binary`）；溢出得 inf，不报错 | trait | — |
| `:op/neg_bf` | `src/builtin/operators/half_ops.cpp#__builtin__neg_bf__` | 加宽到 f32 后翻符号、一次舍入（`src/builtin/operators/half_ops.cpp#__builtin__neg_bf__`、`#__builtin__neg_f6__`），精确 | trait | — |
| `:op/pow_bf` | `src/builtin/operators/half_ops.cpp#__builtin__pow_bfbf__` | `std::pow` 在 f32 上算、结果一次舍入（`src/builtin/operators/half_ops.cpp#binary`）；定义域外得 NaN、溢出得 inf，不报错 | trait | `declared_traits` |
| `:op/lt_bf` | `src/builtin/operators/half_ops.cpp#__builtin__lt_bf__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/le_bf` | `src/builtin/operators/half_ops.cpp#__builtin__le_bf__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/gt_bf` | `src/builtin/operators/half_ops.cpp#__builtin__gt_bf__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/ge_bf` | `src/builtin/operators/half_ops.cpp#__builtin__ge_bf__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/add_f6` | `src/builtin/operators/half_ops.cpp#__builtin__add_f6f6__` | 加宽到 f32 计算、结果一次舍入回元素类型（`src/builtin/operators/half_ops.cpp#binary`）；溢出得 inf，不报错 | trait | — |
| `:op/sub_f6` | `src/builtin/operators/half_ops.cpp#__builtin__sub_f6f6__` | 加宽到 f32 计算、结果一次舍入回元素类型（`src/builtin/operators/half_ops.cpp#binary`）；溢出得 inf，不报错 | trait | — |
| `:op/mul_f6` | `src/builtin/operators/half_ops.cpp#__builtin__mul_f6f6__` | 加宽到 f32 计算、结果一次舍入回元素类型（`src/builtin/operators/half_ops.cpp#binary`）；溢出得 inf，不报错 | trait | `declared_traits` |
| `:op/neg_f6` | `src/builtin/operators/half_ops.cpp#__builtin__neg_f6__` | 加宽到 f32 后翻符号、一次舍入（`src/builtin/operators/half_ops.cpp#__builtin__neg_bf__`、`#__builtin__neg_f6__`），精确 | trait | — |
| `:op/pow_f6` | `src/builtin/operators/half_ops.cpp#__builtin__pow_f6f6__` | `std::pow` 在 f32 上算、结果一次舍入（`src/builtin/operators/half_ops.cpp#binary`）；定义域外得 NaN、溢出得 inf，不报错 | trait | — |
| `:op/lt_f6` | `src/builtin/operators/half_ops.cpp#__builtin__lt_f6__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/le_f6` | `src/builtin/operators/half_ops.cpp#__builtin__le_f6__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/gt_f6` | `src/builtin/operators/half_ops.cpp#__builtin__gt_f6__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |
| `:op/ge_f6` | `src/builtin/operators/half_ops.cpp#__builtin__ge_f6__` | 加宽到 f32 后比较，与 f32 的比较同一条规则 | trait | — |

用例一栏写 `—` 的行没有单独的用例：它的声明与同一实现函数那一行是同一个 `kNeverFaults` 集合
里的两条条目（或同一个 `none` 参数），内核已经被那一行钉住。`declared_records` 逐个点名记录
这一侧的 36 个算子；`declared_traits` 给每个内核各钉一个代表，另加一个 `:op/div_i` 作对照——
“这份清单也能读出可能出错”。

## 看到、但没有跟着改的三处

1. `:op/and`、`:op/or`、`:op/not` 从程序里写不出来：`&&`/`||` 是分支不是算子，这三个 URI 只由
   降级后的图（`modules/onnx/lowering.cpp`）和 C++ 后端（`src/passes/trans/cpp/operator_rules.cpp`）
   产生，所以这棵树上没有一条用例能把它们的 trait 读出来。它们的实现只读 bool，没有出错路径。
2. `:op/mul_c`、`:op/mul_z`、`:op/neg_c`、`:op/neg_z` 的实现同样没有出错路径（`complexBinary`、
   `complexNegate`），但它们不在 `kNeverFaults` 里，语义记录也不带空表：现在算“可能出错”。
   把空表补上去会改变这四个算子的 trait，那是这张表的一次扩张，不属于“把已经写下的空表
   核对成实现的样子”，所以留着不动。
3. `:op/pow_bf`、`:op/pow_f6` 声明为空表，而 `:op/pow_i`、`:op/pow_l`、`:op/pow_f`、`:op/pow_d`
   连语义记录都没有（`builtin_semantics.cpp` 的文件头也这么写）。两处实现走的是同一个
   `std::pow`，都没有出错路径；差别在声明，不在实现，改它同样是一次扩张。

## 这份清单怎么复核

一条命令就是这套断言：`node scripts/test.js test/plans/feat/modules/operator_faults.plan.toml`。
它读 `camel inspect --ops` 的记录（空表与“没写”在列出来的 JSON 里是两件事），再跑一个
程序把 trait 经 `effects(...).may_fault` 读出来。两侧各红一次，是这么核对的：

- 第一处：把 `:op/add_bf` 从 `kNeverFaults` 删掉，同时让 `mul` 的浮点声明回到 `std::nullopt`
  （集合不动）。`declared_traits` 在第十二行变红（那里印 1 而不是 0），`declared_records`
  仍是绿的。这一处同时说明两件事：trait 那一侧能被打红；记录里的空表是从 trait 合并来的，
  不是 `none` 那个实参写的——这也是上面 `trait→记录` 那 8 行的由来。
- 第二处：把 `add` 的整数声明改成 `std::nullopt`，并把 `:op/add_i` 从 `kNeverFaults` 删掉。
  `declared_records` 在 `:op/add_i` 那一行变红（“declares no fault list, expected the empty
  one”）。

两处都恢复之后，两条用例都绿。
