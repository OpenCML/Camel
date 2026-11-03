# `mean_axis` 的转置：一条律改不动这道墙，它等的是两个上游

## 问什么

一条：`rules_angle_args.mean_axis_transpose` 今晚**停手等上游**（个数闸与 `same=no` 收口），还是**改用例的拼写/调用方式**让它绿？

## 为什么会有这一页

派工时给的判断是"另一条车道用写法解开了同一堵墙，把只依赖前向值的部分放进前半边，线性半边只留一个可转置的单操作数算子（`relu` 的 `where(x > 0.0, 1.0, 0.0) * dx`），照这个写法改写 `mean_axis_t` 就能绿"。实测下来这个写法在这里**够不着**：`relu` 解决的是"律的右手边把三操作数算子放进线性半边"，而 `mean_axis` 的三操作数是**前向节点自己**，走查在查律之后、实例化之前按个数拒（`modules/transforms/transpose.cpp`，`TransposeRefusalCause::Arity` 那一段），**右手边写成什么都在那之后**。

## 实测（本机不构建；主机 88zjwei，树 `~/camel-work/meanax2`，基点 `dd5bb620f`）

1. **形状**（探针 `probe_mean_shape`，`x = [[1,2,3],[4,5,6]]`）：
   `shape x=[2, 3] m=[3]`、`numel x=6 m=3`、`scale=2.0`、`mean=[2.5, 3.5, 4.5]`。
   缩放因子 `numel(xs)/numel(ct) = 2` 是**一个标量**，轴语义没有让降维后的形状把缩放因子变成数组。
   律的算术照原样跑一遍就是判据要的那组数：`probe spread=[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]`。
   **所以卡的既不是形状，也不是数字。**
2. **改写右手边不影响拒绝**：`mean_axis(x, 0, false)` 的节点有三个常规操作数，走查在拿到律之后立刻按个数拒；把律的右手边换成只读 `ct`（`return ct`）实测仍是同一条拒绝。
3. **角括号拼写能过闸，但撞上端口缺口**：把同一个算子写成 `mean<0, false>(x)`（一个常规操作数 + 两个 context 参数）之后，`TRANSPOSE function=loss_mean_angle … nodes=8 rhs=dtod_t,sum_t,mul_tensor_t,mean_axis_t` —— `mean_axis_t` **真的被实例化了**，接着停在这里：
   `transposed=(float) => (float[..],)` 对 `graph=(float[..], float) => (float[..],)`，`same=no`，随后
   `Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_loss_mean_angle' has 2 norm ports`。
4. **那个多出来的端口不是律读出来的**（这一条纠正 `docs/handover/questions/2026-11-03-who-carries-the-forward-halfs-intermediate-values.cn.md` 的"律读参数或常量不需要残差"）：律写成只读 `ct` 仍是两端口；连 `f(x) = sum(x)` 这种既无 mean 又无乘积的函数，直接转置也是 `graph=(float[..], float)` 对 `transposed=(float) => (float[..],)`。**直接转置的非线性（乃至 `sum`）函数今天一律长出一个捕获端口，而调用侧的类型不知道它** —— 这就是那两条已知红（`transpose_loop_escape.two_values_of_two_kinds`、`.second_state_of_the_same_loop`）的同一个 `same=no`。
5. **`vjp` 那条路撞同一道闸**：`vjp(loss_mean)` 走 `split`，线性半边里是 `jvp_mean_axis` 的右手边，也就是 `mean_axis(dxs, axis, keepdims)`，转置它得到
   `TRANSPOSE function=split_of_jvp_of_loss_mean_backward skip=arity uri=tensor:mean_axis operands=3`。
6. **旁证**：`sum_axis(x, 0, false)` 在直接转置里连律都没有（`skip=rule uri=tensor:sum_axis reason=unbound`），同一族的三操作数归约没有一条能走。

## 三条路（各带代价）

**A. 停手，等约定收口（默认）。** 捕获端口进调用侧类型这一笔（`same=no`）已经有两条红占着，收口它的人必然要一并定"直接转置要不要带前向值"。定完之后 `transpose(loss_mean)` 的类型从 `(float) => (float[..],)` 变成 `(float[..], float) => (float[..],)`，用例的**调用点**要改成 `transpose(loss_mean)(x, 1.0)`（`main` 里先得有 `x` 这个值）——判据的期望值 `[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]` 一个字都不用动。
代价：判据今晚仍红，`rules_angle_args` 6 = 4/0/2 不动。收益：不为一条用例另造一套端口约定。

**B. 改用例的拼写或入口。** `mean<0, false>(x)` 能过个数闸（实测 #3），但单独换写法仍红在 `same=no`；换 `vjp` 也仍红在同一个个数闸（实测 #5）。
代价：改的是用例在问什么——这条判据本来就是"括号拼写是律能背的写法"的读数，换成角括号或 `vjp` 之后它不再问那件事，等于把判据换掉而不是让它过。

**C. 在 builder 层把常量常规实参折进 `<>`。** 算子表里 `withAfterNorm = true, ordinaryParams = 1` 早就写了（`modules/tensor/ops/reduction_ops.cpp#reduceAxisDef`），是括号拼写的声明把这层意思绕过去了；折过来之后括号拼写的 `mean_axis` 节点只剩一个常规操作数，个数闸自然过。
代价：每个括号拼写的张量算子（`sum_axis`/`mean_axis`/`max_axis`/`argmax_axis`…）节点形状都变，golden 图与 `split` 的残差计数要一起重录；**而且过了闸还是停在 A 那条 `same=no` 上**，单独做仍不绿。

## 默认（按此往下做，同一轮开工）

**A**：本车道不动代码，不抬个数闸，不改判据、不改期望；把上面六条读数留在这页，等 `same=no` 的收口把两端一起定。
探针文件不入库（`test/cases/probe/`、`test/plans/probe/` 跑完即删），日志在 `D:\Temp\meanax2\`。