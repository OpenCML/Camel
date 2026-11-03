# 第 1 项后半截：删掉 `transpose` 的"按位置取参数值"，十张红各自缺什么

**问：把 `modules/transforms/transpose.cpp#forward` 里「闭包第 `at` 格就是第 `at` 个参数的值」那段摘掉，会复活哪些红、它们各自缺哪个值？**

出处：作者 2026-11-03 15:15 的裁定（后半截读到的东西都是残差，进 `lin` 的闭包）与 15:35 的催办（第 1 项排在第 1、2 位，测试设施推后）。这一页是**施工图**，不是完成：后半截要等 `withres` 车道把切点的捕获改完，`transpose` 侧才可能只按身份找值。

**这一轮零提交改动**：探针摘掉那段后跑完 redist，已 `git checkout --` 还原；树 `D:\Projects\Camel-wt\varord` 仍停在 `990e273cf`，`git status --porcelain` 为空。

## 1. 摘掉的是什么

`transpose.cpp` 里 `forwardOf[ref]` 的前两级是**按身份**找：`closurePortOf`（切点已经捕获的端口）与 `capturePortOf`（调用方给的捕获端口）。第三级才是按位置：参数 `at` 的值 = `closurePorts[at]`，并且**校验**该槽的类型等于 `funcType()->normTypeAt(at)`，不等就退回 `TRANSPOSE-NEEDS` 拒绝。摘掉的就是这一级。

## 2. 读数（`-Tier fast -Jobs 4`，同一棵树，摘前 / 摘后）

| 目标 | 摘前 | 摘后 |
| --- | --- | --- |
| `test/plans/feat/compile/transforms.plan.toml` | 87 = 75 过 / 2 红 / 1 known | **87 = 74 过 / 12 红 / 1 known** |
| `test/plans/feat/transforms/split.plan.toml` | 19 = 19 过 | 19 = 19 过（不受影响） |

> 口径提醒：本文与上一份报告里的 `feat/transforms` **是目录目标** `test/plans/feat/transforms`（`compositions` + `split`，共 **29 = 21 过 / 0 红 / 8 known**）；别的车道报的 87/89 是**另一个目标** `test/plans/feat/compile/transforms.plan.toml`。两个不是同一件事，别把 29 当成"transforms 只有 29 条"。

新增的 10 条红（两条 `transpose_loop_escape` 与本改动无关，摘前就红）：

`grad_array_no_int.fvm`、`grad_array_with_int.fvm`、`grad_array_int_parameter_first.nvm`、`grad_of_an_array_parameter.nvm`、`grad_array_one_element.nvm`、`grad_array_two_elements_first.nvm`、`grad_array_sum.nvm`、`grad_array_product.nvm`、`grad_of_a_division.nvm`、`transpose_hand_closed_lambda.same_answer_as_the_cut.nvm`。

## 3. 十张红各自缺什么（逐条实测报文）

| 用例 | 被转置的函数 | `TRANSPOSE-NEEDS` | 跳过时说的是 | 这一段要的那个值 |
| --- | --- | --- | --- | --- |
| `grad_array_no_int.fvm` | `split_of_jvp_of_both_backward` | `param=0` | `skip=call-value-missing param=0 node=9 arg=5` | 参数 0 的原值（`both` 的第一个数组） |
| `grad_array_with_int.fvm` | `split_of_jvp_of_mixed_backward` | `param=0` | `param=0 node=8 arg=5` | 同上，`mixed` 的数组参数 |
| `grad_array_int_parameter_first.nvm` | `split_of_jvp_of_read_at_backward` | **`param=1`** | **`param=0`** `node=3 arg=2` | 参数 1 的原值；**skip 行说 0** ——这就是"端口下标被当参数下标"的现场 |
| `grad_of_an_array_parameter.nvm` | `split_of_jvp_of_g_backward` | `param=0` | `param=0 node=9 arg=5` | 数组参数原值 |
| `grad_array_one_element.nvm` | `split_of_jvp_of_g_backward` | `param=0` | `param=0 node=4 arg=2` | 同上 |
| `grad_array_two_elements_first.nvm` | `split_of_jvp_of_g_backward` | `param=0` | `param=0 node=4 arg=2` | 同上 |
| `grad_array_sum.nvm` | `split_of_jvp_of_g_backward` | `param=0` | `param=0 node=9 arg=5` | 同上 |
| `grad_array_product.nvm` | `split_of_jvp_of_g_backward` | `param=0` | `param=0 node=9 arg=5` | 同上 |
| `grad_of_a_division.nvm` | `split_of_jvp_of___g_250___backward` | `param=0` | `param=3 node=6 arg=3` | 参数 0 的原值，而调用少的是第 3 个实参 |
| `transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` | `transpose_of___g_242__` | （不走这条拒绝） | `Runtime indirect call norm-arity mismatch: caller 'main' node 21 has 1 args, target 'transpose_of___g_242__' has 2 norm ports` | 闭包多出一个端口，调用方只给 1 个实参 |

读法（三组形状）：

1. **七条是"参数原值没进闭包"**（`param=0`、`arg=5`/`arg=2`）——`transpose` 找不到就拒绝；按位置找之所以"能过"，是因为切点把参数的值按位置摆进了闭包，`transpose` 照着同一套位置约定去取。
2. **`grad_array_int_parameter_first` 是这套约定的自证**：`TRANSPOSE-NEEDS param=1` 与 `skip param=0` 说的**不是同一个参数**——一个端口下标被当成了参数下标。这正是第 1 项要删掉的东西。
3. **`transpose_hand_closed_lambda` 不是拒绝而是错数形状**：闭包有 2 个 norm 端口，调用方只给 1 个实参。位置约定一旦去掉而切点仍按老样子摆闭包，就会长出这样一个"多一个端口"的图。

## 4. 在 `ce2691f54`（`withres` 的切点捕获）上再量一次：**一条都没有自己变绿**

`git merge-base --is-ancestor ce2691f54 origin/develop` → **exit 1，它不在 develop 上**。于是按指示基于那个 sha 建树 `D:\Projects\Camel-wt\pos1`（`lane-pos1`），在它上面做同一件事：

| 树 | 摘掉按位置那段之前 | 摘掉之后 |
| --- | --- | --- |
| `pos1` @ `ce2691f54`（含 `withres` 捕获） | 87 = **84 过 / 2 红** / 1 known | 87 = **74 过 / 12 红** / 1 known |

红名单与在 `origin/develop` 上**逐条相同**，报文也**逐字相同**（`TRANSPOSE-NEEDS function=split_of_jvp_of_g_backward param=0` 等，一条不差）。结论：**`withres` 那一半单独不足**，它让切点把"后一半读的 `with` 值"并入残差，但这些用例要的那个值不是 `with` 值，而是**前向图参数的原值**。

## 5. 归因：三条类，以及为什么现在不能自己改

| 用例 | 缺的值 | 该由谁提供 |
| --- | --- | --- |
| `grad_array_no_int.fvm`、`grad_array_with_int.fvm`、`grad_of_an_array_parameter.nvm`、`grad_array_one_element.nvm`、`grad_array_two_elements_first.nvm`、`grad_array_sum.nvm`、`grad_array_product.nvm` | 参数 0 的**原值**（数组）：`arg=5`/`arg=2` 那个位置的实参 | **切点残差**：`split` 的残余表里本来就有参数的值端口（`split_two_params` 的 `[x, y, x*x]` 就是证据），所以这个值**在闭包里**；缺的是"从参数到那个成员"的**身份映射**——位置约定是唯一的一条，它正在被删 |
| `grad_array_int_parameter_first.nvm` | 参数 1 的原值 | 同上；这条还额外暴露了位置约定的错位：`TRANSPOSE-NEEDS param=1` 而 `skip param=0` |
| `grad_of_a_division.nvm` | 参数 0 的原值，而调用缺第 4 个实参（`param=3 node=6 arg=3`） | 同上，只是这个函数有 4 个参数、缺的那一格更靠后 |
| `transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` | 不是某个值，而是**端口数**：`transpose_of___g_242__` 建出 2 个 norm 端口，调用方只给 1 个实参 | **端口表**：按你给的证据，端口表由走查按调用形状决定，不由律读了什么决定；所以删掉位置约定之后，那一格没有值可读，端口就多出来一个 |

一句话：**十条里九条要的是同一样东西——"参数 → 闭包成员"的身份映射**；第十条要的是**端口表与调用形状对齐**。两者都属于第 1 项的核心，**不是一处可以顺手改的 index**：前者要在切点产出映射（与 `withres` 同一块），后者要动端口表（你已明确不让一个人连夜改）。

## 6. 后半截要等什么

十条里有九条要的是**同一件事**：切点把"后半截要读的参数的**原值**"作为**残差**捕获，并以身份（而不是位置）出现在闭包里。这正是 `withres` 车道那一半——切点侧捕获 + `lin` 去 `with` 列表 + `Pullback` 撤 `WithOf<F>`。`transpose` 侧在那之后才能只按身份读：

- `closurePortOf` / `capturePortOf` 两级已经按身份，**不需要改**；
- 要删的是第三级（按位置 + 类型校验），删了以后 `TRANSPOSE-NEEDS` 只会因为"这个值真的没进闭包"而响，而不是因为"位置对不上"。

**顺序**：`withres` 先合，`transpose` 侧再删。两侧同改会同时动 `split` 的捕获与 `transpose` 的读取，红了分不清是哪一边。

## 5. 复现

```bash
# 摘掉的那一段：transpose.cpp#forward 里 `if (forwardOf[ref] == rt::kInvalidNodeRef && at < paramCount)` 的分支（`const size_t slot = at;` 起，到类型校验通过后 return 为止）
camel test/cases/compile/transforms/grad_array_sum.cml std::nvm 2>&1 | grep -E "TRANSPOSE-NEEDS|call-value-missing"
```