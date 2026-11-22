# 第 1 项后半截：「体里经参数端口读原值」这种读，存在吗？

**问：作者 14:55 要删掉的"按位置去找参数的值"，能不能靠"把体里那个读改写掉"来删？**

**答：不能，因为体里没有这种读。** 三条实测（按时间顺序，每条一句）：

1. **把"闭包成员"并入残差 → 中性**：在 `residualsOf` 里把 `isWithPort` 的判据放宽到 `isClosureMember`，`feat/compile/transforms` 仍是 87 = 84 过 / 2 红 / 1 known，`POSPROBE-HIT`（`transpose` 按位置取值）命中仍是 **20**。
2. **`makeCopy` 的 `portIndex` 分支零进入**：在那一支里插 `POSPROBE-PORT`，`grad_array_sum` 单跑一行都没有；同一次运行里 `POSPROBE-HIT` 命中 1 次。那条分支只在**复制源图节点**时进，而 `transpose` 抱怨的 `ref=5` 是后一半自己的端口，不由复制产生。
3. **把 `backward.portOf[i]` 换成残差投影 → 红 35 条**（`feat/compile/transforms` 87 = **51 过 / 35 红 / 1 known**，红从 `vjp_arities.*`、`vjp_loop_gradient.*` 起）。原因：`portOf[i]` 的读者要的是**切线**——体里对后一半参数端口的读本来就是取切线，换成残差投影等于在用切线的地方给原值。

## 那么"原值"从哪来

`transpose` 在**展开规则**时需要前向值，**那是它自己算出来的需求**，不是体里的读：`wantedValues` 与 `TRANSPOSE-NEEDS` 就是这份需求清单的输出。今天这份需求靠"闭包第 `at` 格"满足：

- `at` 是**参数下标**（`portParameter[i]`）；
- `closurePorts` 与 `normPorts` **两张表互不相交**（插桩里五行 `inNorm=0`）；
- 约定只在"参数 k 的原值恰好是闭包第 k 格"时成立——`residualsOf` 把参数原值排在残差表最前（`split.cpp:924` 那段注释原文：`slot at is parameter at's value`）；
- 它破的现场是 `grad_array_int_parameter_first`：`TRANSPOSE-NEEDS param=1` 而 `skip param=0`。

## 可裁（二选一）

- **甲**：让 `transpose` 不靠位置，就得让切点把"参数 k 的原值"**以可辨认的身份**放进闭包——那就是把位置换成字段的映射表，即 2026-11-03 14:55 否掉的乙。
- **乙**：不走映射表，唯一的路是**让律不再需要前向值**（像 `relu` 把掩码放进前半边当值），而这要求 **`jvp` 的前向半边多算一些值**。

## 施工方默认：不动代码

理由：第一条已被作者否；第二条是设计改动，会动 `wantedValues` 与残差表，属于要有裁定的那一类。

**代价写清**：`transpose.cpp#forward` 里按位置取值那段继续存在。它是**将来任何人删它之前必须读的清单**——这一轮 `feat/compile/transforms` 上共 **10 条用例、20 行命中**（`test/results/log/<run>/*/stderr.txt` 与 `output.txt` 各记一次）：

| 用例 | 被转置的图 | `ref` | `at` |
| --- | --- | --- | --- |
| `grad_array_no_int.fvm` | `split_of_jvp_of_both_backward` | 5 | 0 |
| `grad_array_with_int.fvm` | `split_of_jvp_of_mixed_backward` | 5 | 0 |
| `grad_of_an_array_parameter.nvm` | `split_of_jvp_of_g_backward` | 5 | 0 |
| `grad_array_sum.nvm` | `split_of_jvp_of_g_backward` | 5 | 0 |
| `grad_array_product.nvm` | `split_of_jvp_of_g_backward` | 5 | 0 |
| `grad_array_one_element.nvm` | `split_of_jvp_of_g_backward` | 2 | 0 |
| `grad_array_two_elements_first.nvm` | `split_of_jvp_of_g_backward` | 2 | 0 |
| `grad_array_int_parameter_first.nvm` | `split_of_jvp_of_read_at_backward` | 2 | **1** |
| `grad_of_a_division.nvm` | `split_of_jvp_of___g_250___backward` | 3 | 0 |
| `transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` | `__g_242__` | 0 | 0 |

复现：在 `transpose.cpp` 按位置取值那一句前插 `fprintf`，`CAMEL_POSPROBE=1 node scripts/test.js test/plans/feat/compile/transforms.plan.toml --tier fast --jobs 4`，再 `grep -r POSPROBE-HIT test/results/log/<run>`。

## 这一轮改了什么

**代码一行没改**：三次实验都已在 `git checkout --` 后还原，`pos1` 停在 `ce2691f54`、`varord` 停在 `7d25c9111`（本页之外）。`:501` 那条按名拒绝（`the backward half reads the value of parameter N, and it is not one of the values the cut carries`）原文仍在。