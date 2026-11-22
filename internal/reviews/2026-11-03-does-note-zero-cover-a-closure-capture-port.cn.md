# `noteZero` 要不要覆盖闭包捕获端口？

## 问什么

`transpose` 对"切向落在一个没有切向空间的值上"印一行读数（`contribution=0 node=<n> type=int
space=none from=<cast>`，`modules/transforms/transpose.cpp` 的 `noteZero`）。它现在只在**有切向被递给
那个值**的时候触发。一个整数被 `split` 的 cut 当作残差收进闭包之后，它在后半里是一个**闭包端口**：
后半不再计算它，也就没有切向被递给它，于是这一行不印。

要裁定的是：这种形状要不要也印一行"零贡献"。**默认建议：不印**（下面第 4 节）。

## 1. 让它是问句的读数

`test/cases/compile/transforms/transpose_loop_escape_int.cml`，远程 88zjwei、`de84cd84c` 之后
（作者第 ① 项落地、`kGraphFlagClosureCarriesParameters` 已删）的实测输出与 stderr：

```
stdout: 10.0 (1.0) (5.0) (1.0) (3.0) (2.0) (4.0)
stderr: REINTERP split function=jvp_of_int_escape forward=272 backward=273
        type=(float) => (float, (float) => float pure) pure
        residuals=3 loop=1 tape=2 invariants=2
        TRANSPOSE function=split_of_jvp_of_int_escape_backward graph=274 type=(float) => (float,) nodes=14
```

- 七个数字全对（论文答案 1；两个控制 2 和 1；第二次问的 5/3/4 必与切向成比例）。
- `skip=` 一次也不出现 —— 基线上这条是 `REINTERP split ... skip=backward construct=a parameter
  ... why=the backward half reads the value of parameter 0`。
- 三条打印一条也没出现：`TRANSPOSE function=int_escape contribution=0 node=`、
  `type=int space=none`、`TRANSPOSE function=int_escape graph=`。原因两条，都可复核：
  1. **名字对不上**：被转置的图实际叫 `split_of_jvp_of_int_escape_backward`，用例要的是 `int_escape`；
  2. **那个 `int` 在后半里是闭包捕获端口**，没有切向被递给它，`noteZero` 的触发条件（一个切向落下来）
     根本不成立。

所以这一页问的不是"行为对不对"（行为已经对），而是"读数要不要补一行解释"。

## 2. 三条路

**路 A（默认建议）：不印。** 捕获端口本来就没有切向可传，印"零贡献"会让读者以为这里发生过一次递送
然后被吞掉。做的事：把用例的期望从三条打印换成那七个数字（已在
`test/plans/feat/compile/transforms.plan.toml` 落地），并在用例描述里写明为什么那三条不印。
代价：读者要靠 `residuals=3 ... invariants=2` 自己看出整数进了闭包；`contribution=0` 这条读数的覆盖
范围只到"非捕获"的值，得在别处说清楚。

**路 B：对每个没有切向空间的闭包捕获端口印一行 `contribution=0 ... from=-`。** 代价：`from=` 没有
"谁递的"可填，字段含义要改；每一张带整数残差的图都会多一行，`transpose_no_tangent.*` 那批的
"读数条数"含义跟着变；而且它解释的是一件本来就没有发生过的事。

**路 C：不改读数，让后半重算那个整数而不是收进闭包**（即"这个残差不该收"）。代价：与作者第 ① 项
正相反 —— 那一项要的就是"后半读到的参数原值作为残差进闭包"，重算会把这一条重新打开；而且整数
重算等于把 `int(3x)` 求值两次，是同一个值的两次阅读，不是同一个残差。

## 3. 若裁定是路 B，范围在哪

只改 `modules/transforms/transpose.cpp` 的 `noteZero` 及其调用点（闭包端口注册处加一次），不动
`split.cpp` 的残差逻辑（那是作者第 ① 项的落地处，`lane-jvpwith` 也在同一文件上，界线按函数划）。

## 4. 默认建议

**按路 A 落地，不等回复。** 理由：数值正确已经说明行为对，而这个形状里没有任何切向被递给那个整数，
补一行"零贡献"是在解释一次不存在的递送；读数该说的事已经由 `residuals=` 和 `invariants=` 说了。
作者若裁定路 B，只需回退 `test/plans/feat/compile/transforms.plan.toml` 里那一条期望的改写，并在
`transpose.cpp` 的闭包端口注册处加一次 `noteZero`，别的层不受影响。