# bf16/f16 的元素 epsilon、舍入模式与"能不能在 CPU 上跑"（实测，2026-10-24，h432 lane）

本文件报三件需要作者裁定的事，都由 h432「bf16/f16 单开一条车道」这一片**实测**得出。
裁定出处按 reviews 的章节引（`docs/handover/reviews/questions-2026-10-23.md`
§`check-generic-instance-space` 第 3 条；该文件按约定会被删，所以下面引的是它当时的节名与内容）。
**本 lane 只落了裁定逐字给的部分，没有自己发明语义。**

已经落地的部分（不再需要裁定，只作为背景）：`bf16`/`f16` 是语言的一等浮点类型——字面量、
`bf16(x)`/`f16(x)`/`x as f32`、标量算术与比较、数组与张量的元素类型、`array::sum` 与张量归约、
`math::min/max/abs`，都在；造不出实例/没有重载的按名字拒绝
（`test/cases/modules/stdlib/reduced_*.cml`，`test/plans/feat/modules/stdlib_reduced.plan.toml`）。
本片加的是 `check` 一侧：采样器能造出 `bf16`/`f16` 的值（标量与两字节元素的张量），
两个类型的 epsilon 分开取（`include/camel/core/operator_semantics.h#elementEpsilon`：
bf16 = 2^-8、f16 = 2^-10），`float_add_minus_zero<bf16>` 这类实例从
`evidence none / the check cannot make a value of type 'bf16'` 变成 `evidence samples 200`。

## 一、误差界里的 epsilon：四个类型现在有两种含义

`RewriteErrorBound.ulps`（`include/camel/core/operator_semantics.h`）的注释说它"以元素类型的
epsilon 与量级为单位"，而实际参与比较的常数在两个地方：

| 类型 | unit roundoff（一次舍入的最大相对误差） | 相对 ulp（相邻两个值的间距上界） | 裁定给的 epsilon | check 今天比较用的数 |
|---|---|---|---|---|
| bf16 | 2^-8（1 隐式 + 7 显式 = 8 位有效） | 2^-7 | **2^-8** | 2^-8 |
| f16  | 2^-11（1 隐式 + 10 显式 = 11 位有效） | 2^-10 | **2^-10** | 2^-10 |
| f32  | 2^-24 | 2^-23 | （本次裁定只写"与 f32 的 2^-24 分开"） | 1.2e-7 ≈ 2^-23 |
| f64  | 2^-53 | 2^-52 | 同上 | 2.3e-16 ≈ 2^-52 |

`error bound` 的公式是 `allowed = (ulps * eps + relative) * magnitude + ulps * smallest`
（`modules/rewrite/module.cpp#agreeFloat`）。今天的 `eps` 对 bf16 取 unit roundoff（2^-8）、
对 f16 取相对 ulp（2^-10）、对 f32/f64 取相对 ulp（2^-23 / 2^-52）：**四个类型不是同一个含义。**

**要裁定**：`RewriteErrorBound` 里的 epsilon 是哪一个数？

- 选项 A：**unit roundoff**（一次舍入的最大相对误差）。那么 f32 应改成 2^-24、f64 改成 2^-53，
  四个类型同义。代价：每条 f32 规则的界**收紧一半**，`check` 的 f32 读数会移动（已知的
  `refuted tensor_f32_combine_mixed` 之外还可能有别的实例改变判定），属于一次独立的数值契约改动，
  要做在它自己的车道上、按"重新测量一批读数"处理。本 lane 的验收明文要求 f32/f64 读数逐字不变，
  所以本 lane **没有做**这件事。
- 选项 B：**相对 ulp**（一个 binade 内相邻值的间距上界）。那么 f16 = 2^-10 与 f32/f64 一致，
  bf16 应改成 2^-7。代价：bf16 的界放宽一倍——`float_combine<bf16>` 这类 `reassociate` 规则
  本来就在界内（实测余量约 2–3 倍），放宽不会让它们假绿，但会让"bf16 是最低精度"这句话
  （`change_precision` 用 2^-8，`docs/design/rewrite/plan-verification.cn.md` 也写 2^-8）
  与误差界不再用同一个数。
- 选项 C：**照今天这样，逐类型各自取**（bf16 2^-8、f16 2^-10、f32 2^-23、f64 2^-52），
  在 `elementEpsilon` 与 `checkEpsilon` 的注释里写明为什么。代价：`elementEpsilon` 声明的
  f32 = 2^-24 与 `checkEpsilon` 比较用的 1.2e-7 = 2^-23 是两个数，读者必须读注释才知道区别；
  本 lane 取了这一条（它是唯一能同时满足"把裁定的 bf16/f16 数落实"与"f32/f64 读数一条不许动"
  的写法）。

**本 lane 的倾向**：先落 C，然后把 A 单独开一条车道（它需要重测一批 f32 读数，且要作者确认
"收紧 f32 的界"是可接受的读数变动）。

## 二、bf16/f16 的舍入模式没有被写下来

`include/camel/core/rtdata/half.h` 实现的是"store rounding = round to nearest, ties to even，
只舍入一次"，`modules/tensor/dtype.h` 与 `modules/math/operators.cpp` 都按这条写。
实测：`reduced_scalar` 钉住了两处 tie（`1 + 2^-8` 是 bf16 1.0、`1 + 2^-11` 是 f16 1.0，
都是偶数邻居）、溢出到无穷而不饱和（`65504 + 16` 是 f16 `inf`）、以及 f16 的最大有限值
打印成 `65500.0`。但设计原则 7.1 的"规范算法"只对 f32/f64 写了每一步 IEEE 正确舍入；
7.6 的"同一设备逐位相同"也没说 bf16/f16 的舍入模式是规范的一部分。

**要裁定**：bf16/f16 的规范算法是否也钉在 RTNE（一次舍入）？若某个后端或库提供了别的舍入
（f16 在设备上常见的 RZ 变体、或张量内核里 acc 的另一种舍入），它是"同一条规范算法的另一种实现"
还是"需要 `change_precision` 的精度改动"？代价：按前者，后端之间必须逐位相同（7.6），
需要为它加逐位一致的用例；按后者，`change_precision` 的语义要从"降低精度"扩到"改变舍入模式"。

## 三、bf16/f16 是否允许在 CPU 上运行（不只是类型层面）

今天 CPU 侧是"在 f32 里算、舍入一次"的等价路线（标量算子 `src/builtin/operators/half_ops.cpp`、
张量内核 `modules/tensor/kernels/*`），GPU 侧 `tiles` 有 `cublas_bf16` 这一条实现名；
`f32-scalar-to-the-device` 的裁定只落了"设备侧接受元素类型的标量叶子"。

**要裁定**：CPU 上是否要求存在**独立的** bf16/f16 内核（例如用 CPU 的 BF16/F16C 指令），
而不是今天这条"f32 计算 + 一次舍入"的路线？

- 选项 A：不要求，今天的路线就是 CPU 的规范实现。代价：CPU 与 GPU 的 bf16 结果可能不同
  （GPU 的 `cublas_bf16` 累加方式不同），7.6 只承诺"同一设备同样配置"，跨设备逐位相同本来
  就不是默认，所以这条不需要额外的用例。
- 选项 B：要求，且它要与今天的路线逐位相同（那么内核对每个元素的行为必须仍是"f32 算 + 一次
  舍入"）。代价：一条只钉"有这条内核且逐位一致"的用例，以及 CPU 侧的一条 `#impl` 声明。

**本 lane 的倾向**：先 A（今天的路线已经逐位可复现，`reduced_scalar`/`reduced_reduction_bytes`
钉住了每个元素的行为），把"是否要有独立内核"留给性能车道；真要 B，需要同时钉住"与等价路线逐位相同"。

## 四、实测到的真反例：`float_combine_mixed` 的守卫在 f16 上不成立（本 lane 记进了 known refuted）

`float_combine_mixed::<T>(x: T, const c1: T, const c2: T) when is_float::<T>() && combineMixedSign(c1, c2)`
`#requires<reassociate, no_infs, no_signed_zeros>` 的两侧是 `x * c1 + x * c2 => x * (c1 + c2)`。
本 lane 让 `f16` 能被检验之后，它在种子里落到了真反例（逐字读数，`camel <probe> std::nvm`，
`check([float_combine_mixed], 200, 7)`）：

```
refuted float_combine_mixed<f16>: proposition real equality + IEEE on special values, evidence counterexample, the rewritten left side differs at (10000.0, 39.0, -55.0): nan and -inf (bound 11 ulps, 0 relative, scale 100020001)
```

`10000.0`、`39`、`-55` 都是 f16 里普通的元素（不是特殊值）：`10000 * 39 = 390000` 与
`10000 * (-55) = -550000` 都超过 f16 的最大有限值 65504，于是左侧是 `inf + (-inf) = NaN`，
右侧 `10000 * (39 + (-55)) = 10000 * -16 = -160000` 溢出成 `-inf`。守卫 `combineMixedSign`
只查系数有限、非零、异号、`c1 + c2 != 0`，**没有查两个乘积本身有限**；而 `no_infs` 许可的是
"去掉一个无穷"，不是"把 NaN 变成无穷"——`reassociate` 的命题是
`real equality + IEEE on special values`（`verification-licence-to-precondition` 许可 2），
IEEE 不认为 NaN 与 `-inf` 相等，所以这是一条真反例。

f32/f64 上同一个洞**存在但采不到**：要让 `x*c1` 与 `x*c2` 一个 `+inf` 一个 `-inf`，f32 需要
`x > 2e37`（而 `c1` 只能是采样器手里的 `1e30` 或 `[-64, 64]` 的整数），f16 只需要一个四位数值。
所以这不是 f16 特有的毛病，是**窄类型的量程把守卫的洞露出来了**。

**要裁定**（三选一，本 lane 没有自己选）：

- 选项 A：**补守卫**——给 `float_combine_mixed`（和它的裸项兄弟 `float_combine_one_mixed`、
  以及张量族 `tensor_f32_combine_mixed`）的 `when` 加上"两个乘积有限"的条件。代价：这是
  `stdlib/rules.cml` 的语义改动，属于 rules 车道；它会改 f32/f64 的 `fired N of 64` 读数
  （守卫更严，落到的样本更少），需要重测那两条读数。
- 选项 B：**读 `no_infs` 为"假设没有无穷"，于是产生非有限值的样本不在法则断言的定义域内**——
  检查器在规则带 `no_infs` 时不把"某一侧出现 NaN/无穷"算作反例（或直接跳过这样的样本）。
  代价：这是一条**检查语义**的改动（`modules/rewrite/module.cpp#agreeFloat` 现在对无穷的处理是
  `x == y`），会改变"特殊值上仍按 IEEE"这条命题的读法，并且要作者确认"采样到假设之外的值"
  该算反例还是该算没测。
- 选项 C：**保留现状**，把它记为库的已知反例（本 lane 取了这一条：按 H247 的先例，
  `verify/check_covers_every_rule.mjs` 的 `knownRefuted` 里加上 `float_combine_mixed`，
  注释里写清反例与"修守卫属于 rules 车道，这条随之删掉"）。

**本 lane 的倾向**：要么 A、要么 B，都不要长期停在 C —— C 只是把"这条法则在窄类型上不成立"
记在册子上，`refuted` 本身仍然按裁定硬红。另外这条反例**随种子出现**（种子里约 1/3 会落到它，
因为要恰好抽到 `x = 1e4`），所以 C 依赖于"覆盖用例的种子固定为 7"；A 或 B 落地后这个种子依赖
就消失了。

## 五、顺带实测到的一处缺口（不是裁定，是一条待办）：非泛型规则不查 `canDraw`

`check` 的"造不出这个类型的值"这条原因是 `checkGenericRule` 独有的
（`modules/rewrite/module.cpp`：`probe.canDraw(parameter)` → `the check cannot make a value
of type '...'`）。**非泛型**规则走 `checkOneRule`，那里没有这道检查：参数类型造不出值时
`Sampler::value` 直接抛 `std::invalid_argument("a parameter of a rule has a type the check
cannot make")`，它冒到 pass 边界上变成一条 pass 内部错误。实测（本 lane 的树，一条 `string`
参数的非泛型规则，`check([join], 200, 7)`）：

```
error[RuntimeDiag::PassInternalError]: Pass 'std::nvm' failed with an internal error: a parameter of a rule has a type the check cannot make.
help: The pass gave up without a diagnostic of its own: it built or was given a graph it cannot handle, which is a defect of the compiler rather than of the program. Try the pipeline without this pass or with the passes in another order, and report the program together with the pass list.
```

同一条探针在**改前**（`14fb8754a` 的干净检出 `h431-print-verdict`）与**改后**（本 lane）逐字相同，
退出码同为 1：这是既有缺口，不是本 lane 引入的。旁证是泛型实例反过来有这道检查
（`check_type_cannot_be_made.cml` 里 `join::<string>` 得到
`sampled join<string>: proposition equality, evidence none, reason the check cannot make a value of type 'string'`，
本 lane 的 `rules.check_names_the_type_it_cannot_make` 钉住了它）。**本 lane 没有改这条路径**
（不在裁定范围内，且改了会给非泛型规则新增一类行）。若口径应是"和泛型实例一样给一格 reason"，
这是一条独立的小车道。