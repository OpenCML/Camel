# `extent_of` 从类型取值、把长度交给律（车道 `lane-extentof`）

## 一、`extent_of` 落在哪、取值路线是什么

- 规则的一处拼写仍是 `include/camel/core/type/composite/array.h#statedExtentsOf`：非数组没有，开区间（`float[]`、`float[..]`）没有。
- 类型侧：`src/builtin/operators/struct.cpp#extentOfResolver` 只答调用类型，类型里没有长度时按名拒绝（`SemanticDiag::ExtentOfNeedsStatedShape`）。
- **值侧这次补上了**：`src/compile/gir/builder_operators.cpp`（`Builder::visitLinkNode` 里 `targetOperator->uri() == ":op/extent_of"` 的结算点）在调用被建出来的地方把 `int[]` 物化成常量节点，程序拿到的是图里的列表，不是 OPER 节点的运行期读取。理由写在代码里：OPER 节点记录调用的实例化类型，但不记录写出来的类型实参，运行期没有东西可读。
- 于是 `src/builtin/operators/struct.cpp#__op_extent_of__` 的 `PassInternalError` 变成走不到的状态。

读数（宿主 `88zjwei`，lane `0bee06245` 之前的 `679270a1a`）：

```
[2, 3]
3
exit 0
```

红：本车道结算之前的 `2d0c3a930` 上同一个程序

```
error[RuntimeDiag::PassInternalError]: Pass 'calling `extent_of`' failed with an internal error: the extents of `A` are a fact of the type the call names, and an OPER node records the instantiated type of the call but not the type argument it was written with, so the value is settled where the call is built and there is nothing left for the run to read.
exit 1
```

（更早的基线 `c4391db57` 上连名字都没有：`SemanticDiag::UnresolvedReference: 'extent_of'`。）

## 二、把长度交给律：一条多出来的端口

`transpose` 走一条**值图**时手里没有 `at`，也没有切点。做法是：律比它的算子**多写一个端口**，那端口是 `extent_of::<X>()` 的答案，由走查读操作数的类型（`statedExtentsOf`）物化成 `int[]` 再递过去（`modules/transforms/transpose.cpp#statedExtentsNodeOf`）。

只在这一步**原本没有任何律能合上**时才递：先按操作数自己的类型问一次，问不到才按「操作数类型 + `int[]`」再问一次（`chooseTransposedRule` 的 `offeredOperands`）。所以已有的答案一个字都不变，放宽只能碰到原本无律可答的步。

命名 `*_from_type`：因为区分点是**长度从哪里来**，不是「给谁用」——`sum_stated_t` 读的是前向半写出来的 `at`（值），`sum_from_type_t` 读的是类型写下来的 extents。同族三个：`sum_from_type_t`、`take_from_type_t`、`mean_axis_from_type_t`，配套 `take_cotangent_from_type`、`mean_axis_cotangent_from_type`。它们都不读操作数本身（`t` 只是用来固定实例的端口），所以没有零载体、没有「读元素」。

`idx_arr_t` 也改成从类型取长度（`extent_product(at)`），`zeros_like(xs)` 那条读值的路没有了。`transpose(pick)`（`x: float[3]`）仍旧答 `([0.0, 1.0, 0.0])`，两端皆绿。

## 三、四个 known_fail：两个转正，两个仍然红，原因换了

- `transpose_needs_parameter_shape.{nvm,fvm}`：**PASS**，标记已删。同一支 `x: float[..]`，现在拒绝落在缺长度的那一步上，句子点名参数与类型。
- `rules_angle_args.mean_axis_transpose.{nvm,fvm}`：仍 `known_fail`，但读数换了，注释重写。

读数（宿主 `88zjwei`，`679270a1a`）：

```
# 转正的那支（x: float[..]）
error[RuntimeDiag::TransposeNeedsParameterShape]: The transpose of 'loss_mean' needs the shape of the parameter 'x', and the type 'float[..]' does not state it.
TRANSPOSE function=loss_mean skip=parameter-shape uri=tensor:sum node=6 param=0 param-type=float[..]
exit=1

# 仍红的那支（x: float[2, 3]）
TRANSPOSE function=loss_mean skip=rule-ranks uri=tensor:sum rule=sum_from_type_t node=6 position=0 ranks=(0) operands=1 reason=it takes 3 type parameter(s), and it was asked for 2
exit=1
```

为什么第二支还是红的——**不是律的问题，也不是走查的问题**。用 `std::gir` 量这支的图：

```
tensor:mean_axis  value_type="float[..]"
tensor:multiply   value_type="float[..]"
tensor:sum        value_type="float"
```

`x` 是 `float[2, 3]`，但 `mean_axis` 与 `multiply` 的结果类型都是 `float[..]`，所以 `sum` 归约的那个操作数**类型里本来就没有 extents**，任何律都拿不到长度。要修的是 `modules/tensor` 里这两个算子把结果的形状写进类型，那是别人的车道、也是一个决定。所以标记留着，但理由换成了量出来的这一条。

## 四、四条判据的读数

| 判据 | 读数 |
| --- | --- |
| `feat/compile`（基线 `c4391db57`：2243/2230/2红/11xfail） | **2249 / 2238 / 2 红 / 9 xfail**，红仍是 `transpose_loop_escape.{two_values_of_two_kinds,second_state_of_the_same_loop}.nvm`，无新红 |
| `feat/compile/transforms`（基线 107/100/2/5） | **111 / 104 / 2 红 / 5 xfail**，同两条红，无新红 |
| `feat/transforms/split` | **19 / 19**，exit 0 |
| 没有长度的类型 | 按名拒绝，见上；不读值、不编造长度 |
| 新用例先红后绿 | 见第五、六节 |
| `docs/stdlib-reference.md` | `node scripts/stdlib-reference.mjs --write`，539 条，+30 行 |
| `git diff -w` | `transpose.cpp` 248/68、`builder_operators.cpp` 31/0、`transpose.cml` 69/10；被删的都是就地替换的旧块（`shapeFromTypeOf` 拆成两个函数、`chooseTransposedRule` 的头部、拒绝块嵌进 `if (!fits)`），没有搬家式重排 |

三条 `catalog.*` 的红是我先引入的：`SemanticDiag::ExtentOfNeedsStatedShape` 是上一段车道加的码，没有用例按名点它。已在该用例的条目上加 `expect_diagnostic_name`，并 `--write` 重生成 `docs/diagnostics.md` 与 `diagnostics/examples.tsv`。

## 五、新用例（先红后绿）

宿主 `88zjwei`，红在 `extentof-base`（`c4391db57`），绿在 `lane-extentof`（`679270a1a`）。

`extent_of_answers_the_type.cml` —— 红见第一节（`PassInternalError`，在 `2d0c3a930` 上量），绿 `[2, 3]` / `3`，exit 0。

`transpose_of_a_sum_over_a_parameter.cml` —— `func total(xs: float[3]) = sum(xs)`，`transpose(total)(1.0)`：

红（`c4391db57`）：

```
TRANSPOSE function=total skip=rule uri=tensor:sum reason=unbound
error[RuntimeDiag::RuntimeError]: ... a step of `total` (node 1, operator `tensor:sum`) is one the transpose family `rulesets.transpose` cannot turn around: the walk found no rule key for that operator ...
```

绿（`679270a1a`）：

```
TRANSPOSE function=total graph=58 type=(float) => (float[3],) nodes=2 arms=0 rhs=sum_from_type_t
TRANSPOSE op signature=... transposed=(float) => (float[3],) graph=(float) => (float[3],) same=yes
([1.0, 1.0, 1.0])
```

## 六、没做的

- `mean_axis_transpose.{nvm,fvm}` 两支仍 `known_fail`，理由见第三节，要等 `modules/tensor` 把 `mean_axis`/`multiply` 的结果类型写上形状。
- `mul_tensor_t` 那条 `sum_to(ct * w, x)` 仍会读操作数的值：形状只有类型能说的时候是 `shapeFromTypeOf`（`extent_of` 的答案物化成 `tensor:zeros`）。这一条这次没动，它不是 reduction 家族，改它要连 `sum_to` 一起动。
