# 库层 `with_grad` 今天就该挂一个一律拒绝的可调用导出，还是等 `jvp` 的 `with` 切向落地再挂？

状态：待裁。**已按默认建议开工**——导出已落地（`d88f0cc1e`，`stdlib/vjp.cml#with_grad`），
本页问的是它该不该留着。发现时辰 2026-11-04 02:50，树 `D:\Projects\Camel-wt\withgradexport`，
基线 `db741501d`。

## 一、让这是问句的读数

作者 17:55 的判据是"库层 `with_grad` 在 `grad_first_with_parameter` 上与引擎同答"，而盘点车道量出
**库层没有 `with_grad` 这个可被调用的东西**。本车道把四个分组的**内联组合**（就是库层唯一可能的写法：
`vjp` 一个把权重挪到普通形参的 lambda）逐组跑了一遍，宿主 `db741501d`，`std::nvm`：

| 分组 | 引擎（今天） | 组合（今天） |
|---|---|---|
| `gx/vg` `weighted` | `(9.0, 3.0)` | **跑到底，答 `0.0`** |
| `pg/pvg` `point_loss` | `(11.0, { a: 4.0, b: 1.0 })` | `jvp skip=body`，`jvp has no forward transform for this function`，exit 1 |
| `wg/wvg` `dot` | `(32.0, [4.0, 5.0, 6.0])` | `split skip=forward`，`the port is not a parameter of the graph, so neither half takes it`，exit 1 |
| `sk/sx` `scaled` | `6.0`（`sx` 4.0） | `jvp` 无前向变换，exit 1 |

第二、三、四组各自**明确拒绝**，都不是错数。第一组**跑到底并答 0.0**，而它欠的是 3.0——这一条才是
本页存在的理由。根因不在 `transpose`，也不在 `split`，在更上游：

```
jvp((ww: float) => x_times_y<ww>(2.0))(3.0, 1.0)   value=6.0 tangent=0.0     <- 欠 2.0
jvp((y: float)  => x_times_y<3.0>(y))(2.0, 1.0)    value=6.0 tangent=3.0     <- 对
```

`x_times_y` 就是 `x * y`。第一行是**每一个** `with_grad` 组合都有的形状：被微分的值交回给被调方的
`with` 列表，而前向模式对"跨过 `with` 列表的值"答它类型里的零（`stdlib/signatures/transforms.cml`
在 `Jvp<F>` 下写着这条规则）。所以组合对任何权重都答梯度类型的零——而权重的定义就是那个列表。

**结论：今天没有任何一条构形能给出正确数字；能跑到底的那一条给的是错数。** 而"两边都是 0"不能当
"算对了"（仓库纪律那一条），所以导出不能是真实组合。

## 二、三条路

- **甲（默认建议，已落地）：导出，且一律拒绝。** 判据第一次有了可调用的对象：`with_grad(weighted)<3.0>(2.0)`
  能写、能编译、能跑，并在调用点按名拒绝、给出那句"为什么"和两个该读的名字。代价：这个对象**不会自己翻面**
  ——前向模式一旦带上 `with` 切向，得有人把函数体换成真组合、把声明换成 `ValueAndGradient<F>`；一个
  恒拒的存根有腐烂的风险，所以它的文档里写死了要翻的那两处和要测的那四个分组。
- **乙：不导出，等 `jvp` 落地再挂。** 代价：判据仍然没有对象，"判据没有对象等于没有判据"这条规则
  原样成立；这段时间里没有任何读数能说明组合对错。好处是没有存根可烂。
- **丙：导出真实组合。** 它答 `0.0`，是把"明确拒绝"换成"错数"，按 **对 > 明确拒绝 > 错数 > 崩** 是往下走一格。

## 三、代价与哨兵

- 哨兵一：`test/plans/feat/compile/transforms.plan.toml` 的 `transforms.with_grad_from_the_library`
  钉住"四条 `library=` 字段都是 `RuntimeError`"和那句拒绝原文。哪天有人把真实组合直接写进去，
  四个字段会变成元组，这条立刻红——这就是"不许 0.0 式错数"的闸。
- 哨兵二：同文件的 `transforms.with_grad_composition_meets_the_split_wall` **一字未动**，仍是
  `expect_exit = 1` 且钉 `skip=forward`：它钉的是 `split` 自己那堵墙，与本页不是同一件。
- 前向模式带上 `with` 切向之后要做的三件事：把 `stdlib/vjp.cml#with_grad` 的体换成组合、把声明换成
  引擎那个形状（需要"pack 的第一个成员"类型函数，见同目录
  `2026-11-04-should-the-library-grad-be-declared-per-arity.cn.md`）、把上面那条哨兵的四个字段改成正数。