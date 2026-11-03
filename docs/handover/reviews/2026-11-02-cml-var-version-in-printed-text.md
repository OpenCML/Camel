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

**2026-11-02 第十六刀：区域内的读是同一个事实，不是另一把刀（已按此改，补了用例）**

上一版这页写过"区域内的读不动，要单独一刀"。**查过记录版本的位置之后，这个说法收回**：

- `git grep -n 'recordWrite' src/compile/gir/` 只有三处调用：`builder_expr.cpp#recordWrite` 本体、`builder_control.cpp:2921`（`mergeArmVariables`，`builder_control.cpp:2775`/`3657`/`4062` 调它）、`builder_operators.cpp:4250/4293/4302`（经调用的写）。**版本只在一处记**，只是"写本身就是区域节点"时它写 **control** 边而不是 ORDER 边。
- **臂/循环体的节点就在块的同一个图里**（`controlOf` 是"哪个区域铺开它"，不是"哪个图记它的版本"）：`if` 的 then 臂里的读与它的写之间有 `N0C -> N10 [ORDER]`，两端 `controlOf` 都是那个 BRCH。**所以判据一条就够**：`orderUsersOf(ref)` 非空 ⇒ 落 `let`，臂里的 `let` 就落在臂里。
- 删掉那条 `controlOf != 0` 的守卫之后：`report('int', before, t)` 回来了，源 `int 3 4`、打印版也 `int 3 4`（改前是 `report('int', t, t)` → `int 4 4`）。
- **语料 3361 个文件一个都没踩到这个角**，所以补了用例 `test/cases/trans/cml/arm_read.cml`（进了 `cml_contract.py` 的 `CORPUS`/`MARKERS`）。守卫在时它红，删掉后绿——**用例咬得住，缺陷也确实只在角上**。
- 读数（同一棵合并后的树，语料 3363）：`printed 554/554`、`equivalent 504→505`、`not_equivalent 40→39`、`refused 1808/1808`、`unfaithful 10/10`（同一批文件名）、`DIFFERENT 50→49 行、新增 0`、`families 53/53`。

**`unfaithful` 那 10 条的重新分类（按作者 00:30 的裁定：名字只给人读、可重名；身份只靠图 id；去重归 `std::cml` 起名规则）**

裁定把"起名"划给打印器（M7），所以这 10 条要按**打印文本能不能重新编译**分类，而不是按"重名好不好看"：

| 分类 | 用例 | 为什么 |
| --- | --- | --- |
| **真 `unfaithful`（打印文本不能重新编译）** | `compile/static_expansion/comprehensions`、`parse/constructs/comprehensions`（`Redeclaration: 'v'`）、`compile/when_overloads/values`（`Redeclaration: 'pick'`） | 编译器**拒收**打印出来的文本：同一作用域里两处同名的绑定。**不是"读起来费劲"，是编译不过**。修法归 M7（打印器起名保证同作用域唯一），**不引入"名字即身份"的规则** |
| 同上 | `compile/array_shape/literal_length`（`ArgumentNeedsConversion`）、`compile/array_shape/rank_zero_parameter_slot`（`DynamicCastForbidden`） | 打印出的字面量/形状没有它需要的那层类型，编译不过 |
| 同上 | `compile/module_operators/anon_models`（`ReservedIdentifier`）、`compile/static_expansion/fields_and_access`（无诊断但拿不到 canonical 文档）、`consistency/values/closure_var`、`linear/nvm/var_params`、`trans/cpp/exec/variables`（`UnresolvedReference: 'cmlN'`） | 打印出的名字（生成名/临时名）在这些位置不可写、或临时名出了作用域 |
| **只是"读起来费劲"（不是缺陷）** | **无** | 十条全部编译不过；**没有一条**是"能编译、只是重名" |

**"两个同名 `let`"这一句怎么答（承 M7）**：名字只给人读，**重名本身不是缺陷**；但**打印出来的文本必须能重新编译**，而编译器在同一作用域里拒收同名绑定——所以打印器起名时同作用域内要唯一（今天的 `cmlN` 已经这么做），**这条规则不进"身份"那一层**（身份只靠图 id）。

**"名字不是身份"在打印路径上的落点**：打印器**不按名字查算子**——四处查表全按 URI（`isPure(uri)`、`callFormOf(uri)`、`groupOf(uri)`，见 `src/passes/trans/cml/cml.cpp#operatorExpression` 与 `#moduleImports`），名字是表里查出来的**拼写**（`callForm->name`、`operatorSymbolOf(group)`）；`sourceName(ref)` 只用来给人看的绑定名。`plan-roundtrip.cn.md` 的 `byName` 那一处是"`import` 一个名字还是 `import` 一个模块"的**拼写选择**，查的仍是 URI。

**2026-11-02 查证：branch/loop 尾值那个 `return` 不是缺陷（只查未改）**

问题原样：`return i + 1` / `return println(2)` 是"值是语句被当成值是表达式"，还是"区域的尾值被包成一个没有名字的返回"？

**`git grep -n 'println' src/passes/trans/cml/` 的原文**（十处，没有一处是语句/尾值逻辑）：

```
cml.cpp:1281:    /// tensor`). An operator of the core (`:op/add_l`, `:io/println`) has no module to import, and
cml.cpp:1679:                // (`println('before'); fail<'NoPremise'>('...'); println('after')` printed as
cml.cpp:1774:    /// `println(before, z)` after a loop that writes `z` printed `z` twice (the A7 cases
cml.cpp:1781:    ///     println(cml1, z)    // the old version, then the one the loop left
cml.cpp:2601:        // the name and the argument count come from the declaration (`:io/println`, `tensor:sum`,
cml.cpp:2607:        // A name is written when the program can write it: an operator of the core (`:io/println`,
cml.cpp:2636:                // is not an operator of the language (`tensor:sum`, `:io/println`) has no symbol,
cml.cpp:2669:        // `print<'{}-{}'>(3, 4)`, `println<'{:.2f}'>(0.5)`. The format is a `with` input of the
cml.cpp:2670:        // call (`:io/print` and `:io/println` carry `<fmt: string>`), and a print that dropped it
cml.cpp:2678:            // `println<'{:.2f}'>` -- a string the call holds and the source writes in `<>`. Any
```

**答案：第二种，而且是"有名字的返回"——`return <值>`，不是"没名字"**。它在 `CmlPrinter::buildBody` 里一句就写完：`if (!functionBody && regionReturnsValue(region) && !tailPrintedAsStatement) { … body.lines.push_back(indent_ + "return " + value.text); }`——**区域要交值、尾值又没被当成语句打出来时，就打一条 `return`**；注释写的理由是与"裸尾表达式"相比多一道门（"a bare last expression yields the value without that gate"）。**与"值是语句/值是表达式"的混淆无关**：`:io/println` 在打印器里只是普通算子，没有任何 `println` 专属分支。

**而它不是缺陷**：最小用例（循环体尾值 `i + 1` → 打印成 `return i + 1`）回程**判等**——`EQUAL: the two canonical documents are equivalent (37 vertices, 125 edges)`。**打印成 `return` 的图与原图等价，所以不该"修"。**

**因此 49 行 DIFFERENT 的主项要换人**：`var_read_before_region` 今天仍不等价，但检查器指出的是**另一个结构类**——`a structural class has 1 vertex/vertices on the left and 0 on the right: {"op": ":var/new@1.0" …}`，与尾值无关。下一步该查这一处（`order_edges_regions` 大概是同一处），而不是尾值。

**问作者的那一句**：上面第 1 条（版本号不进文本）与第 2、3 条（落 `let` / 具名拒绝）是不是你要的默认？如果"版本号要进文本"，那是另一套设计（文本里出现 `@2` 之类的写法），本刀就要改成拒绝而不是落 `let`。