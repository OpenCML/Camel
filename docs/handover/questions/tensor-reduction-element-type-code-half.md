# 归约返回元素类型：裁定的**代码**那一半落在哪，以及规范定义库怎么跟

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的行号与读数以那条线为准。


提出者：`handover/h451-flip-entry-args`（2026-10-24）。裁定在
`docs/handover/reviews/questions-2026-10-23.md` 的 *f32-scalar-to-the-device 与 tensor-reduction-element-type*：
`sum`/`mean` 对 `Tensor<f32>` 是 `f32`、对 f64 是 `float`，累加在 f64 中、舍入一次，
"现有程序里把结果交给 `float` 的地方改写显式转换（不计兼容）……**现在先按原则改正，不等 P4**"。

> **2026-10-26 更新**：**代码那一半已经落了**（`handover/h579-reduction-element-type`）——
> `sum`/`mean` 的整体形现在答元素类型，law 表、`stdlib/signatures/tensor.cml` 与 tiles 侧一起跟上；
> 本文 §一 与 §二 记的是落地**前**的读数，§五 是那一轮的实测，都保留作历史。
> 落地后剩下的 17 条新红（其中 16 条就是本文 §四 问题 2 那一条定义库的 `mean`）见同目录
> `whole-reduction-element-type-residual-reds.md`；§四 问题 2 仍归作者裁定，
> §四 问题 1（归哪条车道）已由本轮回答：归一条能动 `modules/tensor` 与 `test/cases/**` 的车道，
> 本轮就是它。

本 lane 的任务书把 `feat.modules.definitions` 的 10 条 `ReturnTypeMismatch` 报成"归约返回元素类型"这条。
**实测下来那 10 条不是归约**（见 §二），所以这条裁定的代码那一半到本轮为止仍然没有人做。
把它记在这里，免得它随"10 条已转绿"一起消失。

## 一、今天的状态（本树实测）

`tensor:sum` / `tensor:mean` 仍然只返回 `float`，而按轴的 `sum_axis`/`mean_axis` 已经是元素类型——
这正是裁定说的那个"不一致"：

- `modules/tensor/ops/reduction_ops.cpp:122` `scalarFloatInfer` 就是 `return Type::Float64();`，
  被 `:349`（sum）与 `:358`（mean）用作 `.infer`；
- `modules/tensor/ops/indexing.cpp:881-894` 的 law 把结果写死 f64：
  `sumLaw("sum_f32", f32, f64, f64)`、`meanLaw("mean_f32", f32, f64, f64, LawEmpty::Fault)`；
- 同一段的 `sum_axis`/`mean_axis`（`:896-910`）已经是 `sum_axis_f32 -> f32`、`mean_axis_f32 -> f32`。

判据：`func f(): float { return sum(<f32 张量>) }`（`mean` 同）在本树编译得过、退出 0，
所以两个名字今天对 f32 张量给出 f64。裁定要的是 f32。

## 二、为什么本 lane 没有顺手落它

那 10 条红的唯一根因是**秩 1 元素读取**，不是归约。逐字（`camel test/cases/definitions/kernels.cml`）：

    error[SemanticDiag::ReturnTypeMismatch]: Return type 'f32' does not match the function's declared return type 'float'.
       --> test/cases/definitions/kernels.cml:10:29
     10 |     return 0..numel(t)->map<(i: int): float => v[i]>
        |                             ^^^^^^^^^^^^^^^^^^^^^^^

`v = reshape(t, [numel(t)])` 是秩 1，`v[i]` 取到的就是元素，类型是元素类型 `f32`
（rank-zero-tensor 裁定：秩 1 处取到的是元素 `T`）。改法是裁定说的显式转换 `float(v[i])`，
金标一字不动——本 lane 已经这么做，`feat.modules.definitions` 23/23 绿。
那条诊断与 `sum`/`mean` 的返回类型无关，所以"10 条绿了"不等于这条裁定落了。

## 三、落这条裁定要动的地方（按代码读出来的，本 lane 未实测）

1. `modules/tensor/ops/reduction_ops.cpp` 的 `scalarFloatInfer`（f32 张量给 f32，f64 给 f64），
   以及 `modules/tensor/kernels/reduce.cpp` 里整个张量求和/求均值的**一次舍入**（今天按 f64 出结果）。
2. `modules/tensor/ops/indexing.cpp:881-894` 的六条 law 的结果类型。
3. `test/cases/definitions/grad.cml` 的 12 个 `: float` 函数（`return sum(...)` / `mean(...)`），
   按裁定的"改写显式转换"加 `float(...)`。
4. `test/plans/feat/modules/golden/definitions/kernels.txt` 末尾 5 行 `println(mean(...))`：
   今天钉的是 f64 累加文本，改成 f32 之后要重录；`gradients_numel_shape` 的
   `expect_stdout_exact = "2.5\n1.0\n"` 也可能随之动。

## 四、要裁定的两句

1. **代码那一半归哪条车道**：是 P4（`handover/h438-tensor-alias`，裁定说"不等 P4"）顺手做，
   还是单开一条？它对 `modules/tensor/ops/**` 与核心打印/求值都要动，与本 lane 的 14 条无关。
2. **规范定义怎么写**：`stdlib/tensor_definitions.cml` 的 `mean` 是
   `sum(x) / (numel(x) as float)`，而 `feat.modules.definitions` 的 `kernels_expanded` 要求
   内核与**展开后的定义**逐位相同。`sum(x)` 变成 f32、`numel(x) as float` 仍是 f64 之后，
   定义给出的 `mean` 按不按元素类型算？
   - (a) 定义也改写元素类型：`sum(x) / (numel(x) as f32)`（定义与内核同一个类型故事）；
   - (b) 保持 `as float`，"除以 float 再舍回元素类型"，由求值/打印一侧补那一次舍入。
   两条都能让"内核 == 定义"继续成立，但定义库是规范文本（设计原则 7.1），所以由作者选。

## 五、一轮"按元素类型做整体归约"的实测（翻转线 `09192bf70`）

本轮的任务是把它在翻转线上按那条归约轮分支（`handover/h536-reduction-round`，只作参考读、未并入）的
规则重做。做法是先量，再决定落哪一半；结论是**代码那一半一条都不在 tiles 车道的允许文件集内**，而 tiles
那一半单独落地读不到任何行为，所以本轮只补这份读数（问题 1 由此有答案）。

### 5.1 改前逐字：`sum` 对 `Tensor<f32>` 今天答 `float`

临时探针（跑完即删，不入库）：

    func total(x: Tensor<f32>): f32 { return sum(x) }
    func main(): int sync { println(total(reshape([1.0, 2.0, 3.0] as f32[], [3]))); return 0 }

`camel <case> std::nvm` 的读数：

    element_typed_reduction_probe.cml:4:35: [Error]: Return type 'float' does not match the
    function's declared return type 'f32'. (name=SemanticDiag::ReturnTypeMismatch, code=0x03000011)

同一程序把返回类型写成 `: float` 就编过，打印 `6.0`。即今天 f32 张量的整体归约给 f64；那条参考分支上是
同一条诊断的反向（`Return type 'f32' does not match ... 'float'`），因为那一半已经落了。

### 5.2 区域那一半为什么不能单独落

- `modules/tiles/src/compiler.cpp:865-880` 只在 `inferred` 是 Float64 时取整张量归约，并把它注成
  `DType::F64`；`inferred` 来自 §一 的 `scalarFloatInfer`。把这里的守卫放宽成"F32 或 F64"今天**没有输入**
  能满足：语言层（§5.1）根本不产出 f32 的整体归约。
- 更硬的边界是 **law 表**，不在 tiles 里：`modules/tiles/src/reduction_kernel.cpp:95-102` 要求整张量形的
  `law.result == f64`，而 law 由 `modules/tensor/ops/indexing.cpp` 声明（§一的 `sum_f32(f32, f64, f64)`）。
  只要那条 law 还写 f64，tiles 侧接受 f32 注解也过不了这道检查，除非同时改 `modules/tensor`。
- 所以 tiles 那一半（`compiler.cpp`、`reduction_kernel.cpp`、`schedule.cpp`、`serial_kernel.cpp`，加上
  `test/cases/modules/tiles/plan_check.cpp:652` 那条"f32 注解决不被接受"的钉子）今天落地只做两件事：
  放宽一条钉子、加一段读不到的死代码。

### 5.3 参考读数：那条分支的 tip（`770d956ff`）在 `test/plans/gpu/tiles.plan.toml` 上

`total: 101, pass: 78, fail: 23`（经主机实测）。其中 12 条是这条规则的**直接**结果——函数声明
`: float` 而返回归约，诊断逐字是 `Return type 'f32' does not match the function's declared return
type 'float'`（`branches.cml:7` ×3、`matmul_edges.cml:9` ×3、`grad_expand.cml:44` ×3、
`grad_dynamic.cml:27`、`grad_expand_plain.cml:27` ×2）；另外 11 条是数值读数
（`value_gradient` ×3、`resident_scalars` ×3 报 scalar precision，`numeric_permission_boundaries` 与
`numeric_permissions_moving_gc` 报 `-8.526513e-14`，`matmul_rounding` ×3 报 FMA 链残差
`-1.4210855e-14`）。那条分支自己的页在更早的 tip 上记过 13 条并 A/B 证明不是它那条车道；13 与 23 的差
没有进一步拆开，这里只报读数。翻转线上同一个计划今天是 `101 / pass 101 / fail 0`。

### 5.4 §四 问题 1 的答案

代码那一半要动的是：`modules/tensor/ops/reduction_ops.cpp`（`scalarFloatInfer`）、
`modules/tensor/kernels/reduce.*`（一次舍入）、`modules/tensor/ops/indexing.cpp`（law 表的结果类型）、
`stdlib/signatures/tensor.cml`，以及把归约结果交给 `float` 的程序——本树有 206 个 `.cml`、346 处
`return sum(`/`mean(`。**没有一条落在 tiles 车道的允许文件集内**，所以它归一条能动 `modules/tensor` 与
`test/cases/**` 的车道，tiles 侧那 4 处跟随其后（参考分支的 `7e8608546`、`a2334af46`、`d5895e7b5` 就是
那 4 处的样子）。§四 问题 2（定义库怎么写）仍归作者。