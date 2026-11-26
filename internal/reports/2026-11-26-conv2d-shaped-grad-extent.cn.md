# `conv2d` 的 `*_shaped` 梯度：步长不整除时，形状静默地少了余数

结论先说：**错了，而且不报错。** 输入真尺寸 6×6 的时候，`conv2d_input_grad_shaped` 给出 **5×5**、`conv2d_kernel_grad_shaped` 给出 **4×4**，退出码 0，没有任何诊断；正版拼写对同一组张量给出 6×6 和 3×3。引入点是 `4c7476c82 conv2d_t: derive the answered operand's extents from the ones the gradient reads`，它在 `origin/integration` 的尖端上。

这不是"公式少写一项"能修的：在 `OH` 相同的前提下 `(H, K)` 有一族解，从 `(dy, kernel, stride, padding)` 反推只能给出其中最小的一支。所以修法不是改公式，而是**让被回答操作数的长度以值的形式进规则**（读值端口），或者这一族拼写随读值端口落地一起删掉。`*_shaped` 本页不动、不删。

## 1. 读数原文

用例 `test/cases/modules/nn/conv2d_shaped_derived_extent.cml` 在这棵树上跑出来的五行，原文（`[stdout]`，逐字）：

```
plain  dx=[1, 1, 6, 6] dw=[1, 1, 3, 3]
bias   plain=[1] shaped=[1]
derived dx=[1, 1, 5, 5] dw=[1, 1, 4, 4]
value  dx=5x5 dw=4x4 numel=25,16
divides dx=[1, 1, 7, 7] dw=[1, 1, 3, 3]
```

每行的来源（都是同一个程序，输入 `x = ones::<f32>([1,1,6,6])`、`w = ones::<f32>([1,1,3,3])`、`dy = ones::<f32>([1,1,2,2])`，stride 2，padding 0）：

| 行 | 调用 | 这一行读的是谁 |
| --- | --- | --- |
| `plain` | `conv2d_input_grad(x, w, dy, 2)` / `conv2d_kernel_grad(x, w, dy, 2)` | 正版拼写读它要回答的操作数，尺寸即 `x` 的 6×6、`w` 的 3×3 |
| `bias` | `conv2d_bias_grad(b, dy)` / `conv2d_bias_grad_shaped(dy)` | 两者都是 `[1]`；`conv2dBiasGradShaped` 的体就是 `return conv2dBiasGrad(dy, allocator);`，一行，和原版完全相同（`src/stdlib/tensor/kernels/conv.cpp:570`） |
| `derived` | `conv2d_input_grad_shaped(w, dy, 2)` / `conv2d_kernel_grad_shaped(x, dy, 2)` | **类型推导**的答案：所有尺寸都在类型里，`shape(...)` 被折叠，这一行是 `derivedInputExtents` / `derivedKernelExtents` 算出来的 |
| `value` | 同上，但 `dy`（核梯度那边是 `x`）经 `func opaque(t: f32[...]) => f32[...]` 传进来 | **内核**的答案：参数类型不声明尺寸，所以调用没有静态尺寸，这一行是 `conv2dInputGradShaped` 自己算出来并分配的形状（`numel` 25 / 16） |
| `divides` | `[1,1,7,7]` 的输入、`[1,1,3,3]` 的核、stride 2 的同一对 `_shaped` 调用 | 对照：`(7 - 3) % 2 = 0` 时反推是精确的，7×7 / 3×3 全对 |

`derived` 和 `value` 是两处独立实现的读数（类型推导一处、内核一处），这里两个都少 1，说明两处写的是同一个式子，不是谁抄漏了。

## 2. 推导

正向定义（`src/stdlib/nn/signatures/nn.cml` 里 `conv2d` 的注、`conv2dShape`）是

```
OH = floor((H + 2*padding - KH) / stride) + 1
```

令 `r = (H + 2*padding - KH) mod stride`，`0 <= r < stride`，则 `H + 2p - K = (OH-1)*S + r`，于是

```
H = (OH - 1)*S + K - 2p + r
```

代码里两处写的是 `H = (OH - 1)*S + K - 2p`，**比真值少 `r`**：

- 类型推导：`src/stdlib/nn/tensor_ops.cpp:186` `derivedInputExtents`（`{d[0], k[1], (d[2]-1)*stride + k[2] - 2*padding, ...}`），经 `shapedLikeDerivedExtents(0, 1, true)` 挂在 `conv2d_input_grad_shaped` 上（同文件 `:218`、`:821`）。
- 内核：`src/stdlib/tensor/kernels/conv.cpp:526` `conv2dInputGradShaped` 自己又写了一遍 `inputExtents[] = {dy->dim(0), kernel->dim(1), (dy->dim(2)-1)*stride + kernel->dim(2) - 2*padding, ...}`，然后 `geometry(...)` + `requireDy(g, dy)`。

核梯度是对称的，而且错在**另一侧**：从 `(x, OH, S, p)` 反推 `K`，`K = H + 2p - (OH-1)*S - r`，代码给的是 `K + r`（`derivedKernelExtents`，`tensor_ops.cpp:202`；`conv2dKernelGradShaped`，`conv.cpp:548`）——**大了 `r`**，4×4 而真核是 3×3。

**为什么静默**：`requireDy` 用反推出来的尺寸再算一次正向 `OH`，因为

```
H' + 2p - K = ((OH-1)*S + K - 2p) + 2p - K = (OH-1)*S
```

**正好整除**，`OH` 对得上，检查通过。核梯度那边一样：`H + 2p - (K + r) = (OH-1)*S`，也整除，也通过。于是"少一个余数"的形状被当成合法答案交出去。

**为什么不能靠换公式修**：`floor` 把区间 `[base, base + S - 1]` 的整片 `H` 映到同一个 `OH`（`base = (OH-1)*S + K - 2p + r` 取 `r` 从 0 到 `S-1`），反推只能落在区间左端。想知道真值落在区间的哪一格，就得知道 `r`，而 `r` 恰恰不在 `(dy, kernel, stride, padding)` 里。这是信息不够，不是算术写错。

**后果**：输入梯度那边，真梯度多出来的 `r` 行/列按定义全是 0（没有窗口读到它们），所以重叠部分数值相同、形状小一圈——下游把它累加进声明为 6×6 的切线时才会炸形状，或者悄悄丢掉尾巴；核梯度那边更糟，4×4 的窗口读到的是另一组位置，**形状和数值都错**。本页只取形状读数，数值的错是它的推论，未另测。

## 3. 用例与计划的钉法

- 用例：`test/cases/modules/nn/conv2d_shaped_derived_extent.cml`（五行读数 + `opaque` 那个不声明尺寸的直通函数）。
- 计划：`test/plans/feat/modules/conv2d_shaped_extent.plan.toml`（`feat.modules.conv2d_shaped_extent`，tier `fast`），三个测试：
  - `conv2d_shaped.plain_spellings_keep_their_operand_extents`：**绿**，四个后端（nvm/fvm/jit/prl）各断言一遍，钉住正版拼写的 6×6 / 3×3、两个 bias 都是 `[1]`、以及整除时（7×7）`_shaped` 也对。这是正控：同一个程序把真尺寸写在断言里，说明上面那两个错的不是"输入给错了"。
  - `conv2d_shaped.derived_input_states_the_input_extents`：**应当是红的缺陷**，断言"类型推导必须说出被回答操作数的尺寸"（6×6 / 3×3）。带 `known_fail = ["nvm"]`，`known_fail_msg` 钉住今天印的 `derived dx=[1, 1, 5, 5] dw=[1, 1, 4, 4]`（换一种错法就红，不许换个名字继续挂），`known_fail_ends_when = { prints = "derived dx=[1, 1, 6, 6] dw=[1, 1, 3, 3]" }`——修好那天这一行出现，标记自己变成红、逼人删标记。`known_fail_owner` 写的是「替换这套派生拼写的读值端口工程」，`known_fail_reason` 点名墙的两处文件#符号。
  - `conv2d_shaped.kernel_builds_what_it_derived`：**应当是红的缺陷**，同类，钉内核那一份实现（`value  dx=5x5 dw=4x4 numel=25,16`）。
- **没有**写成"错也可以"的断言：两条缺陷用例断言的都是**正确**的尺寸，只是今天过不去；`known_fail` 的 msg 固定今天的错读，`ends_when` 固定修好后的正确读，两个方向都钉住。
- `*_shaped` 一个都没删、没改；`src/stdlib/` 下没有语义改动（只有用例与计划两个新文件）。
- 冻结目录（`src/stdlib/autodiff/`、`src/stdlib/std/cxx/opt/`、C++ 优化 pass）未碰。

## 4. 跑读

命令（构建主机，日志留在树外 `D:\Temp\`）：

```
pwsh -NoProfile -File D:\Projects\Camel-wt\conv2d-shaped\internal\tools\camel-remote.ps1 -Path D:\Projects\Camel-wt\conv2d-shaped -Plan test/plans/feat/modules/conv2d_shaped_extent.plan.toml -Jobs 8 -NoGpuWatch
```

读数（提交之后的干净树上读的一次）：

```
[PASS] conv2d_shaped.plain_spellings_keep_their_operand_extents.nvm  138.81 ms
[PASS] conv2d_shaped.plain_spellings_keep_their_operand_extents.fvm  151.51 ms
[PASS] conv2d_shaped.plain_spellings_keep_their_operand_extents.jit  153.76 ms
[PASS] conv2d_shaped.plain_spellings_keep_their_operand_extents.prl  150.25 ms
[XFAIL] conv2d_shaped.derived_input_states_the_input_extents.nvm
[XFAIL] conv2d_shaped.kernel_builds_what_it_derived.nvm
[SUMMARY] total: 6, pass: 4, fail: 0, known failures: 2
[TREE] clean: the run left nothing under the checkout (git status --porcelain -u normal: 0 before, 0 after, 928 directories watched)
BUILD_EXIT=0
RUN_EXIT=0
camel-remote] checks: 1 run, 0 failed
camel-remote]   check-doc-links exit 0
```

两条 `[XFAIL]` 的正文（原文）：

```
[XFAIL] conv2d_shaped.derived_input_states_the_input_extents.nvm
    [INFO] known_fail_owner             the reading-port work that replaces the derived spellings
    [INFO] stdout does not contain 'derived dx=[1, 1, 6, 6] dw=[1, 1, 3, 3]'
[XFAIL] conv2d_shaped.kernel_builds_what_it_derived.nvm
    [INFO] known_fail_owner             the reading-port work that replaces the derived spellings
    [INFO] stdout does not contain 'value  dx=6x6 dw=3x3 numel=36,9'
```

`known_fail_owner` 只写工程/技术描述，不写车道名或角色名：计划字段是人读的散文，和注释一样要过过程痕迹检查（`scripts/check-process-traces.mjs`），而车道名属于过程痕迹。

跑这一轮之前有一轮（同一个计划、控制组还开着 `backends_agree = ["exit_code", "stdout_normalized"]`）在 fvm / prl 上报过 `differs from nvm: exit_code`，同一轮里还有两个 case 超时重跑、构建主机 1 分钟负载 66。把逐后端断言拆开（`backends_agree = []`，每个后端各自 `expect_exit = 0`）重跑，四个后端全绿、无超时——那次是过载主机上的抖动，不是后端差异，本页不作为读数。

## 5. 与 630 行红表逐名比

基准表：`docs/coordinator:internal/reports/2026-11-26-promotion-9-reds.tsv`（630 行，`plan_id<TAB>测试名`，量在 `e0ca7ca4d`）。

- `git show docs/coordinator:internal/reports/2026-11-26-promotion-9-reds.tsv` → 630 行；
- 在其中查 `conv2d` → **0 行**（表里一个 conv2d 相关名字都没有），所以我这两个名字都是新的；
- 本次运行的红：`fail = 0`；`known failures = 2`，就是上面那两条新用例。**新增真红 0**；新增 xfail 2，**作为这条缺陷的入表记录**（计划里带 owner / reason / msg / ends_when，见 §3）。
- 逐名比较的结论：`modules.conv2d_shaped_extent/*` 不在表里，也不是从表里某个名字变红来的；表里 630 个名字本次一个都没跑（本计划只跑自己），所以"比"这一步只做新增项判定。

## 6. 正解

1. **让规则拿到长度这条值**（推荐，也是读值端口那条路）：反向规则被回答的那个操作数，长度是正向图里就知道的事实，把它作为参数/读数传进规则，规则就不必从 `dy` 反推，`*_shaped` 这一族拼写存在的理由也就不存在了。
2. 你给的"不整除就拒绝"这一支，落到代码上有个前提问题：拼写里没有 `H`，而 `(H + 2p - K) % S` 里的 `H` 正是它要回答的东西——**它判断不了自己该不该拒绝**。所以拒绝只能退化成"一律拒绝"（等于删掉这个拼写），或者要求调用方同时给出 `H`（等于方案 1）。这一点值得记在案上：这条缺陷的可修版本只有一个。
3. 因此本页只钉缺陷、不改公式、不删 `*_shaped`；等读值端口落地，这两个 `known_fail` 会自己变红（`ends_when` 命中），删标记、删拼写一起做。

## 7. 复现

```
cd D:\Projects\Camel-wt\conv2d-shaped
pwsh -NoProfile -File internal\tools\camel-remote.ps1 -Path D:\Projects\Camel-wt\conv2d-shaped -Plan test/plans/feat/modules/conv2d_shaped_extent.plan.toml -Jobs 8 -NoGpuWatch
```

要看五行读数的原文：把计划里任意一条的 `expect_stdout_contains` 临时改成一个不可能出现的串跑一遍，失败块会把整段 `[stdout]` 打出来（本页第 1 节就是这么取的）；改完记得改回去。