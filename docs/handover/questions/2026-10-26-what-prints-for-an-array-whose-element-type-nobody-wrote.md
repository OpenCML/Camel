# 元素类型没人写过时，数组类型该打印成什么（`void[]` / `void[..]`）？

**问谁**：语言作者（负责人）
**问什么**：`show([])` 这类"空字面量 + 泛型形参 `T[..]`"的组合，今天会打印出 **`void[]`** 与 **`void[..]`** 两种拼写。按您在《`Tensor` 名字删除》里的判据——"**`number` 不是语言里的类型名，打印出程序写不出来的拼写不行**"——这两种都写不出来（`void` 不是可写的类型名），所以按您那条判据这是**真问题**，请您定它应当表现成什么。

## 量到的证据（都在 develop 上）

1. **拒绝消息**（`test/cases/**/native.tensor_show_empty_array` 路径，程序里写的是 `show([])`）：
   - `No matching function found for argument types: <> (void[])`
   - 带 `when` 的那一面：`No candidate of 'show' applies to (void[..])`
2. **`std::gir`**：`func mine::<T>(t: T[..])` 在 `mine([])` 处实例化后——
   - 参数槽节点 `N00 value_type="void[..]"`
   - 字面量节点 `N02 label="[]" value_type="void[]"`
3. **来源**：泛型实例化把 `T` 绑成 `Void`（调用实参是空字面量 `[]`，`unifyTypeVars` 经 `A[..]` vs `Void[]` 绑上 `void`），再由 `withElementType` 造出 `Void[..]`。**`void[]` 是既有的、不是本轮引入**（`void[..]` 也是同一条路，只是"开放秩 + 元素类型没人写"）。
4. **相关背景（上一轮同一条线的返工）**：`ArrayType::elementTypeOpen()` 与它的打印分支（打出 `number[..]`）**已按您的裁定删掉**；现在唯一还能造出"开放秩 + 元素类型没人写"的就是上面这条泛型实例化路径，我们为它保留了一个谓词（`isElementTypeUnstatedOpenArray`），因为删掉它会让 `native.tensor_show_empty_array` 从**编译期拒绝**退回**内核故障**（实测 `pass 6626 → fail 1`）。

## 需要您定的

- **甲**：**绑定处就拒**——`T := Void` 不该被接受（`void` 是"没有值"的标记，不是元素类型）⇒ 那么 `show([])` 应当在**编译期**以"实参类型不成立"被拒，不再产生 `void[]`/`void[..]`；请说明拒绝语该说什么（以及是否另给"空数组"一个可写的说法）。
- **乙**：**保留这个内部状态，但换一个可写的打印**——例如数组类型在"元素类型没人写过"时打印成 `[]`（或您给的别的拼写），诊断与 `std::gir` 同口径；请给出确切拼写。
- **丙**：**别的划法**（例如给空字面量一个真正的元素类型，让 `[]` 的类型由上下文确定而不是 `Void`；或规定"空数组不参与 `T[..]` 的统一"）。

## 现在的状态（不自行决定）

- 行为**未改**：`void[]`/`void[..]` 照旧打印；`native.tensor_show_empty_array` 照旧**编译期拒绝**（这是它的既有期望，我们没有放宽）。
- 与这条线相邻的一件已完成的事（供参考）：`to_bytes`/`from_bytes` 现在对**纯数据聚合**可用（按 `pureDataLayout` 逐叶走字段偏移/叶宽/尾填充），`from_bytes` 只剩一条 `::<T>(bytes, shape) when is_plain::<T>()` 的签名；非纯数据仍由既有的 `WhenConditionUnmet` 拒（**没有新造诊断**）。

## 证据位置（都在 develop 上）

- 打印路径：`src/core/type/text.cpp`（`void[]` 走普通数组打印；`void[..]` 走开放秩打印，原来的 `number[..]` 分支已删）。
- 谓词与守卫：`include/camel/core/type/composite/array.h` / `src/core/type/composite/array.cpp`（`isElementTypeUnstatedOpenArray`，`assignableFrom` 处是**承重**的那一处）。
- 产生路径：`substituteTypeVars` / `withElementType`（泛型实例化把 `T` 绑到 `Void`）。