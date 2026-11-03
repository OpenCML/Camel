# 转置成员的 norm 端口数多于调用点实参数：今天不是墙，是被更早一道拒绝挡住的潜在墙

## 问什么

派下来的说法是：转置成员图的 **norm 端口数多于调用点给的实参数**时，一个合法程序会在运行时撞上
`Runtime indirect call norm-arity mismatch`，而这件事在成员建完、运行之前就知道，应该在那时具名拒绝。

这一页记的是**量到的相反结果**：那条报错在当前 develop 上复现不出来 —— 命名的那个程序停在更早一步，
`transpose` 从来没被建出来。要裁定的是这一条该记成墙、还是记成"等先决条件"。

## 1 原来的说法

```
Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args,
target graph 'transpose_of_read_outer' has 2 norm ports.
```

两个数被追到：`needs 2` 出自 `TRANSPOSE function=read_outer carried=2 node=12 origin=0 captures=2
answer=per-target`，`supplied 1` 出自上面那句的 `has 1 args`。

## 2 今天不可复现：两棵树、同一条命令

命令（本机不构建，全走主机）：`camel-remote.ps1 -Path …\nepeephole -Plan
test/plans/feat/compile/transforms -Tier fast -Jobs 8`。程序
`test/cases/compile/transforms/transpose_loop_escape_reads_outer.cml`，跑 `std::nvm`。

`52896caf` 与 `f385b42aa` 两棵树上，stderr 末行逐字相同：

```
REINTERP split function=jvp_of_read_outer skip=loop (this loop iterates an interval, and the interval form is not lifted by `jvp` yet, so there is no lifted loop to cut)
```

stdout 是：

```
(transpose_loop_escape_reads_outer)…transpose_loop_escape_reads_outer.cml:24:20: [Error]: An error has occurred: transforms: split has no rule for this function. The runtime has no diagnostic of its own for this fault: it is the code every fault falls back to, so the message is the whole of what is known.
```

`TRANSPOSE function=read_outer carried=2 …` 一次没出现，`Runtime indirect call norm-arity mismatch`
一次没出现。本树 73 个历史 run 目录里 grep `norm-arity mismatch`，命中 **0**。

## 3 它从哪来：一次已撤回的实验，不是今天任何程序的形状

读到出处：`docs/handover/reports/2026-11-03-does-a-partial-transposition-need-the-operand-values.cn.md`
第五节。探针用占位端口应答自己的请求、把 need 集合一趟报全，成员于是多了捕获端口，而调用点只带 1 个实参
⇒ 运行时报错（那里报的是 `transpose_of___g_58__` 3 个 norm port，以及 `grad_array_no_int.fvm` 的
`SIGABRT`）。那条改动**已整体撤回**，作者同日裁定改走"成员的闭包 = `f` 的闭包整份透传、`withPorts` 归零"，
归 `b2f` 车道（同一处代码）。

也就是说：捕获端口本身要消失，端口数与实参数是**对齐**的 —— 这条算术前提从来不是今天任何合法程序的形状。

## 4 结论

- **不是墙**：没有程序能走到"成员端口数 > 调用点实参数"这一步（要走到，先得让 `split` 抬 interval 循环）。
- **是潜在墙**：`transpose_loop_escape.*` 六条今天红在 `REINTERP jvp … skip=loop`；那六条一转绿，这条就有机会出现。
- **测不到的守卫比不留更糟**：写进 `modules/transforms/transpose.cpp#buildTransposeMember` 的
  `skip=norm-arity` 在整层 fast 里触发 0 次，读日志的人无从判断它成立不成立；而 `b2f` 落地后它的算术
  （`needs = supplied + 1`）也不再成立。

## 5 重开条件

`REINTERP split function=… skip=loop (this loop iterates an interval …)` 这行在
`transpose_loop_escape_reads_outer` 上消失、且该用例开始打印 `TRANSPOSE function=read_outer carried=2` 的
那一天，这一页重开：先量成员的实际 norm 端口数与调用点实参数，再决定是**具名拒绝**还是**让已知值进闭包**。
判据是那条用例自己的 `expect_exit = 0` 与 `expect_output_contains` 里的 `29.0`，不是新造的红。

## 6 三条路，与各自的代价

- **（甲）现在就留 `skip=norm-arity` + `refusalCause` 那一句。** 代价：树里没有任何程序能到达它，
  一层 fast 触发 0 次，测不到；`b2f` 的闭包透传落地后它算的差值恒为 0，守卫变成散文。
- **（乙）不留守卫，只留这一页与 §5 的重开条件。** 代价：先决条件落地那一刻若真有差值，会先以运行时报错的形式出现一次。
- **（丙）造一个能触发它的用例来证明守卫有用。** 代价：为证明守卫去造一个红，且要动 `transposeSignature`
  的捕获端口 —— 那是 `b2f` 的地盘，与作者"捕获不属于类型、已知值进闭包"的裁定冲突。

## 7 默认

**（乙）不写这个守卫。** `buildTransposeMember` 不动，`transpose_operator.cpp#refusalCause` 不动；按 §5 的
重开条件等到先决条件落地，重开时用读数（不是纸面算术）决定走拒绝还是闭包透传。

> 本页对应的那 12 行（`skip=norm-arity needs=N supplied=M` 与 `refusalCause` 的一句）已在
> `97fdb234f`（原 `lane-norm-arity`，该分支已删）上撤掉；同一补丁后来以 `810f58818` 合入 `develop`（`3ff14cbf8`）。

## 8 读数（基线，不是某一笔的成果；本机不构建，全部走主机）

| 计划 | `52896caf`（clean） | `f385b42aa`（clean） | `579d2c7ad`（clean） |
| --- | --- | --- | --- |
| `feat/compile/transforms` | 64 = **51 通过 / 13 失败** | 70 = **57 通过 / 13 失败** | 70 = **57 通过 / 13 失败** |
| 全量 fast（`-Plan all -Tier fast -Jobs 8`） | 8535 / **8500 通过 / 15 失败** / skip 10 / known 10 / killed 0 | 8541 / **8506 通过 / 15 失败** / skip 10 / known 10 / killed 0 | 8541 / **8506 通过 / 15 失败** / skip 10 / known 10 / killed 0 |

13 个失败名单在两棵树上**逐名相同**：`transpose_loop_escape.*` 六条，`grad_array*` 与
`grad_of_a_division` 七条 —— 与本页说的是同一件事：挡住它们的是 `jvp`/`split` 的抬升，不是 `transpose`。