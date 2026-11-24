# CI 四条腿全红，拆开是四件事

作者 2026-11-22 00:40 点名「修完 CI 后在 develop 头上跑一次 CI」。跑了（run `44925835910`，commit `212fb0596`），四条腿全 `failure`。本页把那些红拆开，因为它们是**四件互不相干的事**，混着读会把「修掉一个」读成「修好了」。

## 发生了什么（run `44925835910`，`212fb0596`）

| 腿 | 红 |
| --- | --- |
| `linux-x64`、`linux-x64-nojit` | `perf_fib_smoke.opt_twice.{nvm,fvm,jit,prl}`；`gpu_scope.a_run_without_a_device_names_its_own_configuration` |
| `win-x64` | `print_then_run_agrees_with_the_origin`；`erfc_grad.f64`；`gpu_scope.…`；`rules.accuracy_*` 三条；`load_class.shared_place` |
| `macos-arm64` | `narrow_reductions_four_backends`；`gelu_grad.*` 两条；`erf_grad.f64`；`erfc_grad.f64`；`gpu_scope.…`；`rules.*` 两条 |

整轮 `[SUMMARY] total: 9827, pass: 9768, fail: 5`（linux 那次）。`known failures: 41`。

## 四类归属

**一、性能预算（linux 独有，四个后端同一 plan）。** `opt_twice` 实测 `5.07 / 5.49 / 5.31 / 5.33 s`，预算 `5.00 s` —— 超 `1.4%`–`9.8%`。四个后端同时、幅度接近，是否符合「真退化」的形状要看这条预算的历史读数，本页不下判：**只报不猜。**

**二、`gpu_scope` 一条（四条腿全红）。** 已在 `29538ed61` 修：那句「这是主机选择」的回答原本写在 `#ifdef CAMEL_TILES_CUDA` 里面，无头构建把它编译掉了，于是 `#else` 的「built without CUDA Toolkit headers」抢了先 —— 而那句话该留给**点名要设备**的运行。修后无头构建该用例绿、有头构建 stdout 逐字节不变。**但 win/macos 也红这一点说明本页写的时候还没验完整**：那两条腿的机器与「有没有 CUDA」是另一回事，需要单独确认。

**三、平台精度（`erf/erfc/gelu_grad.f64`、`rules.accuracy_*`）。既有账，作者已裁定。**
- 那三条 `f64` 用 `expect_stdout_exact` 逐字节比十进制，而 f64 路径没有平台分支（`elementwise.cpp` 的 gelu/erf/erfc、`gradient.cpp` 的 `std::tanh`/`std::exp` 都是工具链 libm）。实测差 **1 ulp**：win 上 `0.15729920705028513` 得 `0.1572992070502851`。
- `rules.accuracy_*` 的机制更硬：判据的参考值是 `long double`（`modules/rewrite/module.cpp:1832`「the reference must hold more precision than the element type being judged」），而**只有 linux 的 `long double` 比 `double` 宽**；MSVC 与 Apple arm64 上二者相同，于是被判值自身那 1 ulp 舍入成了「误差」。win 与 mac 打的是同一条 `refuted accurate_log1p … error 7.28033e-16 exceeds the original's 0 (ratio 1.63938)`。
- 前两个 develop run（`44878175494@526aa6c9`、`44676862205@8fab0d3e`）同红且**数字逐字节相同** ⇒ 与 `212fb0596` 无关。作者 2026-11-04 `d61b51980` 已把这十处断言判为「断言错了」（甲类，见 `2026-11-04-ten-numeric-assertions-and-the-declared-ulp-bound.cn.md`），状态「尚未改」。

**四、构建差（mac 独有）。** `gelu_grad.bits.generic_unit`：`--tensor-isa generic` 在 mac 打出的正好是向量单元那组摘要。原因不是位数，是 `modules/tensor/CMakeLists.txt:62` 给 `gradient_generic.cpp` 加了 `-ffp-contract=on`，而 `vmath::tanh` 在 `kernel…inl:40` 的 `#pragma clang fp contract(off)` **之前**被包含；arm64 基线有 FMA ⇒ desc 里「无 FMA 的机器也给这组摘要」在 arm64 上不成立。要跨平台同摘要只能自带 erf/tanh 实现 —— **作者裁定项，不夹带。**

## 这一页要拦住的误读

**「四条腿全红」不是一件事。** `29538ed61` 修掉的是第二类的一条，**不能**当成「CI 修好了」：第一类未判、第三类是作者已裁定而尚未改的十处断言、第四类是构建顺序。

## 下一步（本页的建议，未派工）

1. **第三类**有裁定在手（「断言错了」）：把 `expect_stdout_exact` 改为按声明界比较（逐元素 ulp），把 `rules.accuracy_*` 的参考值改成**显式宽类型**而不是依赖 `long double`。这两项都在裁定范围内，但**要一次改完并逐条报改前后的值**，不要分批。
2. **第一类**先取历史读数（这条预算在过去若干 run 的实测值），有读数再判是退化还是卡边界。
3. **第二类**需要在 win/macos 上确认 `gpu_scope` 的另一条路径。
4. **第四类**等作者裁定「要不要自带实现」。