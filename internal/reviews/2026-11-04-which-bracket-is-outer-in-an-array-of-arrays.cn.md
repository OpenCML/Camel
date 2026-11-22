# 数组的数组，外层是哪一个方括号：`extent_of::<float[2][3]>()` 该答 `[3]` 还是 `[2]`

**状态**：待裁。本轮按第 5 节的默认落地——**不改行为**，只把今天答的数钉成两条用例，并把「本类型自己的 extents」这句写进 `camel/core/type/composite/array.h#statedExtentsOf` 与 `src/builtin/operators/struct.cpp#__op_extent_of__` 的注释。

## 1. 今天的读数（探针，宿主 `88zjwei`，2026-11-04 08:31 `-05:00`，1 m load 31.5——高负载，只读相对值）

```
extent_of::<float[2][3]>()      [3]          extent_of::<float[2, 3]>()      [2, 3]
extent_of::<float[2][3][4]>()   [4]          extent_of::<float[2, 3, 4]>()   [2, 3, 4]
extent_of::<float[?][3]>()      [3]          // 内层的 `?` 不参与：它不是本类型的 extent

let xs: float[2][3] = [pair(), pair(), pair()]      // pair(): float[2] = [1.0, 2.0]
println(xs)                 [[1.0, 2.0], [1.0, 2.0], [1.0, 2.0]]
shape(xs) [3]   len(xs) 3   xs[1] [1.0, 2.0]   shape(xs[1]) [2]
```

即：`float[2][3]` 的**值**是「3 个 `float[2]`」，它的 `shape` 就是 `[3]`，而 `extent_of` 答的正是同一个列表。**答数 `[3]` 不是最内层**：最内层是 `[2]`（`extent_of::<float[2]>()`），`[2, 3]` 是另一个类型 `float[2, 3]` 的形状。之所以 `[3]` 看起来像「最后一个数」，是因为 Camel 的方括号从左到右读、最后一个方括号是外层。

顺带一处真红（**不在本页的问题里**，另立车道）：`let xs: float[2][3] = [[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]]`——恰好 3 行 2 列——被拒：

```
The array literal written here has extent 3, but the position states the shape 'float[2][3]'.
```

原因是 `src/compile/gct/builder.cpp#statesARank` 把「extents 全是常数」的类型当**一维平铺**交给 `visitRankedArrayInto`，于是 `lengths.size() (2) != extents.size() (1)`；同一个类型的值只能由**值**（`[pair(), pair(), pair()]`）拼出来，不能由嵌套字面量写出来。

## 2. 规则写了什么（以及没写什么）

- `docs/language/03_data_model.md` 3.1：「`T[][]` 是嵌套数组，rank 2 写作 `T[m, n]`」——**`float[2][3]` 不是二维形状**，二维写 `float[2, 3]`。
- `docs/design-principles.cn.md` 5.4：「写 `f32[m, n]` 而不是 `f32[m][n]`：多维数组是一个整体的对象」。
- `docs/design/arrays-memory/README.cn.md` 2.1：「`f32[m][n]` 读作『数组的数组』」。
- `docs/design/arrays-memory/plan.cn.md`（不变量）：「`T[m][n]` 是『数组的数组』（**外层元素类型是 `T[m]`**，不是 rank 2）」——**这一句定的是顺序：最后一个方括号是外层**，与今天的建法、与 `test/cases/opt/generic/specialize_element_type_nest.cml` 的注释（`f32[2]` 外套一层写成 `f32[2][1]`）一致。

**没写的是**：`extent_of` 对一个「元素类型也是数组」的类型该答什么。`stdlib/prelude.cml#extent_of` 只说「the extents an array type writes down, in order」，例子全是平铺类型。

## 3. 要裁的那一句

`T[m][n]` 的外层是**最后一个**方括号（今天，`plan.cn.md`）还是**第一个**（C/NumPy 的读法）；以及 `extent_of` 对数组的数组答**本类型自己的** extents（今天）、答**整条嵌套**、还是**拒绝**。

## 4. 三条路各带代价

- **甲（今天）**：最后一个方括号是外层，`extent_of` 答本类型自己的 extents：`float[2][3]` → `[3]`，与同类型值的 `shape` 逐位相等。代价：`float[2][3]` 的读法与 C/NumPy 相反（2 行 3 列要写 `float[3][2]`），而且这个类型的字面量今天写不出来（第 1 节那条红）。
- **乙**：第一个方括号是外层（C 顺序），`float[2][3]` = 2 个 `float[3]`，`extent_of` 答 `[2]`。代价：类型的打印是「元素类型 + 自己的 extents」，于是 `float[2][3]` 会打印成 `float[3][2]`；改打印就要动 `test/cases/compile/type_text` 的往返语料（含 `int[3][4]`），改建法就要动 `ArrayType` 的嵌套顺序——比一个 `extent_of` 的答数大得多。
- **丙**：`extent_of` 拒绝元素类型是数组的类型（只答「一整条的形状」）。代价：`statedExtentsOf` 是 `extent_of` 与转置走查共用的**同一条规则**（`modules/transforms/transpose.cpp#shapeFromTypeOf`），拒绝会连带拒绝嵌套数组操作数的转置；而今天没有任何用例要求它。

## 5. 我方默认

**甲。** `extent_of::<A>()` 答 `A` **自己**的 extents（一个层），`float[2][3]` → `[3]`，`float[2][3][4]` → `[4]`；元素类型的 extents 是同一个名字问 `float[2]` 得到的 `[2]`。规则依据是它必须与 `shape` 对得上：`extent_of::<float[2, 3]>()` 与 `shape(t: float[2, 3])` 相等（`extent_of_is_not_shape.cml` 钉的就是这条），而 `float[2][3]` 的值的 `shape` 是 `[3]`；答 `[2, 3]` 会把一个**数组的数组**说成一个**二维数组**，正是「静默错一格」。

落地（本轮已做）：`test/plans/feat/compile/extent_of.plan.toml` 两条用例
`extent_of.an_array_of_arrays_answers_its_own_extent`、`extent_of.three_nested_dimensions_answers_the_outermost`——逐位（`len`、`[0]`）、整条（与 `shape` 相等）、并逐个否定 `[2]`、`[3]`、`[2, 3]`、`[2, 3, 4]`；`array.h#statedExtentsOf` 与 `struct.cpp#__op_extent_of__` 的注释写明「本类型自己的 extents，一层」。第 1 节那条字面量红另立车道（`statesARank` 不放行嵌套字面量）。