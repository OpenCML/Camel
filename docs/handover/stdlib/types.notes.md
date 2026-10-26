# types 重写说明

文件：`types.cml`。依据：设计原则 5.5（`when`、`match type`）、10.4。没有编译过。

## 设计：一族谓词，一种拼写

`is_<性质>::<T>()` 问一个类型，`is_same::<T, U>()` 问两个。身份类谓词（`is_integer`、`is_float`、`is_complex`、`is_string`、`is_bool`、`is_function`）各自带 `match type` 的分支；其余谓词（`is_real`、`is_numeric`、`is_ordered`、`is_plain`）只去问它们，类型名只在一处出现。单个具体类型（`i32`、`f32`、`int`）不再各配一个谓词，用 `is_same`——这也取代了 `array` 草稿里的六个 `is_elem_*`。

## 函数表

| 名字 | 处理 | 理由 |
|---|---|---|
| `is_same::<T, U>()` | 新增 | `match type T { case U => true; case _ => false }`（5.5 允许 `case` 里写类型变量）；`conj`、位运算、`array` 的内核分臂都要用 |
| `is_real::<T>()` | 新增 | 整数或浮点：`abs`、`sign`、`clamp`、转换函数的条件，原来到处写 `is_integer::<T>() \|\| is_float::<T>()`（数十处）。`is_numeric`（实数或复数）与 `is_ordered`（实数、字符串、布尔）改为去问它 |
| `is_integer`、`is_float`、`is_complex`、`is_string`、`is_bool`、`is_function`、`is_numeric`、`is_ordered`、`is_plain` | 保留 | 体不变；`module types` 行删除，文档微调 |
| `every_field_plain`（私有） | 保留 | `static loop … for n, F in fields T`，没有运行期的类型检查 |

没有写 `pure`、`@pure`：谓词的结果是编译期常量，不涉及资源。没有 `fail`、没有 `!->`。

未新增：`is_array`、`is_struct`、`is_tuple`、`is_stream`。`flatten` 的两个重载若在 `int[][]` 上同时匹配，需要 `is_array`（`array` 的 notes 里已列为风险点）；`match type` 里数组的 `case` 写法我没有在语言文档里找到，不猜，等集成时确认再加。

## 迁移

54 个文件使用这些谓词，只增不删，无需迁移；`array.cml`（第一批定稿）里的 `is_integer::<T>() \|\| is_float::<T>()` 可顺手换成 `is_real::<T>()`（约 6 处），`types` 文档页（`stdlib-reference.md`）由生成器重生成。

## 待决策者裁定

1. **`is_real` 的名字**（备选 `is_scalar_number`）。推荐 `is_real`：数学上就是这个词，与 `re`/`im` 对应。
2. 是否加 `is_array::<T>()`，写法待语言确认（见上）。推荐加，一旦 `match type` 能匹配数组。

## 修订（决策者 2026-10-26）

- 前奏的核心 `slice` 需要谓词 `is_sliceable::<T>()`（数组、`string`、`Bytes`）；它要问"是不是数组"，仍卡在 `match type` 里数组的 `case` 写法，和 `is_array` 同一个未决点。
