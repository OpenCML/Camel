# 转置要的前向值，要不要以可辨认的身份进闭包

**一句话**：2026-11-03 14:55 的裁定要求删掉"闭包第几格就是第几个参数"的位置约定。实测发现：**代码体里根本没有"经参数端口读参数原值"这种读**——那个前向值是 `transpose` 在展开规则时**自己算出来的需求**（`wantedValues` / `TRANSPOSE-NEEDS` 就是这份需求清单），今天靠"闭包第 `at` 格"满足它。所以"把体里的读改写成读闭包端口"这条没有可改的东西；剩下的只是一个选择。

实测依据（三条，详见 `docs/handover/reports/2026-11-04-is-there-any-read-of-a-parameter-value-through-a-port.cn.md`）：

1. 把"闭包成员"并入残差 → **中性**，`transpose` 按位置取值入口的命中数仍 **20**；
2. 在 `split.cpp#makeCopy` 的 `portIndex` 分支里改成"若该参数被残差带走就返回残差投影" → **该分支一次都没进入**（它只处理复制源图的节点，后一半的参数端口由建签名那段 `addPortNode` 出来）；
3. 把 `backward.portOf` 换成残差投影 → **红 35 条**（`vjp_arities.*`、`vjp_loop_gradient.*` 起），因为 `portOf` 的读者要的是**切线**，不是原值。

而 `at` 是**参数下标**，闭包端口表与 norm 端口表**互不相交**（五行 `inNorm=0`），约定只在"参数 k 的原值恰好排在闭包第 k 格"时成立——`grad_array_int_parameter_first`（`NEEDS param=1` vs `skip param=0`）就是它被打破的现场。

## 范围收窄（2026-11-04 01:20，同一岔口拆成两页之后）

**同一个岔口原来被问了三遍**（本页、`2026-11-04-the-identity-already-exists-on-the-cut-side.cn.md`、`2026-11-04-what-the-positional-read-is-a-copy-of.cn.md`），而三页的默认建议互不相同（"暂不动"／"不算乙、可推进"／"无默认"）。**同一问句让作者裁三次是不合格的**，所以按下面拆开：

- **甲那一边**（"身份进闭包"算不算 14:55 否掉的乙）→ 单独成页 **`2026-11-04-the-identity-already-exists-on-the-cut-side.cn.md`**，默认"不算乙、可推进"，本页不再重复问。
- **本页只问乙那一边**：要不要改成"律不再要前向值、由 `jvp` 前半边多算一些值、用残差承载"。
- **`what-the-positional-read-is-a-copy-of`** 那页是**证据**（谁建的、递过去的是原值还是切线、约定何时成立），不是问句——它的读数已并入 `the-identity` 页。

**一处必须一起看的更正**：本页第 2、3 条读数说"换过去的是参数原值"是对的，但**当时把它读成"这段代码有存在必要"是错的**——实际是"实参是切线、递过去的是原值，两者只有该是谁不同；`index_into` 的转置成员只读形状，所以今天碰巧对"。**它碰巧对，不是有必要。**
## 要裁的那一句

**让 `transpose` 不靠位置，只有两条路，选哪一条？**

- **甲**：让切点把"参数 k 的原值"以**可辨认的身份**放进闭包。—— **本页不再问这一支**，见上节；那一支单独成页，默认"不算乙、可推进"。
- **乙**：让律**不再需要前向值**——像 `relu` 把掩码放进前半边当值那样，要求 `jvp` 的前向半边**多算一些值**，由残差承载。

## 默认建议（施工方默认）

**暂不动代码，保持现状，等裁定。** 理由：甲已被明确否掉；乙是真设计改动（动 `wantedValues` 与残差表，并改变"前向半边算哪些值"的边界），属要有裁定的那一类。

**代价（写清，供裁定时权衡）**：按位置取值那段继续存在，命中 **10 条用例 / 20 行**（`grad_array_no_int`、`grad_array_with_int`、`grad_of_an_array_parameter`、`grad_array_one_element`、`grad_array_two_elements_first`、`grad_array_sum`、`grad_array_product`、`grad_array_int_parameter_first`、`grad_of_a_division`、`transpose_hand_closed_lambda`），复现命令与每行 `ref`/`at` 在报告里。**任何人删这段之前必须先读那份清单**：删了会红 12 条，其中 9 条缺的是"参数原值"、1 条缺的是端口数。

**若取乙**：`jvp` 前半边多算哪些值要有明确判据（谁能按需索取、谁只是被顺带算出），否则残差表会无界变长——这一点建议在裁定里一并定。

