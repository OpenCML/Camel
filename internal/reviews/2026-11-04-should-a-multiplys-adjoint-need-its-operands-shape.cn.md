# 乘法的伴随要不要读操作数自己的形状？

**状态**：待裁；我方默认见第 3 节（**甲：不读，程序把形状写进类型**）。

写这页的直接原因：`lane-tranche2`（`82450275b`）给 8 个算子写了正反两半律，其中 `erf`、`erfc`、`gelu` 三个的**库半边只有在参数类型写明形状时才走得通**；换成今天最常用的 `float[..]`，库半边就停在同一行拒绝上。而这一行拒绝**不是这一批引入的**：`exp`、`sigmoid`、`tanh`、`relu` 四个的库半边在 develop 上今天就是这条，`exp_gradient_through_the_library` 等四条已作为"已知失败"钉住。也就是说这不是三个新算子的问题，是**一条已经吃掉了 7 个算子库半边的墙**，需要一次判决：它是设计，还是缺口。

## 1. 读数（实测，含树、时间、负载）

树 `/home/zjwei/camel-work/tranche2`（分支 `lane-tranche2`，基点 `34b659107`），宿主 88zjwei（32 核），2026-11-04 04:2x，1 m load 18.07/18.67/20.08。

1. **同一条律，形状写在类型里就通，写成 `[..]` 就拒。** `erf_gradient_both_legs` 的库半边写成 `func total_plain(x: float[4])` 时读数 `engine=[0.8787825789354448, 0.4151074974205947, 0.6429310691952074, 0.020666985354092053] library=[同] same=true`；只把那一处改成 `float[..]`，引擎半边照常打印，库半边停：

   ```
   TRANSPOSE function=split_of_jvp_of_total_plain_backward skip=cotangent-needs-a-value uri=tensor:multiply position=0 reason=the rule's right side reads a value the caller does not carry
   ```

   `erfc`、`gelu` 逐字相同（`gelu` 的 `uri=tensor:multiply position=0` 也一样）。三者的正半律本身是对的：同一棵树上它们的引擎读数与库读数在 `float[4]` 下**逐位相同**。

2. **缺口在 `mul_tensor_t` 的答案里，不在走法里。** 那条律的答案是 `sum_to(ct * w, x)` 与 `sum_to(ct * x, w)`（`stdlib/rulesets/transpose.cml#mul_tensor_t`）：它要把余切**约到操作数自己的形状**，而形状是一个值。`modules/transforms/transpose.cpp#buildTransposeMember` 里 `knownValue` 只认两种来源——常量，或者"转发得出来的值"；一个**参与微分的参数**没有值（它只有余切），于是 `sum_to` 的 `like` 无值可读。同一处代码对"类型写明形状的参数"有一条专门的出路：`shapeCarrierOf` 用一个该形状的零数组当**形状载体**（`transpose.cpp` 里"answered from the type"那一段），这就是第 1 条现象的全部原因。

3. **这条墙今天吃掉的算子**（都在同一棵树上量过，红都是这一行）：`exp`、`sigmoid`、`tanh`、`relu`（图标"已知失败"），加上这一批的 `erf`、`erfc`、`gelu`。**七个闭式函数的库半边**，它们的正半律都已写下、都正确。

4. **反面：需要"按值缩放"以外的算子全都通。** 同一批 8 个里，`transpose`（律里只有 `transpose`）、`linear`（只有 `matmul`/`transpose`/`+`）、`where`（只有 `where`）、`max`/`min`（`where` + `sum_stated`）在 `float[..]` 下**不需要写形状**就 `same=true`。所以墙的形状是清楚的：**"余切乘上一个值"需要形状，"余切挪、选、加、矩阵乘"不需要。**

## 2. 三条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：不读（默认）** | 程序把形状写进类型（`float[4]`）时库半边工作；`[..]` 时按今天的口径拒绝，律与用例照写 | **7 个算子的库半边对 `[..]` 仍红**；作者要在用例里写死形状。不动 `modules/transforms`，今天就能落地 |
| **乙：让 `mul_tensor_t` 不读形状** | 给"两个操作数同形状"一条不约的答案 | 规则的右手边**没有语法**说"这两个操作数同形状"。要能写，得先给规则加一条形状相等的前提（类型层），或者给 `mul_tensor` 拆一个"同秩同形"的专门版本——两者都是语言/类型层的新东西 |
| **丙：加一个不读形状的缩放算子** | 例如 `scale(x, s: float)`，律是 `ct * s`（`s` 是标量，没有形状可读） | 能解掉"乘一个标量"的一半；`dx * slope` 里 `slope` 是**数组**（逐元素斜率），仍要 `mul_tensor`。且新算子要在 `stdlib/signatures/tensor.cml` 与 C++ 侧一起加，超出本 lane 的四个可改路径 |

第四条（今天先不做）：让 `shapeCarrierOf` 对 `[..]` 也回一个载体——但 `[..]` 恰恰是"形状未知"，载体只能是猜的，等于把缺席的形状编出来。

## 3. 我方默认

**甲。** 理由三条，都是量出来的：① **今天的事实就是甲**——7 个算子的库半边已经在拒绝，这一批没有把任何一个变红；② 甲的代价是"作者要写形状"，而这个仓库里已经有用例这么写（`grad_array_fixed_shape`、`transpose_product_fixed_shape`），不是新写法；③ 乙、丙都要动 `modules/transforms` 或 C++ 注册面，本 lane 的可改路径只有两个 ruleset 与 `test/`，做不了。

落地时要盯的一句：**若将来 `[..]` 走通，`erf/erfc/gelu` 三个用例的 `float[4]` 应当去掉**——那时它们会变成"形状与律无关"的更强读数。今天不改，因为改了就是红的。