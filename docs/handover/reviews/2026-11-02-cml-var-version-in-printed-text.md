# 打印器写一个 `:var/get` 时，它读的是哪个版本要不要进文本？

**问题（一问）**：`trans/cml` 把图打印回源代码。一个 `var` 的读在运行时读的是**某一个版本**（`:var/get` 的输入是那版节点：`:var/new`、一次 `:op/assn*`、或一个区域节点），而文本里只有变量的**名字**。那么：**文本里要不要出现版本号**？如果不出现，读到旧版本时怎么写？

**今天的证据（三条用例，同一机制）**：

| 用例 | 源程序 | 改前打印版 | 错在哪一行 |
| --- | --- | --- | --- |
| `linear/runtime/var_read_before_region` | `0 2` / `0 3` / `0 5 1` | `2 2` / `3 3` / `5 5 1` | `println(z, z)`——源是 `println(before, z)`，`let before = z` 整条消失 |
| `linear/runtime/var_read_before_write` | `int 2 1` / `int 2 1` / `array [7, 7] [1, 1]` | `int 2 2` / `int 2 2` / `array [7, 7] [7, 7]` | `report('int', a, a)`——源是 `report("int", a, b)`，`let b = a` 消失 |
| `linear/runtime/order_edges_regions` | `exit 56` | `exit 76`（差 20） | `return x + y + y + z + z + n`——源是 `x + b + y + d + z + n`，`b`、`d` 两个读都成了变量名 |

三条都是同一个机制：`:var/get` 只打 `cellName(cellReadBy(...))`，**读的是哪一版它不打**，于是"写在区域/写之前的读"全打成变量的名字，取到的是最后一版（`4+10+30+5+5+2 = 56` 变 `4+30+30+5+5+2 = 76`，差正好 20）。

**我方默认（已按此改，仍等作者一句确认）**：

1. **版本号不进文本**。打印出来的是**程序**，它必须重新执行出同一个值；版本是内部细节，`let` 落对位置之后文本里就不需要版本号。**"按什么次序落笔"由位置决定，不由版本号决定。**
2. **能便宜地忠实 ⇒ 忠实**：那个读若被建图器排在一次写之前（`recordWrite`："读要看见写之前的值"，图上是**读 → 写的一条 ORDER 边**），就把这个读**落成一条 `let`，位置不变**——`let before: int = z` 落在循环之前，`println(before, z)` 用名字。
3. **便宜不了 ⇒ 具名拒绝**，诊断写清"这条读的版本在文本里无法表达"；**绝不打成变量名然后取最后一版**（那是"打错了还看不出来"，比少打一行坏）。

**已按此改的读数**（基线是合并 `origin/develop = 01ff08254` 之后、本刀之前的同一棵树，语料 3361 个文件）：

| 读数 | 基线 | 本刀 |
| --- | --- | --- |
| `printed` | 553 | **553** |
| `equivalent` | 503 | **504** |
| `not_equivalent` | 40 | **39** |
| `refused` | 1807 | **1807**（**具名拒绝一次没触发**：ORDER 边给出了"读在写之前"的保证，落 `let` 总是安全的） |
| `unfaithful` | 10 | **10**（这三条不在这一格里：它们是 `not_equivalent`，行为比对不参与） |
| `DIFFERENT` | 50 行 | **49 行，新增 0** |
| `families` | 52 | **52** |

**三条卡的两侧输出（改后，`std::nvm`）**：三条**逐行相同**（`0 2`/`0 3`/`0 5 1`；`int 2 1`/`int 2 1`/`array [7, 7] [1, 1]`；`9 1 2 …` 且 `exit 56`）。打印文本里 `let before: int = z`、`let b: int = a` 都回来了。

**这一刀没做的，写在这里免得被误读**：

- **区域内的读**（`controlOf(ref) != 0`）不动：区域里的版本是区域节点的事，`arm_write.cml` 读的正是分支留下的版本，那时候变量名就是对的。区域内的版本要单独一刀，代价更大（要在 `buildBody` 里维护每个区域的版本）。
- 三条里只有 `var_read_before_write` 从 `not_equivalent` 变成 `equivalent`；另外两条**行为已经相同**，但 canonical 文档仍有差异，差的是**另一个缺陷**——循环/分支体的尾值打印成 `return i + 1` / `return println(2)`，与版本无关。

**`unfaithful` 那 10 条，逐条说出为什么（这一格不为 0 就要能解释，且本刀没有换过成员——两次扫描的 10 个文件名逐一相同）**：

| 用例 | 打印出的程序拿不到 canonical 文档的原因 |
| --- | --- |
| `compile/array_shape/literal_length` | `ArgumentNeedsConversion`：数组字面量打印后没有被 `f32[4]` 的类型，`count` 的调用需要一次转换 |
| `compile/array_shape/rank_zero_parameter_slot` | `DynamicCastForbidden`：秩零形状的打印落成语言禁止的动态转换 |
| `compile/module_operators/anon_models` | `ReservedIdentifier`：打印出的名字两端是双下划线（生成的模型名） |
| `compile/static_expansion/comprehensions` | `Redeclaration`：静态展开后同名 `v` 出现两次 |
| `compile/static_expansion/fields_and_access` | 编译无诊断，仍拿不到 canonical 文档（要单独看） |
| `compile/when_overloads/values` | `Redeclaration`：重载打印成同名 `pick` 两次 |
| `consistency/values/closure_var` | `UnresolvedReference: 'cml1'`——**基线是同一个名字**，来自通用 `let` 路径，不是本刀 |
| `linear/nvm/var_params` | `UnresolvedReference: 'cml1'`，同上 |
| `parse/constructs/comprehensions` | `Redeclaration`：同名 `v` |
| `trans/cpp/exec/variables` | `UnresolvedReference: 'cml3'`——**基线也是 `cml3`** |

**问作者的那一句**：上面第 1 条（版本号不进文本）与第 2、3 条（落 `let` / 具名拒绝）是不是你要的默认？如果"版本号要进文本"，那是另一套设计（文本里出现 `@2` 之类的写法），本刀就要改成拒绝而不是落 `let`。