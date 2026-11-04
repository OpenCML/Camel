# 形状要当操作数交下来：`mul_stated` 与 `matmul_stated`

**状态**：待裁，**但第 4 节的默认已被 01:2x 的实测推翻一半**：七条红逐条打印后，两条 matmul 是**甲**（类型陈述了，律把 `a` 当值交给 `sum_to` 了），修法在律里；五条 `mul_tensor_t` 是**甲（值）+ 乙（形状）**两半，孪生算子只对乙那一半有理由，且单独关不掉任何一条。证据在 `docs/handover/reports/2026-11-04-the-seven-reds-classified-by-what-the-port-states.cn.md`，下一步见该页第 5 节。

写这页的直接原因：作者 2026-11-04 04:20 裁"**现在就删掉 `shapeFromTypeOf` 里的零载体回落**；写死形状的类型用 `extent_of`；开秩的裸 `transpose` 按语言规则拒绝"，同一份裁定里"`exp`/`erf`/`gelu` 的 `jvp` 规则把形状作为已知实参交给乘法（与 `sum_stated` 同一个做法）"。前半句本轮落了（零载体已删，见 `docs/handover/reports/2026-11-04-extent-of-reads-the-type-and-hands-it-over.cn.md`），后半句要一个新算子，本轮没落——而**删掉零载体之后，正是这七条从绿转红**，所以这页不是可选工作，是那七条的唯一出路。

## 1. 读数（实测，含树、时间、负载）

树 `D:\Projects\Camel-wt\zerocarrier` → 宿主 `88zjwei`（32 核），分支 `lane-zerocarrier`（基线 `d7f15f2b3`），宿主时钟 2026-11-03 23:2x–23:4x（`-05:00`），本机 2026-11-04 12:2x–12:4x，1 m load 19.12 / 24.29 / 23.63 / 24.33——**强负载，读数只做同机同时段比较**。命令一律 `camel-remote.ps1 -Path D:\Projects\Camel-wt\zerocarrier -Plan test/plans/feat/compile/transforms.plan.toml -Tier fast -Jobs 4`。

1. **零载体删掉后**：`[SUMMARY] total: 170, pass: 151, fail: 5, known failures: 14`。五条（全量 `feat` 量到同一个病还有两条，见本节第 5 条）：

   | 用例 | 拒绝（原话） |
   | --- | --- |
   | `erf_gradient_both_legs` | `skip=cotangent-needs-a-value uri=tensor:multiply position=0` |
   | `erfc_gradient_both_legs` | 同上 |
   | `transpose_gradient_both_legs` | 同上 |
   | `mean_axis1_keepdims_gradient_reads_the_row_length` | 同上 |
   | `linear_gradient_both_legs` | `skip=cotangent-needs-a-value uri=tensor:matmul position=0` |

   这五条都在 `skip=` 这一步停：**律的右式要读一个调用方没有的值**。删掉零载体之前它们是绿的，靠的正是那个零（`sum_to` 只读零数组的形状，数字对、机构不对——作者 11-03 17:35 那句话说的就是这里）。

5. **全量 `feat` 量到同一个病还有两条**（`2026-11-04 00:10` 宿主时钟，`total: 8762, pass: 8710, fail: 11, skip: 11, known failures: 30`）：

   | 用例（都在 `feat.showcase_s2`） | 拒绝（原话） |
   | --- | --- |
   | `first_step.tensor_vjp_answers_the_step` | `skip=cotangent-needs-a-value uri=tensor:multiply position=0` |
   | `matmul_t.law_read_through_vjp` | `skip=cotangent-needs-a-value uri=tensor:matmul position=0` |

   所以这次删除在整棵树上的账是 **7 条**（其中 5 条在 `feat/compile/transforms`），另外全量还有一条 `docs.links.questions_index`（本轮新加的两页问题页没进 `questions/README.md` 的待裁表，已用 `node scripts/check-doc-links.mjs --questions-index --write` 补上，那一页的表从 50 页变 52 页）。

2. **缺的不是值，是"被回答那一位自己的形状"**。`stdlib/rulesets/transpose.cml#mul_tensor_t` 的答案写的是 `sum_to_stated(ct * w, shape(x))`：`ct * w` 要的是另一个操作数（前向值或常量，**闭包里都有**），`shape(x)` 要的是**被回答那一位**（`transpose(dx)`、`dx` 这类由切线派生的值）的形状，而它的类型是 `float[...]`，陈述不出 extents。`matmul_t` 的 `ct_of_a(ct, a, b) = sum_to(matmul(ct, transpose(b)), a)` 同理：要的是 `a` 的形状（`sum_to` 的第二个操作数按文档只被读形状）。

3. **把参数的值塞进闭包不解决这一族**（本轮实测两次，第一次是全参数、第二次只收"类型不陈述 extents"的参数，两次都读完即撤）：`residuals` 从 `0` 涨到 `1`、从 `2` 涨到 `4`、从 `3` 涨到 `4`，七条**一条都没翻**。原因是缺的那一位是**切线**（`dx`），不是原值；而收切线的值有实测的旧账：`modules/transforms/split.cpp#residualsOf` 里那条注记——`g(x) = f(x) * x` 曾经答成"对参数恒定的函数"、36.0 不变、`grad(g)` 静悄悄答 0.0（2026-11-04 实测）。所以切线不进闭包，这一族的形状只能当操作数交下来。

   第二次实测还量到一件**属于零余切那一页**的事：那次只收"类型不陈述 extents"的参数，`jvp_of_const_open` 的 `residuals` 从 `0` 涨到 `1`（值确实进了闭包），可 `transpose.cpp#buildTransposeMember` 把 `forwardOf[ports[port]]` 改成 `forward(ports[port])` 之后仍然拿不到端口，读数 `skip=parameter-shape port=1 at=0 no-cotangent-zero`，`vjp_const_function_answers_zero` 的开放那一支由 `([])` 变成拒绝。也就是说作者 04:20 那句"原值是残差，在闭包里"在 `vjp` 这条路上**今天不成立**：值进了闭包，成员却没有一个端口读它。这一半的落点不是孪生算子，是 `split`/`transpose` 之间的残差—端口对应，要自己一轮。

4. **另一页第 44 行需要更正**：`2026-11-04-who-states-the-length-when-the-shape-is-open.cn.md` 说"乙管不了 `mul_tensor`：位 0 的答案要的是另一个操作数的**值**，不是它的长度"。本轮量到，在这七条里另一个操作数的值**是有的**（`e = exp((x*x)*-1.0)` 是前向值，`W` 是常量，`b` 是前向参数），缺的是 `shape(被回答的那一位)`。所以带长度的孪生算子**正好管这一族**，那页的结论对那六条 `known_fail`（位 1 要读切线的值）依然成立，对这一族不成立。

## 2. 设计（甲）

两个孪生算子，照 `sum_stated` / `take_stated` / `mean_axis_stated` 的样子落在 `modules/tensor/ops/**`（声明 `stdlib/tensor.cml`，签名表与文档按仓库脚本重生成）：

```
mul_stated::<T>(x: T[...], w: T[...], ax: int[], aw: int[]): T[...]     where is_float::<T>()
matmul_stated::<T>(a: T[...], b: T[...], sa: int[], sb: int[]): T[...]  where is_float::<T>()
```

- 两个算子的**值**与 `multiply` / `matmul` 逐位相同（内核直接转调今天的那两个），多的四个（两个）`int[]` 只说明"这两个操作数的形状是这些"，运行时期不看它们就能算——它们存在只为让反向律读得到。
- 正向律写：`jvp_mul_tensor(x, w) => (dx, dw) => mul_stated(dx, w, shape(dx), shape(w)) + mul_stated(x, dw, shape(x), shape(dw))`。**前半程里 `dx`、`dw`、`x`、`w` 都是值**，`shape(...)` 在那里算得出来——这正是 `jvp_sum` 今天写 `sum_stated(dxs, shape(xs))` 的做法。
- 反向律写：`mul_stated_t(x, w, ax, aw) => (ct) => (sum_to_stated(ct * w, ax), sum_to_stated(ct * x, aw))`；`matmul_stated_t` 同理把 `sum_to(..., a)` 换成 `sum_to_stated(..., sa)`。两半都只读 `ct`、另一个操作数的**值**和 `at`，不读被回答那一位的任何东西——这是"形参不读值"的规则本身。
- `jvp_linear` 改写成 `matmul_stated(dx, w, shape(dx), shape(w)) + matmul_stated(x, dw, shape(x), shape(dw))`。

为什么是两对而不是一对：广播乘积的两个操作数形状可以不同（`[2,2,3] * [3]`），一个 `at` 说不清；而 `sum_to` 要把每一位的余切收回到**它自己**的形状，所以每一侧各要一份。这也是 `sum_stated` 一个 `at` 够用（一个操作数）而这里要两个的原因。

## 3. 凭什么相信它能关上那七条

- `erf`：线性半是 `(dx * e) * k`，`e = exp((x*x)*-1.0)` 是前向值。位置 0 的答案 `sum_to_stated(ct * e, at_dx)` 只读 `e`（有）与 `at_dx`（新操作数，`shape(dx)` 在前半程算好）。`gelu` 今天已经绿，说明这个跨阴谋的形状是同一件事。
- `transpose`：`transpose(dx) * W`，`W` 是常量；位置 0 要 `shape(transpose(dx))`——`shape(dx)` 在前半程算得出来，而 `transpose` 只换轴，`shape(transpose(dx))` 也能在前半程写成 `shape(transpose(dx))`（值有，算子有）。
- `mean_axis1`：同 `erf`，缺的是那一侧的形状。
- `linear`：`matmul_stated` 把 `ct_of_a` 的 `sum_to(..., a)` 换成 `sum_to_stated(..., sa)`，`sa = shape(dx)` 在前半程算好。

## 4. 我方默认

**甲，先 `mul_stated`（一条命令能关四条：`erf`、`erfc`、`transpose`、`mean_axis1`），再 `matmul_stated`（关 `linear`）。两条都在 `modules/tensor/ops/**`，与 `sum_stated` 同一处、同一形状。**

本轮没做，理由只有一条：这一轮从"删零载体"走到"给形状找一条新通路"，两条孪生算子要动 `modules/tensor` 的算子面（算子定义、内核、注册表、`stdlib/tensor.cml`、签名表、文档、注册表用例），加上 `jvp`/`transpose` 两侧的律，是一次完整的算子落地，本轮的时间预算不够把它做**对**（尤其是广播那一支的数字：`abs`/`maximum`/`divide` 那六条 `known_fail` 里，位 1 要读切线的值，孪生算子管不了它们，别在落地时把它们一起"修"成错数）。

因此本轮的取舍是：**删零载体（作者已裁"现在就删"）与它暴露出的七条红一起交给作者**，而不是把零载体留下换七条绿——留下零载体等于把"静悄悄答错数"的机构留在树上，那正是这轮要拆的东西。

若作者裁定"先别删，等孪生算子落地再删"，则这轮的两处改动（`modules/transforms/transpose.cpp#shapeFromTypeOf` 的删除、`#buildTransposeMember` 的 `forward` 分支）回退即可，`docs/handover/**` 的说明与两份问题页不依赖它们。

**零余切那一半（第 1 节第 3 条末尾）本轮试过并已回退**：`split.cpp#residualsOf` 收开秩参数、`buildTransposeMember` 的零余切那一段改成 `forward(ports[port])` 并去掉 `zeroOfType` 回落，实测 `vjp_const_function_answers_zero` 的开放那一支由 `([])` 变成拒绝（`skip=parameter-shape`），所以两处都撤回原状——**留着只会把一条已钉住的读数换成一个拒绝，而拒绝的理由（没有端口读残差）还没修**。那一半的入口在 `2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal.cn.md` 已经写定的"原值是残差、在闭包里"，缺的是 `split` 交给成员的闭合端口与参数之间的对应；请与孪生算子分开裁。