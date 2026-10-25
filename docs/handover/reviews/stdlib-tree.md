# 标准库评审：`tree`（决策者 2026-10-22，H51 第 13 步 / H230）

方向很好：五个 `tree_*` 里三个已经是 Camel 函数，`match type`、`static for`、`t.[n]`、`..ts`、`static loop` 组合得当，`reduce_at` 的 `case struct | tuple` 尤其干净。以下几处**返工**：

1. **去掉 `typeas`**（H229 已合入）：全部改为自动绑定的写法。
2. **结构体与元组两个分支逐字重复**（`paths_at`、`map_at`、`first_leaf`、`unzip_at` 各一次）：只差 `{ … }` 与 `( … )`。这是语言缺口，补进语言而不是在库里重复：**"与 `T` 同形的推导式"**——`T { static for n, F in fields T => e }` 构造一个与 `T` 同种类（结构体或元组）、同字段的值；元组的字段名就是十进制下标，规则不变。之后四个函数都写成 `case struct | tuple => T { … }`。
3. **`first_leaf` 在 `static for` 里用 `return` 提前退出**来取第一个字段：这是绕路。取"第一个字段"应有直接的写法，例如编译期的字段名数组 `field_names::<S>()[0]`（库里用 `fields` 写的类型函数或编译期函数），然后 `first_leaf(r.[first], s.[first])`。
4. **`unzip_trees(r, s, sample: typeas L)` 用一个值只为取它的类型**：与 `to_function(t, 样本)`、`constant(c, f32(0.0))` 同一个毛病。用设计原则 5.6 已列的原语 `typeof`：`static for j, C in fields typeof(first_leaf(r, s)) => …`，删去样本参数。`typeof` 若未实现，作为本项的语言缺口补上。
5. 辅助函数 `join` 与规则语言的 `join`（`fact` 的子句）同名，读者容易混淆：改名为 `dotted`。
6. `tree_leaves` 仍是内建：报告里写明还缺什么（多半是"结果是异构元组"需要的类型位置），列为第二期，不在库里绕。

## 第二轮（决策者 2026-10-22）

通过：同形推导式 `T { static for … }` 与 `typeof` 落地后，结构体与元组的重复全部消失，`unzip` 的样本参数也没了，代码已经是范本的样子。剩下的都是小处：

1. **转手的包装函数**：`tree_map` 只是调用 `map_at`、`tree_reduce` 只是调用 `reduce_at`、`tree_unzip` 只是调用 `unzip_trees`——让导出的函数自己递归，删去 `*_at`/`unzip_trees`。`paths_at` 多一个 `here` 参数，用 H235 的默认值写成 `tree_paths::<T>(t: T, here: string = '')`。
2. **`first_leaf` 里同一个表达式 `( static for n, G in fields S => n ).[0]` 写了两遍**：先绑定一次（`let first = …`），或在库里给一个 `first_field::<S>()`。
3. **用不到的绑定**：`n, G`、`j, C` 里的 `G`、`C` 没有用到，写成 `n, _`；若 `_` 在这里还不能用，作为语言缺口补上（与 `match` 的 `_` 同义）。
