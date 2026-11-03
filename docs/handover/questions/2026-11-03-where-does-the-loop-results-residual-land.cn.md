# "循环结果作为残差跨过切口"这条裁定，落在哪一格？

## 问什么

作者 2026-11-03 10:30（`9c69cb3df`）裁定：`split` 造的后一半里遇到 `Loop`，就是 `residualsOf` 漏记了；
后一半读到的、由前一半算出的值，`Oper`/`Call`/`Loop`/`Brch` 的结果一律记为残差。裁定的判据是
`vjp_loop.rounds_are_undone_in_reverse` 答 `(1.0, 1.0)`、`two_values_of_two_kinds` 的 `same=no` 变
`same=yes`，前提是**不加端口、不抄区域**。

这一页说的是：**那两格的读数与裁定的前提不符**，照着字面补 `residualsOf` 会把两条今天绿的用例弄红，
而两条目标用例不动。所以要问的是"这条裁定该落在哪一格"，不是"怎么实现它"。

## 量到的（`lane-resid`，HEAD `e3453a60a`，2026-11-03 18:29–18:40）

基线，三族合跑（`-Tier fast -Jobs 4`，主机 88zjwei）：

```
feat/compile/transforms   80 = 77 过 / 2 红 / 1 known
feat/transforms/split     19 = 19 过
feat/modules/remat         6 =  6 过
合计                     105 = 102 过 / 2 红 / 1 known
```

### 一格：`vjp_loop` 走的不是 `residualsOf`

`residualsOf` 今天**已经**把 `Oper`/`Call`/`Loop`/`Brch` 一视同仁：它从后一半的出口走 `dataInputsOf`，
凡在 `beforeCut` 里、又不是 `tangentHalf` 的节点一律 `found.push_back(ref)`，只有 `isLiftedCall`
（前一对值由体自己搭）和 `remat` 会被 `erase_if` 掉。它漏的不是节点种类。

`vjp_loop` 的图走的是**循环专用通路** `cutLoopGraph`（`exitShapeOf` 返 `ExitShape::Loop`），
那条通路根本不调 `residualsOf`，它的残差表是 `readLoopCut` 的 `cut.outside`：

```
REINTERP split function=jvp_of_g forward=276 backward=277 \
  type=(float[]) => (float, (float[]) => float pure) pure residuals=1 loop=1 tape=5 invariants=0
TRANSPOSE function=split_of_jvp_of_g_backward skip=call-value-computed-by-the-body param=0 node=23 arg=2
```

`invariants=0`。后一半的节点（临时探针，读完即撤）：

```
0:1(Port 闭包=tape)  1:1(Port 参数 tangent)  2:15(Loop) norm=[0 1] with=[20 21] \
3:1 4:1 5..9:5(Accs 读 tape 元素)  ...  21:1(轮端口) 22:0(常数 0)  23:10(Func) norm=[2 22]
```

`2:15` 是 `GCNodeKind::Loop`，它是**后一半自己的那个"遍历 tape 的反向循环"**，`23:10` 是
`index_into(<ref 2>, 0)`。也就是说后一半读到的那格循环结果，是**后一半自己算出来的**，不是前一半算的。
（`docs/handover/questions/2026-11-03-is-the-loop-halfs-missing-value-at-the-call-site.cn.md` 用两个探针
独立量到同一件事：`ref 2` 的 `norm[0]` 是闭包里的 tape，`ref 2` 本身是这个循环的**输出**，`isMember=0`。）

### 照字面补 `residualsOf` 的读数（已撤）

把"出口链读到的那个 `Loop` 也记进 `cut.outside`"落进去（`remat` 除外）之后：

```
REINTERP split function=jvp_of_g skip=forward construct=a loop uri=- why=this loop is on the chain
  the exit reads and neither half has a value for it: the cut copies a loop where the exit reads its
  two components through one projection each, and here the half would have to copy a body region
feat/compile/transforms   80 = 77 过 / 3 红 / 0 known
feat/transforms/split     19 = 18 过 / 1 红      (split_loop_value_projected: 此前 19/19 里的绿)
```

原因是硬的：`cutLoopGraph` 在造循环**之前**就先把每个 `cut.outside` 交给**前一半**算
（`split.cpp:1664`），所以把一个循环记成残差 = 要求前一半算出一个循环的值 = **抄一个区域**，
正是裁定明令禁止的那条。而且这个值前一半**算不出来**：前一半的循环只带值状态，
`dxs` 是后一半反向循环的输出。两条红用例（`vjp_loop`、`second_state_of_the_same_loop`）读数一字未变。

### 另一格：`two_values_of_two_kinds` 缺的值不是"端口多了"

```
TRANSPOSE function=read_outer graph=49 type=(float, float) => (float,) nodes=14 arms=0 rhs=add_t,mul_t
TRANSPOSE op ... transposed=(float) => (float,) graph=(float, float) => (float,) same=no
Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args,
  target graph 'transpose_of_read_outer' has 2 norm ports
```

多出来的那个 norm 端口是 `capturesOf` 里参数 0（`x`）的捕获端口，`transpose.cpp:531-544` 把它按
**普通端口**加进去。裁定说"闭包不属于类型"，那么它该是闭包端口——但 `read_outer` 的函数值闭包是空的
（`transpose_operator.cpp:246-249` 只是把 `f` 的闭包原样拷过去），没人填这一格。改成闭包端口会把
今天的**响亮的 arity 报错**换成一次**读到空槽的静默错数**，比现在更差。要 29.0 就必须知道 `x=1`，
而 `transposed=(float) => (float,)` 这个类型里没有它的位置；`transpose.cpp:176-186` 自己也写着，
这一处不一致是类型拼接丢 multiplicity 的缺陷。

## 三条路

- **A：`transpose` 的 `forward` 学会抄 `Loop`**（连同 `LoopCond`/`LoopBody` 两个区域）。代价：`forward`
  从此要抄区域，而它今天连 `Brch`/`Join` 都不抄；裁定明说不要。好处：`vjp_loop` 的两条能真走通。
- **B：`split` 的后一半不把"遍历 tape 的那一格"露给 `index_into`**——让 `idx_arr` 的转置不读数组的值
  拿长度。代价：动算子的转置约定，要作者裁；`rules_angle_args.mean_axis_transpose` 那族同一条墙。
- **C（默认建议）：把这两格划出这一站**，并**把裁定的那句话收窄成一句可判的规则**：残差表覆盖
  `residualsOf` 的四种节点（今天已覆盖，且已有用例），`cutLoopGraph` 的 `cut.outside` 只收"前一半
  算得出、后一半读得到"的值——一个循环的**输出**不在其中，因为那要求前一半抄区域。两条用例各留
  自己的记号（`vjp_loop` 已知失败、`two_values_of_two_kinds` 基准红），并在 `known_fail_note` 里写清
  缺的是 A 或 B，不是 `residualsOf`。

## 未测出

- 没量过 A 落地后 `transpose` 的区域复制会不会把 `second_state_of_the_same_loop` 一起带绿。
- 没量过 B 的端口数上界（`transpose_many_parameters` 只能给个方向，不是这个形状）。
- 这一页没有提交任何代码改动：`split.cpp`/`transpose.cpp` 与 `e3453a60a` 逐字相同，唯一断言改动是
  把 `vjp_loop.rounds_are_undone_in_reverse` 的 `known_fail` 留回原样（撤掉后它是红，不是绿）。