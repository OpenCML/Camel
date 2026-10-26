# `match type` 里怎么认出一个数组？

**问谁**：语言作者（负责人）
**问什么**：库需要"`T` 是不是数组""`T` 是不是能切片的（数组、`string`、`Bytes`）"这两个判断，而设计原则 5.5 的 `match type` 今天只有 `struct`／`tuple`／`func`／`#leaf` 四个**形状**词，数组没有对应写法。请裁一个写法（或裁"不要用 `match type`"）。

## 量到的事实

- **谁需要它**：
  - `docs/handover/stdlib/README.cn.md:39`（第三批第 1 条）：核心 `slice` 的**函数形式**作用于数组、字符串和 `Bytes`，"它需要 `match type` 能认出数组（`is_array`、`is_sliceable`）"。
  - `docs/handover/stdlib/prelude.signatures.cml:195`：`export func slice::<T>(x: T): T when is_sliceable::<T>()` —— 前奏的 `slice` 就挂在这个谓词上。
  - `docs/handover/stdlib/types.notes.md:20`：作者已经把这一条留成待确认——"`match type` 里数组的 `case` 写法我没有在语言文档里找到，**不猜**，等集成时确认再加"；`:29` 推荐加 `is_array::<T>()`，`:33` 说 `is_sliceable` 卡在同一个点上。
  - `docs/handover/stdlib/std.notes.md:78`：同上，并给了备选"或写成三条 `when` 分臂"。
  - `docs/handover/stdlib/README.cn.md:8`：`array` 的风险点——`flatten` 的两个重载若在 `int[][]` 上同时匹配，`T[..]` 那一个要加 `when !is_array::<T>()` 区分。
- **今天的读数**：`is_array`／`is_sliceable` 在全树（`stdlib`／`modules`／`src`／`test`）**0 处**，只在上面那几份草稿里出现，所以没有任何现存写法可以参考。
- **形状词就四个**：`src/parse/ast/builder.cpp:3086-3102` 的 `typeCaseKindOf` 只认 `struct`／`tuple`／`func`／`#leaf`，其余一律 `ASSERT(text == "_", "Unknown match type pattern")`；`src/compile/gir/builder_static.cpp:76-80` 是同一份名字的另一半（诊断文本）。`case int`／`case string`／`case bool` 走的是**另一条路**：`builder.cpp:3114-3121` —— 带类型子节点的模式是**身份**模式（`TypeCaseKind::Identity`），形状词不带类型子节点。
- **今天写 `case array` 会静默变成 `case _`**：`array` 不是类型名，`case array` 不带类型子节点，于是落到 `typeCaseKindOf`；Release（`npm run build` 带 `NDEBUG`）里 `ASSERT` 是空操作（`include/camel/utils/assert.h:70` 的 `#define ASSERT(...) ((void)0)`），函数返回 `TypeCaseKind::Wildcard` —— **它读作 `case _`，匹配一切类型，而且没有任何诊断**。所以"先照某个候选写、等作者回话"这条路走不通：它会悄悄匹配一切而不是报错。这也是一个缺的具名诊断（未知形状词）。
- **规范文本的现状**（`docs/design-principles.cn.md:392`，5.5）：形状词写的是 `case struct`／`case tuple`／`case #leaf`；"一个 case 也可以写类型名（`case int`、`case MyStruct`、`case T`），按类型的**身份**精确选分支"。数组两种都不是：它没有形状词，而身份 case 要写一个确定的类型名，`T[]`／`T[..]` 这类带形状和类型变量的数组类型没有先例。

## 候选

- **甲（推荐）：给 `match type` 加一个数组形状词 `case array`**（与 `struct`／`tuple`／`func`／`#leaf` 并列为 `TypeCaseKind::Array`）。则 `is_array::<T>()` 就是 `match type T { case array => true; case _ => false }`，`is_sliceable::<T>()` 由三个谓词合成：`is_array::<T>() || is_string::<T>() || is_same::<T, Bytes>()`（`is_same` 是 `types` 本次要加的那一个）。**好处**：谓词用 Camel 写得出来，用户也能自己写（设计原则 294 说 `when` 的条件就是"用 `match type` 写在库里，用户也可以自己写"）；形状词的位置已有四个先例，改动只落在解析器与 GCT 的 kind 表。**代价**：解析器＋GCT 各加一个 kind；顺带该补上"未知形状词"的具名诊断（今天 Release 静默变 `_`）。
- **乙：用身份 case 写**，例如 `case T[..]`（元素类型正是这个函数的 `T`）或 `case _[..]`。**好处**：不动 kind 表。**问题**：身份 case 今天比的是"类型身份"，而数组类型带形状（`T[4]` 与 `T[..]` 是不是同一个身份、元素类型里写类型变量怎么比）没有任何先例，得先裁"身份"对带形状的类型是什么意思。
- **丙：不在 Camel 里认数组**——把 `is_array`／`is_sliceable` 做成核心的计算签名（`#op`），或把前奏的 `slice` 按 `std.notes.md:78` 的备选写成三条 `when` 分臂、由编译器认。**好处**：语言不动。**代价**：谓词不能给用户用（`types` 这个模块的意义就是"用户也能自己写的谓词"），`is_sliceable` 这种"或"的判断也没有 Camel 写法。

我们倾向**甲**。

## 缺陷：未识别的形状词在 Release 里静默当 `_`，在 Debug 里 abort（请单独立项，不必等形状词的裁定）

这一条不是开放语义，是**缺陷**：不管形状词的语言规则怎么定，"写错的形状词静默匹配一切"必须先停。

- **位置**：`src/parse/ast/builder.cpp:3086-3102` 的 `typeCaseKindOf`——它只认 `struct`／`tuple`／`func`／`#leaf`，其余走 `ASSERT(text == "_", "Unknown match type pattern")` 后 `return TypeCaseKind::Wildcard`。
- **Release 的读数**（`npm run build` 带 `NDEBUG`；`include/camel/utils/assert.h:70` 的 `#define ASSERT(...) ((void)0)`）：**静默返回 `Wildcard`**，也就是 `case _`。
- **Debug 的读数**：`ASSERT` 生效，但它走的是 `handle_assert_failure`（`include/camel/utils/assert.h:62`，`[[noreturn]]`，打印后 **abort**），不是一条带位置的诊断。
- **最小复现**（今天全树没有一处写 `case array`，所以这是潜伏的，不是正在算错的答案）：

  ```camel
  func is_array::<T>(): bool {
      return match type T {
          case array => true
          case _ => false
      }
  }
  ```

  在 Release 里 `case array` 读作 `case _`，于是**每个类型都返回 `true`**，`case _ => false` 是死分支，且编译期一声不响。
- **建议**：把未识别的形状词改成**具名诊断**，钉在 `case` 那个词的位置上（形状词的位置是语法上标明的，`TypeCaseKind` 表就在 `src/compile/gir/builder_static.cpp:76-80`）；这条与形状词的裁定彼此独立，可以先行。
- **注意**：`case T`／`case U`（身份 case，带类型子节点）走的是 `builder.cpp:3114-3121` 的另一条路，不受这条影响。

## 挂起（不自行决定）

- `types` 的 `is_array`／`is_sliceable` 两个谓词**先不接**（`is_same`／`is_real` 不受影响）。
- 前奏核心 `slice` 的**函数形式**（`stdlib/signatures/globals.cml` 一侧）等这条裁定；`str` 模块自己按 `str.notes.md` 只做 `s[a:b]` 形态，不受影响。
- `array` 的 `flatten` 两个重载是否需要在 `int[][]` 上区分，等 `is_array` 有了写法再量（README 第一批第 3 条要求的正是"先编译验证"）。