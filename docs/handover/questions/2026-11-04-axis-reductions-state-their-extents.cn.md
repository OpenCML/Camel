# 轴归约的结果类型：把声明退掉交给推导，还是留着声明另开一条路？

**状态**：待裁；我方默认见第 3 节（**甲：退掉六个 `*_axis` 声明，让算子的推导表回答**），并**已按甲的默认开工**。

写这页的原因有两条，第一条是读数冲突，第二条是我动了任务书 Allow 清单以外的一个文件。

1. **任务书说 `transpose_needs_parameter_shape` 的两条"必须仍然 PASS（lane-extentof 刚修好的）"，但在实时 `origin/develop`（`34b659107`）上它们是 `known_fail`。** `lane-extentof` 的 tip `719fc2baf` 不在 `develop` 里（`git merge-base --is-ancestor` 为假，`git branch --contains` 只有 `lane-extentof` 自己），所以那两条在今天的主线上是 XFAIL，不是 PASS。按 `test/plans/feat/compile/rules_angle_args.plan.toml` 的实测：`total: 8, pass: 4, fail: 0, known failures: 4`。**"仍然 PASS"这件事只可能在合了 `lane-extentof` 的树上成立**，我这棵树单独不可能让它成立（见第 1 节第 3 条）。
2. **让 `mean_axis` 带上形状，唯一要动的地方是 `stdlib/tensor.cml` 里的一行声明**，而任务书的 Allow 清单只写了 `modules/tensor/**`、`test/`、`docs/`，Forbid 清单里没有 `stdlib/tensor.cml`（只点了 `stdlib/rulesets/*.cml`）。我按"`stdlib/tensor.cml` 是 tensor 模块的签名文件"处理了——`modules/tensor/module.cpp` 自己的注释就是这么写的（"The declaration is the module's signature"）——但这是判断，得让作者看见。

## 1. 读数（实测，含树、时间、负载）

**改前**：宿主 88zjwei 上的检出 `fwdclosure`，它**正好建在 `34b659107`**（我的基线），二进制 `2026-11-03T15:33:32-05:00`；本机 2026-11-04 04:28，load 49。读法 `camel probe.cml std::gir`，取每个 `type="OPER"` 节点的 `value_type`，操作数一律 `x: float[2, 3]`：

| 算子 | 改前 | 改后 |
| --- | --- | --- |
| `tensor:mean_axis`（axis 0，keepdims false） | **`float[..]`** | `float[3]` |
| `tensor:mean_axis`（axis 0，keepdims true） | **`float[..]`** | `float[1, 3]` |
| `tensor:multiply`（`x * x`） | `float[2, 3]` | `float[2, 3]` |
| `tensor:add` | `float[2, 3]` | `float[2, 3]` |
| `tensor:divide` | `float[2, 3]` | `float[2, 3]` |
| `tensor:sum`（整体归约） | `float` | `float` |
| `tensor:transpose` | `float[3, 2]` | `float[3, 2]` |
| `tensor:reshape<[3, 2]>` | `float[3, 2]` | `float[3, 2]` |
| `mean_axis(x, 0, false) * w`（`w: float[3]`） | **`float[..]`** | `float[3]` |

**表里最重要的一行不是 `mean_axis`，是 `multiply`：它本来就带形状。** 任务书假设"两条用例卡在 `mean_axis` 与 `multiply` 两处"，但量下来只有一处——`multiply` 的推导是 `elementwise_ops.cpp#binaryInfer` 的 `broadcast(lhs.shape, rhs.shape)`，它把操作数的形状原样传下去，所以 `mean_axis(x,0,false) * w` 读成 `float[..]` 不是 `multiply` 抹平的，是它的左操作数**本来就没有** extents。改掉 `mean_axis`，这一行自己就跟着好了。

**改前的 `float[..]` 不是"没算"，也不是"算不出来"，是"算出来了但没人问"**。`modules/tensor/ops/reduction_ops.cpp#reduceAxisInfer` 一直在算：

```
const TensorFacts in = ctx.facts(0);
const bool keepDims  = ctx.has(2) ? ctx.constBool(2).value_or(false) : false;
return tensorOf(dtype, reduceShape(in.shape, ctx.constInt(1), keepDims));
```

`support.cpp#reduceShape` 会把被归约的那根轴从操作数形状里去掉（`keepdims` 时留成 1）。但在 `34b659107` 上这条推导**没有一次被调用过**，因为六个轴归约都同时出现在两处：

- `stdlib/tensor.cml` 的 `export func mean_axis::<T>(xs: T[..], axis: int, keepdims: bool): T[..]`——一个**声明**；
- `modules/tensor/module.cpp#kDeclaredOperators` 的 `"mean_axis"`——它的意思是"这个名字由声明固定，模块不导出 C++ 的算子组"。

而 `src/core/module/module.cpp#Module::inheritExportsFrom` 与 `replaceTypeQualifiedAliases` 的注释把优先级写死了："A declaration this module holds is the signature of its operator"。**声明赢**，于是 `T[..]` 就是节点的类型，推导表从不发言。两条读数钉住这一点：

- `mean_axis<0, false>(x)`（角括号形式，正是 `reduceAxisInfer` 的调用形式）被拒：`NoMatchingFunction ... Available overloads: <tensor:mean_axis>: (xs: T[..], axis: int, keepdims: bool) => T[..]`——**能回答的推导被声明挡住了，连候选都不是**。
- 六个 `*_axis` 全是 `float[..]`，一个不差（`sum_axis`、`mean_axis`、`max_axis`、`min_axis` 实测同表）。

**声明为什么只能写 `T[..]`**：结果的外延取决于 `axis` 和 `keepdims` 的**值**，而声明的结果是一个**类型**，不是两个值的函数。`stdlib/tensor.cml` 自己的头注释早写过这句话："What a declaration still cannot state is a signature that depends on a *value* argument."

3. **两条 `mean_axis_transpose` 不可能在我这棵树上转正，缺的那一半在 `lane-extentof` 手里。** 今天它的拒绝是 `skip=rule uri=tensor:sum reason=unbound`：`stdlib/rulesets/transpose.cml` 里没有一条 `sum` 的反向律（`sumshape` 那轮把 `sum_t` 换成了 `*_stated` 双胞胎）。`lane-extentof` 的 `sum_from_type_t::<T, X, A>(t: X, at: A)` 就是补这一条的律，而 `A` 这个类型参数要绑定的是 `extent_of::<X>()` 物化出来的 `int[]`——**`X` 必须带 extents，否则那条律拿到的是 `float[..]`，`extent_of` 无从下手**。所以两件事是一对：**律在人家车里，extents 在我车里**。我在 `D:\Projects\Camel-wt\statetypes-extentof` 那棵一次性验证树上同时叠了 `lane-extentof` 与我这棵树，实跑结果见第 1.1 节；**这两条不在我这棵树上，我不撤它们的 `known_fail` 标记**。

### 1.1 这一刀切下去露出来的一个洞：秩 0 的余切被当成数组读（SIGSEGV）

带上诚实类型之后，`vjp` 一条"秩 1 张量上归约唯一那根轴"的函数会**稳定段错误**（8/8 次，rc=139），gdb 的帧是：

```
Program received signal SIGSEGV
#0  camel::tensor::ops::(anonymous namespace)::numelKernel(ArgsView&, ArgsView&, ...)   tensor.cmo
#1  NodeVMSchedPass::evalNativeOperator(...)
...
TRANSPOSE function=split_of_jvp_of_total_backward graph=346 type=(float) => (float[],) rhs=mean_axis_stated_t
```

原因是一条反向律的端口类型和诚实类型对不上：`stdlib/rulesets/transpose.cml#mean_axis_stated_t` 写的是

```
mean_axis_stated(t, at) => (ct: float[..]) => mean_axis_cotangent(ct, at)
```

`ct: float[..]` 是**数组**。而 `mean_axis(x, 0, false)` 在秩 1 上的答案是**那个元素**（`float`，5.4：秩零数组就是它的元素）。值是 double，类型说是指针：`mean_axis_cotangent` 里的 `numel(ct)` 于是把 double 的位当成 `Array*` 读，段在 `numelKernel`。改前不炸，只因为改前 `mean_axis` 的类型是 `T[..]`——**数组**，`wrap` 就把它保持成秩零数组，谁都没读错。**所以这不是我改坏的地方，是我把一处"类型和值靠 `T[..]` 勉强对齐"的地方挪开了，露出来的洞。**

真正的修法在律那一侧（`stdlib/rulesets/transpose.cml`，这条车道的 Forbid 清单里）：`ct` 的端口应当写成元素类型（`ct: T` 或 `X`），`mean_axis_cotangent` 的第一句改成能接元素的形式（或在律里显式 `full`/`reshape` 成 `[1]`）。对照 `sum_stated_t` 那条同样的律它是不炸的——因为 `sum(x)` 从 `stdlib/signatures` 一路就是 `T`（元素），它的 `spread_over::<T>(ct: T, at)` 收的就是元素，包装/解包在那一层是对的。**也就是说：同一个"余切是标量"的情形，一条律支持、一条律不支持，差别只在端口怎么写的。**

**我这一轮怎么处理的**：`test/cases/compile/transforms/stated_mean_axis_gradient.cml` 改成 `sum(mean_axis(x, 0, true))`（保留那根轴，于是余切是 `float[1]` 而不是标量），读数一位不变（`value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]`），用例和 plan 的 `desc` 都写明了为什么保留、以及这条洞在哪。**我没有把 `mean_axis` 的类型退回 `T[..]` 去迁就那条律**——`T[..]` 是"不知道"，而这里知道。

**这条要请作者裁的一句**（可以和上面那句一起答，也可以分开）：`mean_axis_stated_t` 的余切端口该不该改成元素类型？我默认**该改**（和 `sum_stated_t` 对齐），并且这一改应当落在 `lab/transpose` 或谁手里能动 `stdlib/rulesets/transpose.cml` 的那条车上；在那之前，秩 0 的轴归约做 `vjp` 会段错误，这是一个**今天就能复现的洞**，不是"暂不支持"。

## 2. 两条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：退掉声明，交给推导** | 六个 `*_axis` 从 `kDeclaredOperators` 与 `stdlib/tensor.cml` 里去掉，改由 `OpRegistry::operatorGroups("tensor")` 导出；`reduceAxisDef` 的三个参数改成普通参数（`withAfterNorm = false`），这样括号写法直接落到推导；`stdlib/signatures/tensor.cml` 收下六条计算签名，`docs/stdlib-reference.md` 与 `docs/reference/camel-for-ai-stdlib-signatures.md` 用生成器重录 | **动到 `stdlib/tensor.cml`**（任务书 Allow 清单外、Forbid 清单内也没有）；六个名字的签名从"声明"变成"计算"，`docs/` 两个生成文件跟着变；`withAfterNorm` 从 true 变 false，`declareCallForm` 因此开始为这六个op 登记调用形状（`std::cml` 打印器因此从"拒绝"变成能写 `tensor::mean_axis(x, 0, false)`） |
| **乙：留着声明，另开一条不声明同名算子的路** | 比如给 `mean_axis` 加一个带角括号的第二拼写 `mean_axis<axis, keepdims>(x)`，声明留 `T[..]`，新拼写走推导 | 两条拼写对同一个算子给两个结果类型（`mean_axis(x,0,false)` 是 `float[..]`、`mean_axis<0,false>(x)` 是 `float[3]`），正是 `--verify-ops` 那条"one operator, two signatures"要防的形状；而且程序要改写法，`rules_angle_args` 的用例文件也得改 |
| **丙：什么都不动，把 `float[..]` 当终局** | 认下 `T[..]`，让 `extent_of` 那一路去猜 | 不行：`float[..]` 是"不知道"；靠猜长度就是任务书禁止的"看起来知道" |

第三条（今天先不做）：给 `kDeclaredOperators` 加一个"声明可以作为兜底、推导更精确时用推导"的机制。那是 `src/compile` 的改动，改的是**所有**声明算子的解析优先级（`shape`、`numel`、`pick_at` 等十个名字都受影响），不是这条车道能承担的面。

## 3. 我方默认

**甲。** 三条理由：

1. **推导已经在库里、且是对的**：`reduceAxisInfer` 与 `reduceShape` 是既有代码，`keepdims`、负轴、越界轴都已经处理过（越界抛 `ShapeError`）；我这轮只是把挡在它前面的声明退掉，没有新写一行形状算术。这比"另开一条拼写"便宜得多，也不会让一个算子有两个签名。
2. **判据要求的是"能算出来的写进类型、算不出来的老实留 `float[..]`"**：退掉声明之后，`mean_axis` 在 `axis`/`keepdims` 是常量时给出精确形状，在它们不是常量时**仍然**老实退化（轴是运行时的值→`T[?]` 只有秩；`keepdims` 是运行时的值→`T[..]`）。这三种答案都是同一条 `reduceShape` 给的，没有一处是猜的。
3. **`mean_axis_stated` 留着不改**，正好是"算不出来"那一类的样本：它的轴藏在运行时的整数列表里，没有调用点能说出秩，所以它继续声明、继续回 `T[..]`，并在 `docs` 里写明理由。

**要请作者同时裁一句**：`*_axis` 这六个名字从"声明"变成"计算"之后，`stdlib/array.cml` 里那六个**有函数体**的 `sum<axis>`/`mean<axis>`/… 还是 `T[..]`（函数体的返回类型同样不能是值的函数）。这一层不一致要不要这一轮一起收（把 `array.cml` 那六个也改成计算签名，代价是它们的外延只能在调用点推），还是留作下一轮？**我默认留作下一轮**：它不在判据里，且 `array::sum<0>(xs)` 今天本来就没有任何用例要求精确形状。

## 要作者答的一句

**「轴归约的结果外延只能由算子的推导表算出来，写不进声明；我把六个 `*_axis` 声明退掉、让 `OpRegistry` 的算子组回答（`stdlib/tensor.cml` 因此被改，它在任务书 Allow 清单外但不在 Forbid 里）。这一刀该不该切，以及 `stdlib/array.cml` 那六个体函数要不要同一轮收？我默认『切，`array.cml` 留作下一轮』，并已按此开工。」**