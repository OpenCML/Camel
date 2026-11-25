# 编译器自动插的转换遇到非 Safe 配对，应该是什么：编译期拒绝、运行时 fault、不插这个转换，还是别的

## 一句话的问题

**用户写下的转换**（`x as T`、`float(x)`）遇到非 Safe 配对时抛 fault 是对的：`castSlotFrom` 只对 Safe 配对定义，
把「编译器写了一个它不该写的调用」的保证变成一次可捕获的 fault，说的是程序请了一次它做不到的转换。

**编译器为了让类型对上而自动插的那个转换**——`error(...)` 落到 `int?!` 的返回位置、`T` 装进 `T?`、`T` 装进 `T!`
——遇到同样非 Safe 的配对时，应该是什么？四条候选，我不替作者选：

1. **编译期拒绝**：不插，或插了就在建图时报错（设计原则 3.9「越界」那句的形状：编译期能知道的，不该留到运行期）。
2. **运行时 fault**：这就是 (a) 的做法（`RuntimeDiag::InvalidArgument`）。
3. **不插这个转换**：让类型规则自己容纳这个位置（第 15 节 `entering-operator-args` 正在问的是它的近亲）。
4. **别的**：例如让配对表把这些配对算 Safe —— 它们本来就是「装入」而不是「转换」。

## 事实：两半各自都对，合起来让 332 条红

| 半 | 提交 | 它说的事 | 对不对 |
| --- | --- | --- | --- |
| (a) | `61bab72902` | `__cast_value__` 对**非 Safe** 配对不再调 `castSlotFrom`，改抛 `InvalidArgument`。理由是 `castSlotFrom` 只对 Safe 配对定义，那个 GUARD 是「调用方可以写什么」的陈述，不是程序能观察到的 fault | **对**：对用户写的转换，它把「说编译器写错了」换成一次可捕获的 fault |
| (b) | `310a952e0f`（车道 `gir-opertarget`） | **编译器写的转换**改成解析到内建算子 `:op/cast`——一个 `registry.set(":op/cast", {.pure = true, … .mayFault = true})` 的算子（`src/stdlib/std/builtin/executors/builtin.cpp`） | **对**：图里只有算子，转换有身份、有事实（`operator_table.h`） |

于是**编译器自动插的**转换真的走进了那条会拒绝的路径：332 例 `exit 1`，典型 `compile.tagged/combined.cml:13:21`
的 `void!` → `int?!`。**已决定：只 revert `61bab72902` 一笔，保留 (b)**，恢复旧行为，等这一页的裁决。

## 为什么这不是工程上可以顺手选一个的

因为**每一半都能在自己的判据里站住**，冲突落在「没人定义过的那个接口」上：设计原则没有一句话说
「编译器自动插的转换，算不算程序写下的转换」。这五段原文各自都不含糊，而它们合起来并不给答案：

**转换是什么**（5.1，第 360 行）：

> **数值类型之间不做隐式转换。** 整数不会自动变成浮点，别的数值类型之间也一样（参数、返回值、变量之间都一样）；需要时显式写 `float(x)`。唯一会动的是**字面量**：它按上下文取类型（见本节末）。

**转换不合法的后果**（5.1，第 362 行）：

> **转换的故障**：`int('12abc')` 是故障，整个字符串必须是一个数，要安全地解析用 `parse_int`，它返回 `int!`；值放不进目标类型的转换是故障：`int(1e30)`、`int(nan)`、`i32(5000000000)`。

**例外只有「装入」**（5.8，第 482 行）：

> **泛型类型一律不变**：`Array<int>` 不能当 `Array<int?>` 用；隐式转换（如装入 `T?`）只发生在叶子值上，不穿透泛型类型的参数。

**故障只从故障源产生**（3.9，第 253 行）：

> **故障只从故障源产生**：声明了 `panic` 的算子（除零、越界，以及 `fail` 这个内建算子），和运行时的资源极限（递归过深、内存耗尽）。函数没有自己的 panic 机制；函数层可恢复的失败只有错误值 `T!`。

**编译期与运行期的分界**（3.9，第 267–268 行，两段方向相反、各管一类）：

> **必然出故障的常量表达式**（如 `1 / 0`）：折叠遍不折叠它，给一条带位置的警告，留到运行时。这是折叠遍自己的选择，不是对所有遍的约束（见 9.6）。

> **越界**：编译期能知道长度时（尤其是固定长度的类型），`slice` 与下标的越界应当是编译期错误，而不是悄悄截断或留到运行期。运行期的 `slice` 越界是故障，与下标越界一致；

**不许为了让检查变绿改语义**（第 14 节第 10 条，第 922 行）：

> 10. 为了让某个测试通过而改变语义。——先确认语义，再判断是测试错了还是实现错了。

以及它上面那句规范性的话（第 3 行）：

> 它是规范性的：当实现、其他文档或某次测试结果与本文档冲突时，以本文档为准，冲突之处应作为缺陷或待办记录下来，而不是据此修改语义。

读法：3.9 第 253 行说**故障源是列得完的一张表**；编译器自动插的转换不在表里。若它现在会 fault，
程序就多了一个它没写过的故障源——这不是「转换的故障」（第 362 行说的是程序要求的转换），而是
「编译器替程序做了一个会失败的决定」。反过来，3.9 第 268 行说编译期能知道的就该在编译期拒绝：
这一句支持候选 ①。**两段都在这页的射程里，所以这一页不能由一个读数裁掉。**

而且这个问题**已经有半页挂在第 15 节**（第 943–945 行），同一个形状：

> **内建算子的实参是否自动装入**（`entering-operator-args`）：`xs->append<5>`（`xs: (int?)[]`）。
> 现在的实现：不装入，要写 `5 as int?`；因为内建算子的形参类型由 C++ 的类型规则从实参算出，调用之前没有目标类型。
> 候选：让类型规则能报出"我接受的类型"，使内建算子与用户函数一致。

那一页问「要不要替用户插装入」，这一页问「插了之后遇到非 Safe 配对怎么办」。**同一个决定的两半，分在两条页上，
应该由同一次裁决回答。**

## 两条读数

| | 读数 | 出处 |
| --- | --- | --- |
| ① (b) 的好处 | **转绿 57 条**（`camel_passes` 13、`algebra_rules` 9、`modules.rules` 8、`modules.graph` 8，其余未拆），**另清掉 40 条库图红** | `C-1124-426`（整层读数 `733d613058`）；按计划的拆解是协调者给的，我没有复算 |
| ① 附带的一个读数 | `opt/generic` **121 / 99 / 22**，无新红 | 车道的自测。**它不能当「(b) 单独」的证据**：车道的尖端 `archive/2026-11-25-gir-opertarget` 已含 `61bab72902`（`git merge-base --is-ancestor` = 真），所以这是「(a)+(b) 但只跑了一片计划」 |
| ② (a)+(b) 的代价 | **`9559 / 8367 / 1141 / skip 21 / known 30`**，新增 333，其中 **332 条同一句** `Invalid argument of 'cast': a value of type '…' does not convert to '…' by this call`（`compile.collections` 62 / `compile.tagged` 32 / `examples` 30 / `consistency.entering` 24 / `tutorial.examples` 20 / `compile.extent_expressions` 16 / `modules.random` 12 / …）。退出码未取到 | `C-1124-426` |

归因：**(a) 只会在原来不被拒绝的地方加一次拒绝，它不可能让任何一条从红转绿**（这一条是单调的：它只增加 fault，不撤任何 fault），所以上面那 57 条转绿
只能是 (b) 带来的；反过来那 332 条只能是 (a) 带来的。两个数都要写，只写一个就是把一笔交易的半边价码藏起来。

## 今天树上的实际行为（量出来的，不是推的）

座位头 `efc1d65cbd`（= `9e34fcca2d` + revert，`61bab72902` 已被撤）与车道头 `105d813e84`（含 (a)）各有一个
干净构建，同一份用例、同一条命令：

| 树 | 构建 | `compile.tagged/combined.cml` |
| --- | --- | --- |
| 座位（**revert 之后**） | `7a615af572`，`dirty=False`；`git diff --stat efc1d65cbd 7a615af572 -- src/` **空** | **`exit 0`**，十行输出全对：`anonymous / 1 / KeyError / -1 / 5 / null / error IOError: disk / 2 / 9 / 4` |
| 车道（(a)+(b)） | `105d813e84`，`builtAt 09:32` | **`exit 1`**：`error[RuntimeDiag::InvalidArgument]: Invalid argument of 'cast': a value of type 'void!' does not convert to 'int?!' by this call.` 指向 `13:21` |

`std::gir` 下那个转换印成（座位构建，同一份用例）：

```
N1A [label=":op/cast", type="OPER", origin=8918, span="12:20-12:48",
     srcfile="…/test/cases/compile/tagged/combined.cml", value_type="int?!"];
```

- **`label=":op/cast"` + `type="OPER"`**，不是 `label="CAST"`：全图 12 个 `:op/cast`、**0 个 `CAST`**（`CAST` 这个种类
  在 `af3c6c9bdd` 删了，视图改印算子的 URI）。这一条与 (b) 无关地成立，所以裁决不会把它推回去。
- `span="12:20-12:48"` 与诊断的 `13:21` 是**同一处**：GIR 的 span 行号从 0 起算（旁证：`N20 … span="14:30-14:42"
  value_type="int?"` 正是第 15 行的 `null as int?`）。

**一条我没解开的读数，记在这里不当作结论**：`Type::castSlotFrom` 的 GUARD（`base.cpp` 的 `castSlotFrom`，
`castSafetyFrom(sourceType) == CastSafety::Safe`）在每种构建里都活着（`GUARD` 在 `NDEBUG` 下不消失），而 (a) 的检查
对这对配对判的正是「非 Safe」；那么座位这条路要么没走到那个 GUARD、要么 `castSlotFrom` 对它不设防。座位这条用例
既不抛也不中止，**说明座位与车道走的不是同一条执行路径**；是哪一条，我没有量出来（不加日志量不出来）。这不改变
裁决，但改变另一个判断：**「回到旧行为」不等于「回到一个已理解的行为」。**

## 我的默认（协调者）：裁决前保持旧行为

**座位停在 `efc1d65cbd`，`61bab72902` 不回来，(b) 留着，不写第三条改动。** 理由：两半都对的时候，
最小的落点是撤掉「让它们开始互相打」的那一笔；而 (b) 单独带来 57 条转绿、清掉 40 条库图红，撤整条会把对的也撤掉。

配套的三条，都在这一页之外执行：

- **不新增断言、不改 golden**：在裁决之前把答案写进树里，等于用一次提交替作者回答。
- **332 条现在回到绿是旧行为的结果，不是「已修」**：它现在是绿的，而它为什么会绿没人说得清（上面那条）。
- **重新跑一次整层**（不是 `opt/generic` 一片）：332 条是否全部转绿、还有没有第二条红，要一个整层读数才算数。

## 裁决会影响什么

**用例**

- 那 332 条：`compile.collections` 62、`compile.tagged` 32、`examples` 30、`consistency.entering` 24、
  `tutorial.examples` 20、`compile.extent_expressions` 16、`modules.random` 12，其余散在别的计划里。
  裁决 ①（编译期拒绝）会把它们从「运行时 fault」变成「编译期诊断」，golden 与期望串都要重写；
  裁决 ②（运行时 fault）就是现在 (a) 的形状，一条也不用改；裁决 ③（不插）要让它们照旧运行。
- 那 57 条与 40 条库图红：任何撤掉 (b) 的方向都会把它们退回去（`feat/opt/camel_passes.plan.toml`、
  `feat/opt/algebra_rules.plan.toml`、`modules.rules`、`modules.graph` 四个计划里）。
- 一条**现在没有**的用例：今天没有任何一条断言钉住「编译器自动插的转换遇到非 Safe 配对会怎样」。
  不管裁到哪一边，都该有一条钉住它；裁决之前写不了。

**golden**

- `test/plans/feat/compile/golden/tagged_null_then.dot`、`test/plans/feat/consistency/golden/entering_graph.dot`、
  `test/plans/feat/consistency/golden/narrowing_graph.dot`：三份 golden 里已经有 `:op/cast` 节点，
  裁决 ①/③ 会改它们的形状或数量。
- `test/plans/feat/opt/generic.plan.toml` 里数 `label=":op/cast"` 与 `type="OPER"` 的那几行（`fold_cast.*`）：
  数字是常量转换的处数，裁决不改它；但若裁到「不插这个转换」，这几个数字先动。

**`docs/design-principles.cn.md` 的段落**（按「冲突记成缺陷或待办，不据此改语义」）

- 5.1（第 360、362、364 行）：转换是显式的、转换不合法的后果、转成自己的类型什么也不做——三句都要在
  「编译器自动插的那种」上落到一句。
- 3.9（第 243、253 行）：错误与故障是两回事、故障只从故障源产生——若判「运行时 fault」，故障源那张表要加一项
  （或加一句「编译器插入的转换算程序要求的转换」）；若判「编译期拒绝」，这两句不用动。
- 3.9（第 267–268 行）：编译期与运行期的分界，判 ① 时它是正面依据。
- 5.8（第 482 行）：隐式转换只发生在叶子值上——「装入 `T?`/`T!`」到底走不走转换表，这句话要补。
- 第 14 节第 10 条（第 922 行）与第 3 行：任何方向都要过这两句，尤其「332 条转绿」不能自己成为理由。
- 第 15 节 `entering-operator-args`（第 943–945 行）：与这一页是同一个决定的两半，应同次裁决。

## 怎么量的（可复现）

```powershell
$env:CAMEL_HOME="D:\Projects\Camel-wt\graph-ir-prose\out\latest"   # 座位头的干净构建
& "$env:CAMEL_HOME\bin\camel.exe" test\cases\compile\tagged\combined.cml            # exit 0
& "$env:CAMEL_HOME\bin\camel.exe" test\cases\compile\tagged\combined.cml std::gir  # 12 个 :op/cast、0 个 CAST
```

- 座位的构建取自 `D:\Projects\Camel-wt\graph-ir-prose\out\latest`：`build-info.json` 记 `revision=7a615af572`、
  `dirty=False`，而 `git diff --stat efc1d65cbd 7a615af572` 只有 `docs/technical/05_graph_ir.md` 与 `test/vars.toml`
  两处文档/测试配置，**`src/` 无差异** ⇒ 它的代码就是 `efc1d65cbd` 的代码。（「量的那棵不是要写的那棵」这个坑，
  今晚已经踩过两次，所以这一段的每个数都写了它来自哪棵树的哪个构建。）
- 车道的构建取自 `D:\Projects\Camel-wt\opertarget\out\latest`，`revision=105d813e84`、`dirty=False`。
- 本次**没有**跑测试、没有构建、没有改代码；332 条里我只复现了 `compile.tagged/combined.cml` 这一条，
  其余 331 条是同一句、同一行被 revert 删掉后不再产生，整层是否全绿要等一次整层跑。