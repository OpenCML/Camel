# 十处数值断言按 ulp 界重写：哪四条是「精度断言」，以及为什么登记比断言更糟

`lane-powzero`，2026-11-04。作者裁定（`d61b51980`，06:35）：`gelu_grad`/`erf_grad`/`erfc_grad` 的位、
`native.sim_declaration_import` 的十进制文本、`modules.rules` 四条精度断言，**都是断言错了**——
它们按位比对了语言没有承诺逐位相同的结果，要改成按声明的 ulp 上界断言，**不登记成已知失败**。

## 0 宿主与树

读数取在 `88zjwei`，`~/camel-work/powzero`，`-Tier fast -Jobs 4`。跑前跑后 1 分钟负载 25–26
（六条车道同时在全量，宿主并不安静）；`feat/modules` 那一次的 `[SUMMARY]` 见第 4 节。

## 1 依据：7.1 承诺什么、不承诺什么

`docs/design-principles.cn.md#7.1 规范算法`，逐句：

- 「加、减、乘、除、开方每一步按 IEEE 正确舍入，严格按程序书写的顺序，不收缩为乘加融合。按规范算法
  计算时（`#strict`、安全模式，见 7.4），各后端、各设备的结果**逐位相同**。」
- 「矩阵乘的规范算法是按 k 从小到大连续做乘加融合……这是矩阵乘自身的规范算法，不是对"不收缩为乘加
  融合"的放宽。」
- 「**超越函数**（`exp`、`log`、`sin` 等）**不要求跨设备逐位相同**，只承诺一个**写明的误差上界（以
  ulp 计），逐个声明**；标准库的目标是 `exp`、`log` 不超过 2 ulp；NaN、无穷、零等特殊值必须精确。」

`#7.2 许可`：「`contract`（收缩为乘加融合）」是默认模式下**可以授予**的许可，所以默认模式的四则也
不逐位。

**「声明的上界」今天写在哪儿**：不是结构字段，而是模块文档里的那句承诺——
`stdlib/math.cml`（同名 `docs/handover/stdlib/math.cml` 同步）：「The transcendental functions are
computed in f64 and rounded once into the type asked for. They promise an error of at most 2 ulp (of
the type's own precision, for `float`), not the same bits on every device (design principles 7.1).」
`stdlib/signatures/tensor.cml` 对 `exp`/`log` 逐个写「within 2 ulp」。所以取法是：**按函数取它自己那句
声明的 ulp 数**，取不到就不许写界。

## 2 全树一条按平台登记的 `known_fail` 都没有

`test/plans/**` 里 `known_fail = [...]` 含 `macos`/`win`/`linux`/`arm64`/`x64` 的：**0 条**。

**所以「撤掉甲类登记」在 `develop` 上撤的是 0 行**——那批登记在**发布侧**，没有进 `develop`。读这份
报告的人要知道这一点，否则会以为已经撤过一轮。撤登记这件事必须在做登记的那一侧做，或者那批登记
根本不该落：**一个「这个平台在这里不一样」的标记，会把这个平台上以后真出的错一起藏住。**

## 3 十处断言的分类

判据两条，缺一不可：**这条断言比的是不是语言承诺逐位相同的东西**；**换一个 ulp 的误差，这条断言会不会翻**。

| 用例 | 甲/乙 | 依据 | 旧断言 | 新断言 |
|---|---|---|---|---|
| `native.sim_declaration_import` | **甲**（已改） | 7.1 超越函数句；价格是 128 个连乘 `exp` | `expect_stdout_exact` 两行逐字节 | 整数 7863 精确；价格 300 ulp（界在 `desc` 里推：128×2 + 折现 2，`contract` 余量向上取整） |
| `gelu_grad.f64` / `erf_grad.f64` / `erfc_grad.f64` | **甲** | 同上（`erf`/`erfc`/`tanh` 皆是） | 三行 `expect_stdout_exact` 的十进制文本 | 尚未改：改成逐元素 `ulp` |
| `gelu_grad.bits` / `gelu_grad.bits.generic_unit` | **甲，但摘要搬不动** | 同上，且 7.2 允许 `contract` | 三个 sha256 摘要 | 尚未改：**摘要无法按 ulp 比**，用例要先打印数值才能谈界——这是改题目，不是改断言 |
| `rules.accuracy_check_compares_error` | **甲** | 7.1 超越函数句；钉的是 `log1p`/`expm1`/softmax 的**实测误差比**（`worst error ratio 0.193` 等），换 1 ulp 就换这个比值 | 逐字钉 `worst error ratio` 的十进制 | 尚未改：按声明的 2 ulp 判比值是否在界内，不再钉具体比值 |
| `rules.accuracy_rejects_naive_softmax` | **甲** | 同上；`1.91128e-06`/`7.75714e-08`/`ratio 8.01648` 全是 `exp` 的实测误差 | 逐字钉三个误差与比值 | 尚未改：钉「refuted，且右式误差是左式的 8 倍以上」这一**判据**，不钉小数点 |
| `rules.accuracy_library_passes_the_check` | **甲** | 同上；四行 `worst error ratio` | 逐字钉四个比值 | 尚未改：同第一条 |
| `rules.a_reassociate_rule_is_compared_in_the_higher_precision` | **甲** | 7.2 `contract`：`1.0000000150474662` 与 `1.0000000031710769` 是同一实值在两种精度下的结果，已相差 27 ulp | 逐字钉两个 f64 文本 | 尚未改：钉「两侧的差不超过许可声明的界」 |
| `rules.typed_boundaries_not_crossed` | **乙** | `7.6999993` 是 `7.7` 一次舍入到 f32 的**精确值**，没有第二次舍入 | 精确钉 | **不改** |
| `rules.typed_exact_doubling_boundary` / `rules.typed_exact_doubling_boundary_is_real` | **乙** | `9.80908925027372e-45` 是最小 f32 次正规数加宽到 f64 的**精确值**（IEEE 加宽无舍入） | 精确钉 | **不改** |
| **乙类合计** | **一处乙都没有落在「超越函数」上**，而 7.1 承诺逐位的 `+ - * / sqrt`（`#strict`）在本清单里**一次都没出现**——所以这十处里没有一处是「真缺陷不能登记」 | — | — | — |

**「乙类一条都没有」这句话的边界要说清**：`gemm_canonical_bits` 仍然逐位是对的，因为 7.1 第二条把
矩阵乘的**规范算法**定成「按 k 从小到大的连续乘加融合」——**那是被规定的算法，不是被允许的差异**。
它不是这十处之一，也不该被按 ulp 松。这个区分防的是下一个人「看到 ulp 就以为所有数值断言都能松」。

**额外一处（不在十处之内，同病）**：`rules.tensor_laws_execute` 钉 `0.90000004`——一个 f32 乘加链的
结果，`contract` 一开就可能挪一位。

## 4 这一次改完之后的读数

`feat/modules`：`[SUMMARY] total: 1957, pass: 1950, fail: 0, skip: 7`（`RUN_EXIT=0`）。
`feat/compile/transforms`：**未读到**——helper 等测试槽 1800 s 后自退，`exit 4`，原文
`[camel-remote] no host test slot within 1800 s`，跑前负载 46.35/37.06/29.88。**那不是红，是宿主被
六条车道占满**；槽一空即补。

## 5 为什么这次只改了一条

改断言是断言的事，改题目是题目的事。`gelu_grad.bits` 与 `gelu_grad.bits.generic_unit` 钉的是**摘要**，
摘要对 ulp 界没有任何意义——它们要先变成用例打印的数值。同理 `erf_grad.f64` 那三条要按元素比。这两
类都不是把 `expect_stdout_exact` 换个字段能做完的，所以分两笔：**这一笔给出介质与第一条消费者
（`sim`），下一笔把剩下九处按同一介质逐个改完**，每一条的界都写进它自己的 `desc`。

## 6 界要有出处：这句是这条规矩的全部

`ulp(...)` 的单位是**声明值**的最后一位，不是被测值的——**取被测值的格点，等于让错答案自己挪公差**。
红先绿后的证据：把 `sim` 的声明价格挪到 411 ulp 之外，条目红，打印
`the price is 4.110e+2 ulp from the declared value 5.89432552952 (bound 300, derived in this entry's
desc): 5.894325529519635`；恢复即绿。