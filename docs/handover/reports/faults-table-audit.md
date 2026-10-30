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

99 行是这棵树现在声明“永不出故障”的全部内置算子：91 条来自 `kNeverFaults`，8 条（复数的
`+`、`-`、`*`、取负）只写在语义记录里。第一轮的 77 行全部核对过，没有一行与它的实现不符；
裁定之后又补了 22 行：17 行是裁定点名的那些族（`pow` 的十个、`len` 的三个、复数 `mul`/`neg`
的四个），5 行是按可达性补的第一族（集合上的谓词）。核对里唯一看上去不一致的一处，查下去是
同一件事：

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
这一侧的 40 个算子；`declared_traits` 给每个内核各钉一个代表，另加一个 `:op/div_i` 作对照——
“这份清单也能读出可能出错”。

## 这一轮补上的 17 行（裁定之后）

裁定是“没声明按可能出故障读（第三态有意义，读数保守），但表要补完：先落已经核对过的族，
其余按 reach”。这 17 行就是“已经核对过的族”：它们的实现上一轮就读过，只是当时没有声明。
补的时候照该族既有写法：`pow` 跟着 `pow_bf`/`pow_f6` 走 trait，`len` 三个没有数值语义、
也走 trait，复数 `mul`/`neg` 跟着同族的 `add`/`sub` 走记录的 `none`，两条路都没有混用。

| 算子 | 实现 | 实现里的出错路径 | 声明 | 用例 |
|---|---|---|---|---|
| `:op/pow_i` | `src/builtin/operators/ops.cpp#__builtin__pow__` | `std::pow` 在 double 上算再转回元素类型；定义域外得 NaN、溢出得 inf，负指数是倒数，同样只得到 inf/NaN——实现里没有抛点（唯一的抛点是类型分派的 `default`，对写定类型的 URI 不可达） | trait | — |
| `:op/pow_l` | `src/builtin/operators/ops.cpp#__builtin__pow__` | 同上 | trait | `declared_traits` |
| `:op/pow_f` | `src/builtin/operators/ops.cpp#__builtin__pow__` | 同上 | trait | — |
| `:op/pow_d` | `src/builtin/operators/ops.cpp#__builtin__pow__` | 同上 | trait | — |
| `:op/pow_c` | `src/builtin/operators/ops.cpp#complexPowBinary` | 指数是复数时走 `std::pow`，整数指数走 `#integerPower` 的平方求幂；两者都只得到 inf/NaN，负指数是一次复数除法 `1/result`，除数为零也只得 inf/NaN | trait | — |
| `:op/pow_c_i` | `src/builtin/operators/ops.cpp#complexPowBinary` | 整数指数，走 `#integerPower`；同上 | trait | — |
| `:op/pow_c_l` | `src/builtin/operators/ops.cpp#complexPowBinary` | 同上 | trait | — |
| `:op/pow_z` | `src/builtin/operators/ops.cpp#complexPowBinary` | 同上，宽度是 double | trait | — |
| `:op/pow_z_i` | `src/builtin/operators/ops.cpp#complexPowBinary` | 同上 | trait | — |
| `:op/pow_z_l` | `src/builtin/operators/ops.cpp#complexPowBinary` | 同上 | trait | — |
| `:struct/len_arr` | `src/builtin/operators/struct.cpp#__len_arr__` | 读数组已存的元素个数 `arr->size()`，只读一个字段 | trait | `declared_traits` |
| `:struct/len_str` | `src/builtin/operators/struct.cpp#__len_str__` | 按码点数长度（`src/utils/utf8.h#count`），一个纯循环；输入已保证是合法 UTF-8，没有抛点 | trait | `declared_traits` |
| `:coll/len` | `src/builtin/operators/collection.cpp#__coll_len__` | 读 Map/Set 已存的条目数 `table->size()`，只读一个字段 | trait | — |
| `:op/mul_c` | `src/builtin/operators/ops.cpp#complexBinary` | 四个部分各做一乘一加，各舍入一次；与 `add`/`sub` 同一个内核，没有抛点 | 记录 | `declared_records`、`declared_traits` |
| `:op/mul_z` | `src/builtin/operators/ops.cpp#complexBinary` | 同上，宽度是 double | 记录 | `declared_records` |
| `:op/neg_c` | `src/builtin/operators/ops.cpp#__builtin__neg_c__` | 两个部分各翻一次符号，精确 | 记录 | `declared_records` |
| `:op/neg_z` | `src/builtin/operators/ops.cpp#__builtin__neg_z__` | 同上，宽度是 double | 记录 | `declared_records` |

补完之后新出现的一处口径差，记在这儿而不是顺手改：`:op/div_d` 在除数为零时报
`division_by_zero`，而 `:op/pow_d` 的 `0 ^ -1`、`:op/pow_c` 的零的负次幂在实现里只得 inf
（`1/result` 是一次复数除法，见 `#integerPower`）。也就是说同一个“除以零”，在 `div` 上是故障、
在 `pow` 上是 inf。这是两个算子的语义口径要作者定的事，不是声明与实现不符：声明写的是实现的
样子，`pow` 的实现确实不抛。

## 第二批：集合上的谓词（5 行，按可达性挑的）

裁定之后按可达性排的第一族。挑它是因为三件事同时成立：`stdlib/prelude.cml` 直接声明了这几个
算子并且每个都带示例（`#op(':coll/has')`、`'set_contains'`、`'eq'`、`'neq'`、`'set_is_subset'`），
测试在用，而且它们的实现不造任何值。**不造值**是关键：这个运行时里分配失败是一条真的出错路径，
`src/core/rtdata/collection.cpp#Table::create` 在分配不到时 `throw std::bad_alloc()`，
`src/execute/pass.cpp` 在跑的循环里接住 `mm::MemoryExhausted` 并报成 `RuntimeDiag::MemoryExhausted`。
所以 `coll/` 里造值的那些（`map_put`、`set_add`、`merge`、`set_union`、`set_intersect`、
`set_difference`、`keys`、`map_values`、`map_items`、`set_items`，以及会给可空值装箱的
`map_idx`）都有这条出错路径，不在这一批里。

| 算子 | 实现 | 实现里的出错路径 | 声明 | 用例 |
|---|---|---|---|---|
| `:coll/has` | `src/builtin/operators/collection.cpp#__coll_has__` | 在表里走一遍找键（`table::has`），找不到返回 false；不造值、不抛 | trait | `declared_traits`、`fault_free_behavior` |
| `:coll/set_contains` | `src/builtin/operators/collection.cpp#__coll_has__` | 与 `has` 同一个内核，返回的是“是不是元素” | trait | `declared_traits` |
| `:coll/eq` | `src/builtin/operators/collection.cpp#__coll_eq__` | 按内容比较两张表（`Table::equals`，递归比键与值），只读 | trait | `declared_traits` |
| `:coll/neq` | `src/builtin/operators/collection.cpp#__coll_neq__` | 同一个比较取反 | trait | `declared_traits` |
| `:coll/set_is_subset` | `src/builtin/operators/collection.cpp#__coll_set_is_subset__` | 逐个元素在另一张表里找（`table::isSubset`），只读 | trait | `declared_traits` |

这一族的反向验证有两半，正好各打红一次：

- 把 `coll/has` 从 `kNeverFaults` 删掉，`declared_traits` 在 `map->has<'a'>` 那一行变红（读到 1）。
- 给 `__coll_has__` 加一个“找不到就抛”：`declared_traits` 仍是绿（声明没动，读数只看声明），
  但 `fault_free_behavior` 那条**运行**的用例变红——它把返回的 false 换成了一段诊断。这就是
  “这条断言能在它其实会 fault 时红”的意思：空表是声明的样子，运行出来的值是实现的样子。

## 看到、但没有跟着改的

1. `:op/and`、`:op/or`、`:op/not` 从程序里写不出来：`&&`/`||` 是分支不是算子，这三个 URI 只由
   降级后的图（`modules/onnx/lowering.cpp`）和 C++ 后端（`src/passes/trans/cpp/operator_rules.cpp`）
   产生，所以这棵树上没有一条用例能把它们的 trait 读出来。它们的实现只读 bool，没有出错路径。
2. tensor、math 两张表和其余没有语义记录的算子仍然“没声明”。它们的实现这一轮没有核对，按裁定
   一律读作可能出错——`declared_traits` 里 `tensor::sum` 那一条就是这个状态的钉子：它今天读 1，
   而且它的记录里 `faults` 成员整个不出现。补它们按下面“按 reach 补的规矩”走。

## 按 reach 补的规矩

裁定让其余算子“按 reach”补。这是它唯一可执行的形态：

- **谁是“碰到”**：改了那个算子的实现（`src/builtin/operators/` 里的内核或它的调度条目）或它的
  声明（`src/builtin/executors/builtin.cpp#kNeverFaults`、`builtin_semantics.cpp` 的记录）的那个人。
  只读代码、只改用例的不算。
- **碰到时做什么**：一次做完三件事——读实现的出错路径；按这一族既有的写法补 `faults`（trait 或
  记录里的 `none`，不许混用）；把它挂到 `test/plans/feat/modules/operator_faults.plan.toml` 的
  两条断言上（记录侧点名，trait 侧读 `effects(...).may_fault`），并在这张清单里补一行。
- **为什么不能一次补完**：这张表不是注释，是 `effects(...).may_fault` 的输入
  （`src/runtime/effects.cpp#ofNodes`）。一次补齐等于一次全局语义改动：一批图的 `may_fault`
  从 1 变 0，而读它的地方不止一处；一处读错，错的是调度与优化能假定什么。所以补齐要跟着
  “有人核对过这个内核”走，不能跟着“表看起来缺”走。
- **判据**：一条“不会故障”的声明成立，指的是那个内核既不抛运行时故障，**也不分配**——这个
  运行时里分配失败是 `RuntimeDiag::MemoryExhausted`（`src/execute/pass.cpp` 接住
  `mm::MemoryExhausted` 后报成诊断），是一条程序看得见的故障。所以造值的算子（数组、字符串、
  表、装箱）默认都有这条路径，除非内核里根本没有分配。
- **没碰到但不放心**：写进 `docs/handover/questions/` 的一页，不要在这里顺手补。

这一轮之前“没声明是第三态还是没写完的表”提的那一问，答在这一页：
`docs/handover/reviews/questions-2026-10-30-undeclared-faults.md`。

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

补上的 17 行也各打红了一次，打红的是它们各自那一侧：

- 第三处（trait）：把 `:op/pow_l` 与 `:struct/len_arr` 从 `kNeverFaults` 删掉。`declared_traits`
  在第十六行（`a ^ b`，注意源程序的 `int` 是 i64，`^` 落到 `:op/pow_l`，`:op/pow_i` 从程序里
  写不出来）与第十八行（`len(xs)`）变红，`declared_records` 仍是绿的——只写在 trait 里的声明
  在 `--ops` 里根本不出现，这也是这一族只能用 trait 那一半钉住的原因。
- 第四处（记录）：把复数 `mul` 的声明改回 `std::nullopt`。`declared_records` 在 `:op/mul_c` 变红
  （“declares no fault list, expected the empty one”），`declared_traits` 绿。

第四处正是记录侧与 trait 侧的差别：复数乘没有 trait 条目，空表只能写在记录里，也只有
`declared_records` 读得到。

第五处（集合谓词，两半各红一次）：把 `coll/has` 从 `kNeverFaults` 删掉，`declared_traits` 在
`m->has<'a'>` 那一行红；恢复之后给 `__coll_has__` 加一个“找不到就抛”，`declared_traits` 仍绿、
`fault_free_behavior` 红（它读到的是诊断而不是 `false`）。这一处说明了为什么这一族要有第二条
用例：声明那一侧只证明“宣言还在”，运行那一侧才证明“这个保证是真的”。

复核一条命令之后的三条读数（今天）：`declared_records` 点名 40 个带记录的算子（36 个空表、
`div`×6 与 `mod`×2 的零除数、外加 `tensor:sum` 的“没有这张表”），`declared_traits` 是 25 行
（23 个 0，`:op/div_i` 与 `tensor::sum` 两个 1），`fault_free_behavior` 5 行输出。
