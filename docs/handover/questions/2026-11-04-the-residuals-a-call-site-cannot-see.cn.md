# 调用点看不见的那部分残差，由谁来算

**一句话**：2026-11-03 16:55 的裁定给了方向（「按内容对被调函数 `split`、转置后一半、把调用转过来」），工程量按默认建议**乙**（`transpose` 学会"调一个闭包值"，不动 `Vjp<F>` 的可见形状）开工。**做完发现乙缺一件底层能力**：调用点能按身份认出"参数/`with` 值"那类残差，但认不出被调函数**自己算出来的**残差；而要把 `lin` 的闭包原样交给成员，需要一条**读函数值闭包格**的图操作——**图上没有这个东西**。所以问题不是"乙怎么写"，是"乙少的那一格谁补"。

## 实测依据（`lane-bycontent`，基线 `origin/develop = 5e461c590`）

提交 `transpose` 改动后，`docs/handover/questions/2026-11-04-the-residuals…` 所指的形状第一次走到了规则内部：

```
REINTERP split function=jvp_of___g_322__ forward=350 backward=351 type=(float) => (float, (float) => float pure) pure residuals=2
REINTERP split function=jvp_of_weighted forward=356 backward=357 type=<float> (float) => (float, (float) => float pure) pure residuals=4
TRANSPOSE function=split_of_jvp_of___g_322___backward skip=call-no-transpose node=5 callee=jvp_of_weighted pair=1 why=a value the second half reads is computed inside the callee, so this call does not stand on it
transforms: transpose has no transpose for this function: this call's callee has no transpose of its own, so the call cannot be turned around.
```

三件事，逐条对应到代码：

1. **`answersAPair` 判据成立**：`weighted` 被 `jvp` 抬起后（`jvp_of_weighted`）由 `answersAPair` 判为「答对」，`buildSplitGraph` 也真的切了它（`forward=356 backward=357 residuals=4`）。**判据与切都对了**，卡住的是调用点。
2. **残差能按身份填的那部分已经通了**：残差是参数 → 取该参数位置的实参；残差是被调函数的 `with` 端口 → 取对应的 `with` 实参。`jvp_of_weighted` 的四个残差里，`y`（参数）与 `x`（`with`）走这条，**不需要任何新能力**。
3. **卡住的是"被调函数自己算出来的"那一格**：`weighted(y) = x*y + x` 的切面里，后一半读的 `x*y`（乘积节点）是前一半算的。调用点**没有**这个值，`residualsOf` 也**没有**办法把它指到实参上——它既不是参数位置，也不是 `with` 端口。**这一格只能靠"把 `lin` 的闭包交给成员"补，而图上没有读闭包的节点。**

## 为什么乙缺这一格：底层的三条事实

- `Function::tuple()` 就是闭包；**运行期间接调用**（`FastVMSchedPass::populateIndirectCallFrame` → `populateFunctionClosureFrame`）从**被调的那个 `Function` 值**里填目标的闭包端口。所以"目标图 + 别人闭包里的值"这个组合**表达不出来**：图和闭包都来自同一个值。
- **没有任何节点种类读运行时函数值的闭包格**（`GCNodeKind` 表里没有，也没有对应算子）。`std::defunctionalize` 能把闭包调用变直调，靠的是**编译期**从 `Fill`（`GCFillKind::FunctionClosure`）的 `with` 输入读，而不是运行期读一个值。
- `Function::rebind` 是程序/重写期的，不是运行期把某个值的图换掉。

所以乙的施工版只能退到「调用点自己造一个闭包值」：`buildLinearClosure` 造的是 **`with` 输入**拼出来的闭包，值必须是调用点**手里有的**。手里有的、按身份对得上的已经做了；对不上的那一格没有来源。

## 三条路

- **甲：前一半出口露出残差。** 前一半答 `(result, lin, 残差…)`，或让 `lin` 带一份可读的残差表。**代价**：改 `Vjp<F>` 的**语言可见形状**，所有 `vjp` 用例与文档跟着改；**要作者点头**（施工方不得自行选）。
- **丙：加一条"读闭包格"的图操作。** 让调用点能取 `lin` 的函数值、按格号取出闭包里的值，再交给 `buildLinearClosure`。**代价**：动**图模型**（新增节点种类或算子，含 VM/调度/exporter 与 `docs/design-principles` 的一格），**不动语言可见形状**——`vjp` 的签名与全部用例期望不变。
- **丁：反向pass里重算。** 把前一半算残差的那几个节点复制进转置成员，自己算出来。**代价**：与"切面之外的东西一律不重算"（残差存在的理由）直接冲突，且前向算两遍；**也是设计原则层面的事**。

## 默认建议（施工方默认）

**先按住：保持"按名字拒绝"，不动上面三条中的任何一条。**

理由三条：

1. **乙在现有图上做不成，而甲/丙/丁都不是施工方能单独决定的**：甲改语言可见形状，丙改图模型，丁改"切面之外不重算"。三者都要作者点头，**自选哪一条都是替作者决定**。
2. **按名字拒绝落在这个lane的次序上**（**对 > 明确拒绝 > 错数 > 崩**）：现在的失败是 `pair=1 why=a value the second half reads is computed inside the callee, so this call does not stand on it`，一行字指明是哪一格；**它不会把一个邻近实参冒充成残差**。
3. **若作者要在甲/丙/丁里选，推荐丙**：裁定的目的是"结果只看输入图的内容"，不是改 `vjp` 的对外类型；丙是三者里唯一**既不碰语言可见形状、也不推翻"切面之外不重算"**的一条。**若作者认为图模型不该长这一格，那就只能甲**——那时需要一份"`vjp` 新可见形状"的完整用例改写清单，施工方按清单改。

**未按默认做的部分**：`answersAPair` 判据、两条路径共用 `transposePairCall`、`transpose.cpp` 里 `liftedFrom` 归零、`split` 侧改按内容重定向——这些都在本 lane 里落地了，**不依赖上面这一问**。

## 判据 / 哨兵（本次读数，主机 load 1m 14.56–20.96）

- `feat/transforms/split` **19/19**；`feat/compile/transforms` **91 = 88 过 / 2 红 / 1 known**，与基线同（`91 = 88/2/1`），**不新增红**。
- 两条长红仍是拒绝且**原文不变**：`transpose_loop_escape.two_values_of_two_kinds`（`Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports`）、`transpose_loop_escape.second_state_of_the_same_loop`（`7.0` + `split does not build this function (this loop is on the chain…`）。
- `grad_array_no_int` 仍答 `6.0` / `([2.0, 3.0])`；`transpose_hand_closed_lambda` 四行仍是 `cut ct1= (6.0)` / `cut ct3= (18.0)` / `hand ct1= (6.0)` / `hand ct3= (18.0)`；`jax_has_aux` 未动。
- **库层 `weighted`（`with <x: float> func weighted(y: float): float { return x * y + x }`，`vjp((ww: float) => weighted<ww>(2.0))(3.0)`）现在拒绝**，读数上面第一条；**这是本页要作者裁的那一格**，不是"实现没做完"。