# 第 6 项：规则互相覆盖的普查（`stdlib/rules.cml`，37 条规则两两）

**本机不编译。** 读数取自宿主构建：commit `2579c9c487`（clean），1344 源文件，指纹 `0244fca0cbac`，
选项 `09afb0dfe7d8`（defaults），`BUILD_EXIT=0`，`BUILD_SECONDS=2.6`；计划冻结在
`internal/reports/eval-round10/probes/overlap_census.plan.toml`（不在 `test/` 下，见第 5 节）。

**这一页只测量，不含裁定。** 判据问题见 `internal/questions/2026-11-19-rule-overlap-is-judged-by-value-or-by-writing.cn.md`。

## 0. 三个数

| 数 | 值 |
| --- | --- |
| 重叠对总数（两条规则能同时命中同一个节点） | **18** |
| 其中值相同 | **18** |
| 其中值不同 | **0** |

18 对里，**两侧都真的改写了见证程序**的 12 对；**只有一侧改写**的 2 对；**两侧都没改写**的 4 对（见第 4 节，
那 4 对的"相同"是空的，不能当读数用）。全部 18 对共比较 **245 次读数**（`int` 域 12 值 ×7 对、`float` 域 19 值 ×7 对、
`f32[]` 域 7 值 ×4 对），**无一处不同**。

## 1. 普查怎么分的桶，以及为什么不是按参数类型分桶

引擎派发匹配是按**算子 id**（`src/runtime/writings_driver.cpp#DraftWritings::saturate` 里的 `byOper`），
不是按参数类型：`float_combine`（`(T, T, T)`）与 `float_double`（`(T, T)`）**参数表不同，却能命中同一个节点**。
所以桶是 **head + arity**，不是 head + arity + 参数类型（后者是 `DuplicateRuleForOperator` 的重复判据，不是重叠判据）。

37 条按 head+arity 落桶：`+`/2 有 18 条、`*`/2 有 8 条、`/`/2 有 3 条、`-`/2 有 4 条、``-``/1（取负）2 条、`log`/1 有 2 条。

桶内两两做**左式合一**（一阶合一，每条的变量各自新鲜，`const` 形参只能绑常量节点，数组形状的变量不能绑标量常量）。
两个筛子，依次是：

| 筛 | 去掉 | 剩 |
| --- | --- | --- |
| 形状可合一（两侧的左式能指向同一个节点） | 37 条 666 对里去掉绝大部分 | **26** |
| 守卫不能同时成立 | 8 对（下表） | **18** |

"形状"这一筛本身带两条结构性约束，各去掉过一些对，举一个例子：数组形状的变量不能绑标量常量，否则
`x + x` 会在 `x` 是数组时被当成可以绑 `-0.0`——`array_add_self × array_add_neg_zero`、
`array_add_neg_zero × array_combine_one`、`array_add_neg_zero × array_combine_one_mixed` 这三对就是这么去掉的
（它们不在上面那 26 对里，也不在下面的动态普查里）。

被"守卫不能同时成立"去掉的 8 对，理由是具体的，不是"跳过"：

| 对 | 为什么不能同时成立 |
| --- | --- |
| `float_combine × float_combine_mixed` | 同一对系数，`combine_same_sign` 要同号、`combine_mixed_sign` 要异号 |
| `array_combine × array_combine_mixed` | 同上，逐元素 |
| `add_self × float_combine_mixed` | `add_self` 的左式 `x + x` 逼两个加数相同 ⇒ `c1 = c2`，异号不成立 |
| `array_add_self × array_combine_mixed` | 同上 |
| `float_combine_mixed × float_double` | `double` 逼 `c1 = c2 = c`，异号 ⇒ `c = -c = 0`，而两条都要求 `c != 0` |
| `array_combine_mixed × array_double` | 同上 |
| `float_combine_one × float_combine_one_mixed` | `c > 0` 与 `c < 0` |
| `array_combine_one × array_combine_one_mixed` | 同上 |

## 2. 18 对重叠对的清单

`节点形状` 是合一器给出的公共节点，`_x` 是规则变量，`'c'` 是 `const` 形参。`右式写法` 逐字取自 `stdlib/rules.cml`。
`n=源/A/B` 是抽取后程序的节点数：某侧与源不同，就是那条规则确实改写了程序。

| # | 对 | 公共节点形状 | A 的右式 | B 的右式 | n=源/A/B | differ |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `mul_one × int_fold` | `(x*'c1') * 1` | `x` | `x * (c1 * c2)` | 5/3/3 | 0 |
| 2 | `sub_zero × int_sub_self` | `0 - 0` | `x` | `0 as T` | 3/3/3 | 0 |
| 3 | `add_self × int_add_zero` | `0 + 0` | `x * 2` | `x` | 3/3/1 | 0 |
| 4 | `add_self × int_combine` | `(x*'c2') + (x*'c2')` | `x * 2` | `x * (c1 + c2)` | 6/4/3 | 0 |
| 5 | `int_add_zero × int_fold_add` | `(x + 'c1') + 0` | `x` | `x + (c1 + c2)` | 5/3/3 | 0 |
| 6 | `int_add_zero × int_combine_one` | `(0 * 'c') + 0` | `x` | `x * (c + 1)` | 3/1/3 | 0 |
| 7 | `int_mul_zero × int_fold` | `(x*'c1') * 0` | `0 as T` | `x * (c1 * c2)` | 5/2/3 | 0 |
| 8 | `mul_one × float_fold` | `(x*'c1') * 1` | `x` | `x * (c1 * c2)` | 5/3/3 | 0 |
| 9 | `add_self × float_add_neg_zero` | `(-0.0) + (-0.0)` | `x * 2` | `x` | 3/3/3 | 0 |
| 10 | `add_self × float_combine` | `(x*'c2') + (x*'c2')` | `x * 2` | `x * (c1 + c2)` | 6/5/3 | 0 |
| 11 | `add_self × float_double` | `(x*'c') + (x*'c')` | `x * 2` | `x * (c + c)` | 6/5/3 | 0 |
| 12 | `float_combine × float_double` | `(x*'c') + (x*'c')` | `x * (c1 + c2)` | `x * (c + c)` | 6/3/3 | 0 |
| 13 | `float_add_neg_zero × float_combine_one` | `((-0.0)*'c') + (-0.0)` | `x` | `x * (c + 1.0)` | 3/3/3 | 0 |
| 14 | `float_add_neg_zero × float_combine_one_mixed` | `((-0.0)*'c') + (-0.0)` | `x` | `x * (c + 1.0)` | 3/3/3 | 0 |
| 15 | `array_mul_one × array_fold` | `(x*'c1') * 1` | `x` | `x * (c1 * c2)` | 5/3/3 | 0 |
| 16 | `array_add_self × array_combine` | `(x*'c2') + (x*'c2')` | `x * 2` | `x * (c1 + c2)` | 4/5/3 | 0 |
| 17 | `array_add_self × array_double` | `(x*'c') + (x*'c')` | `x * 2` | `x * (c + c)` | 4/5/3 | 0 |
| 18 | `array_combine × array_double` | `(x*'c') + (x*'c')` | `x * (c1 + c2)` | `x * (c + c)` | 4/3/3 | 0 |

读这两行就够看出"值同、写法不同"是常态：#4 的 `x * 2` 与 `x * (c1 + c2)` 在 `c1 == c2` 时是同值异写；
#11/#12 的 `x * 2` 与 `x * (c + c)` 也是；#1/#8/#15 的 `x` 与 `x * (c1 * c2)` 在 `c1 * c2 == 1` 时也是。

## 3. 值怎么判的（可操作版本）

对每一对：写**一个**见证程序，让它含那一对共有的节点；用 `saturate<[A]>` 饱和一次、`extract<nodes>` 取最省写法，
再用 `saturate<[B]>` + `extract<nodes>` 做同样的事；然后在**同一解释器**上把两份程序在同一有限定义域上跑一遍，
按**读数的文本**（设计原则 7.7 的"最短可读回同一值"）逐点比较。`+0.0` 与 `-0.0` 是两个读数，每个 NaN 是一个读数。
定义域取库自己那份用例的边界与特殊值：`int` 12 个（两端、回绕点、0、±1、±2、±3、±5、±1024）、
`float` 19 个（两个零、±1、±0.5、±2、±0.1、`2^52`、`1e300`、`5e-324`、两个无穷、NaN）、
`f32[]` 7 个（空、`[0.0]`、`[-0.0]`、`[1.0,-0.0,2.0]`、`[-1.0,0.0,-2.0]`、`[3.4e38,-3.4e38,1.4e-45]`、`[0.1,-0.1,8388608.0]`）。

**仪器必须是 `saturate`+`extract`，不能是 `run`。** 这一点是量出来的，不是猜的：第一版用贪心 `run`，结果
`add_self` 在 `(x*2.0)+(x*2.0)` 上一次都没命中——因为贪心路径不共享相同子式（`src/runtime/writings.cpp#DraftWritings::lookupOrAdd`
的 hashcons 在 `greedy_` 时整支跳过），写成两遍的 `x*2.0` 是**两个**节点，`x + x` 便无从匹配。
换成饱和（非贪心，会共享）之后，同一对两侧都命中。**这本身是一个结论：这 18 对里有一半只在饱和下成立，在 `simplify` 那种贪心路径下根本不会发生。**

许可写在**代码**上（设计原则 7.5），所以见证函数按规则的要求标注：`float_fold`/`array_fold` 要
`#[reassociate, no_infs]`，`float_combine*`/`array_combine*` 要 `#[reassociate]`，`*_mixed` 再加 `no_signed_zeros`。
不标它，那条规则就静默地不触发（第一版就这样把 `float_fold`、`array_fold` 漏成了 `B-fires=0`）。

## 4. 没能触达的 4 对，以及为什么

`sub_zero × int_sub_self`、`add_self × float_add_neg_zero`、`float_add_neg_zero × float_combine_one`、
`float_add_neg_zero × float_combine_one_mixed` 这 4 对，两侧规则**一次都没有改写程序**（`n` 三列相同）。
原因是它们共有的节点只由字面常量构成（`0 - 0`、`(-0.0) + (-0.0)`、`((-0.0)*2.0) + (-0.0)`），
前端在任何一个 pass 看到之前就把它折掉了。**所以这 4 对的"值相同"是空的**，我把它们照实列出来，不拿它当读数。
另外 `add_self × int_add_zero`（只有 `int_add_zero` 动手）与 `int_add_zero × int_combine_one`（只有 `int_add_zero` 动手）
是"一侧改写了、另一侧没有"，它们证明那一侧真的换了写法，但没有证明两侧同时对同一个节点动手。

要真正触达这 4 对，得让节点活到 pass 之前（例如把常量做成参数、或让前端不折常量），那是另一件事，本页没有做。

## 5. 复现

见证程序与驱动冻结在 `internal/reports/eval-round10/probes/overlap_census.cml`，计划冻结在旁边
`overlap_census.plan.toml`。两处都**不在 `test/` 下**：这份计划是故意跑红的（下段），放进 `test/plans/`
就会把 `feat/**` 整套计划带红。要再跑一次，把两份拷回它们认得自己的位置——

```
copy probes\overlap_census.cml           test\cases\modules\rules\
copy probes\overlap_census.plan.toml    test\plans\feat\modules\
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <tree> -Plan test/plans/feat/modules/overlap_census.plan.toml -Jobs 8
```

计划里的 `case = "../../../cases/modules/rules/overlap_census.cml"` 是相对 `test/plans/feat/modules/` 写的，
所以拷回去后不用改。`main` 故意返回 1：只有计划跑红，运行器才把每对那一行打出来；一页 18 行又超过它一次显示的行数，
所以整数那 7 对与浮点/数组那 11 对是**两次运行**分出来的读数（第二次把浮点与数组的调用暂时摘掉再跑）。
两半的行数、`n=` 与 `differ=` 都已并回上面第 2 节那张表，表里的数是两次运行的读数合起来算的。

## 6. 这份普查能支持什么、不能支持什么

- **能支持**：在今天的规则库里，18 对能同时命中同一个节点的规则，在这两套有限定义域上给出**同一个值**。
- **不能支持**：它不证明这 18 对到处同值（有限定义域，且 4 对没触达）；也不证明不存在第 19 对
  （合一器只读左式，`when` 的语义只按守卫的定义域粗判，见第 1 节）。
- **它说的最要紧的一句**：**18 对里 0 对值不同。** 所以"两条规则同时命中"今天不是缺陷；
  要立检查，按**值**立的检查今天会是绿的，按**写法**立的检查今天会把 18 对全部报成红——那 18 对都不是错误。