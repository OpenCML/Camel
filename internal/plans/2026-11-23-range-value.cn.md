# 范围是值：`Range` 进标准库，下标与循环按类型走

依据：设计原则 5.4「下标与切片」「范围是值，不是数组」两条（负责人 2026-11-23 定）。

## 要达到的状态

- `a..b`、`..b`、`a..`、`..`、`range(a, b, s)` 的值都是标准库的命名类型
  `Range = { start: int?, stop: int?, step: int }`，任何位置都不建数组。
- 下标逐维按写进去的值的类型分派：`int` 取位置，`Range` 切片，整数数组 `take`。
  解析器与建图器里不再有"认识 `range` 这个名字"或"下标里的范围另走一条路"的代码。
- `for` 接受能转成流的值；`Range` 提供到 `int..` 的转换，循环每轮数一个。
- 不新增内核类型种类：`Range` 是库里的记录，内核只把 `..` 写法映射到它的构造函数（按 URI）。

## 现在的特例（要删的）

- `src/parse/src/syntax/parser_expr.cpp` 的 `sliceIndices`（约 854–900 行，含 `atWord(0, "range")`）。
- `src/parse/src/ast/builder_func.cpp` 约 660–690 行对 `range` 名字的判断。
- `src/compile/src/gct/builder_expr.cpp` 的 `buildSlice` 及"写了哪一端"的掩码。
- `src/compile/src/gir/builder_call.cpp` 约 1853 行、`builder_static.cpp` 约 1113 行按 `"range"` 名字的判断。
- `src/compile/src/gir/builder_composition.cpp` 的 `intervalValue`（`a..b` 降成 `range(a, b, 1)` 返回 `int[]`）。

## 步骤

1. **库**：在标准库声明 `Range` 与 `range(a, b, s)`；长度、第 `k` 个、是否为空；到 `int..` 的转换；
   按期望类型到 `int[]` 的转换；`map`、`filter`、`reduce` 等接受 `Range`（给出结果数组）。
2. **写法**：`a..b` 等四种写法建 `Range` 值，缺的端为空；`..` 仍不可重载。
3. **下标**：多维下标拆成逐维，每一维按值的类型走同一张重载表；`Range` 那一维是切片算子，
   端点从记录字段读，缺端由这一维补（规则见原则 5.4）。只检查实际取到的下标
   （`2026-11-23-how-a-backward-slice-names-the-position-before-the-first` 的答复）。
4. **形状**：切片的类型函数读常量 `Range` 的字段，`t[2..5]` 的结果形状编译期已知；端点不是常量时结果长度是动态的。
5. **循环**：`for` 接受能转成流的值（新增的通用规则，诊断 `LoopSequenceNotIterable` 的条件随之改）；
   `static for` 遍历常量 `Range` 在编译期展开。
6. **语料与文档**：把 `a..b` 当数组用的地方改成按期望类型转换或直接用 `Range`；
   `docs/language` 的 03（数据模型里"区间"那一行）、06（下标与切片、`loop`）、07、08（文法）同步改。

## 验收读数

- `git grep -n '"range"' -- src/parse src/compile` 为 0。
- `for i in 0..100000000` 的峰值内存与 `0..10` 相同量级（报两个数）。
- `export_core_slice`、`slice_step_negative` 转绿，`ort_core_slice.py` 五组对照不变。
- 新用例：`let r = 2..5` 后 `t[r]` 与 `t[2..5]` 相同；`t[2..5]` 的形状是静态的；
  一个用户自己的命名类型提供转流，`for` 能遍历它。
- 内核类型种类的枚举没有新增项。
