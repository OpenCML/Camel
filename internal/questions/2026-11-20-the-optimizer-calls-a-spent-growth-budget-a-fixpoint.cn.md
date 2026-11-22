# The optimizer calls a spent growth budget a fixpoint, and the budget is reset on every call

日期：2026-11-20 ｜ 来自 `lane-optperf`（工作树 `optperf`，读数在 `58ea47acc`）｜ 对象：`std::opt` 的终止条件
（`src/passes/opt/generic/generic.cpp:2076`、`src/passes/opt/inline/engine.cpp:1104`）与增长预算
（`include/camel/passes/opt/config.h:53`）｜ 状态：**默认按（b）开工，待作者确认**

## ① 问的这一句

**`std::opt` 把「增长预算用尽」报成 fixpoint 并且每次调用重置预算——应该改预算的作用域（a），
还是只改这件事的报法（b）？**

## ② 让它是问句的读数：一条命令就定性

同一张图，一次调用把 `growth_budget` 给大，与两次调用各用默认值：

```
perf_fib   1p=786    1p@8192=1110   1p@16384=2285   2p=2392
fib_new    1p=604    1p@8192= 918   1p@16384=2959   2p=4377
```

⇒ **一次调用把预算给到 16384，跑出的图 ≈ 两次默认调用**（`perf_fib` 2285 对 2392，差 4.5%；
`fib_new` 2959 对 4377，两次更远但同一方向）。预算 `kDefaultGrowthBudgetNodes = 4096`
（`include/camel/passes/opt/config.h:53`），而计数器 `growthSpent` 建在 `simplifyGraph` 的栈上
（`src/passes/opt/generic/generic.cpp:1970`）——**每次调用一个**。

⇒ 所以 `perf_fib_smoke.opt_twice`（`test/plans/feat/opt/splice_place.plan.toml`，两次 `std::opt`）不是
「对已优化的程序再看一遍」，是**「把增长预算给两倍」**。

## ③ 它为什么是静默的

终止判据是 `!optChanged && folded == 0 && merged == 0 && simplified == 0 && removed == 0`
（`src/passes/opt/generic/generic.cpp:2076`）。这一行**看不见「这次 inline 因为预算被拒」**——那条拒
在 `src/passes/opt/inline/engine.cpp:1104`（`InlineDecline::GrowthBudget`），既不进上面五个计数，也不
产生任何诊断。于是预算用尽与真正的不动点打的是同一句：

```
<Opt> std::opt: fixpoint after 7 round(s)
```

这一点必须写死：**只要这句话还能在预算用尽时打出来，「它到 fixpoint 了」就不是一个读数。**

## ④ 两条路，代价写死

- **（a）预算改成跨调用的运行状态**：第二次调用继承已花掉的额度。**省时间**（第二遍几乎不做事），
  但它**同时改变所有 `std::opt` 调用的产出图**——凡是有两次以上 `std::opt` 的计划，图都会变，而
  **今天没有一次全量对照**能说清那些计划的输出会不会跟着动。
- **（b）不动预算，只改「因预算被拒」的报法**：改成显式警告 + 停止，不许报 fixpoint。**只改诊断**，
  产出图一个字节不变；代价是**成本不降**——第二遍仍会膨胀，只是这件事从此可数。

**⇒ 默认建议（写死）：先做（b）。** 理由不是它省事，是它**先让缺陷可观测**：把「预算被拒」变成一个能数的
事件，跑一次六计划看有多少条计划在打这个警告；**有了那个数，（a）才有依据**。反过来先做（a）＝在不知道
波及面的情况下改所有 `std::opt` 的产出图。

**⇒ （b）要付的代价（写死）**：它**不修成本**。做完（b）`opt_twice` 仍然慢，只是它慢这件事会有一句自己的
话，而不是被念成「fixpoint」。页里不把（b）说成「修好了」。

## ⑤ 「把 `opt_twice` 压回 5000 ms」不是良定目标

两个几乎同构的程序差 5.4 倍（下面第 ⑥ 段），所以**能判的是幂等性**——同一张图过第二遍不再放大——**不是
某条毫秒线**。这一条直接管着 `perf_fib_smoke.opt_twice` 的 `cpu_budget_ms`：它的数要等幂等性修好之后
才能重定，现在重定只是把红线挪到今天的病态值上。

## ⑥ 读数坐标（谁都能重跑）

基线 `52fe1b6a2` 的构建（主机，`1m load` 14–17，故只读同一 run 内的比值）：

```
真用例  perf_fib 143→786→2392 节点   第二遍/第一遍 = 20.04×
无 fib  老 timeit 形状 64→127→127  0.04×     新形状 60→1495→2435  1.14×
带 fib  老 timeit 形状 77→464→1276 4.14×     新形状 73→604→4377  108.83×
```

⇒ **「第二遍放大」= 调用图 × 列表构造形状，两者相乘**：去掉 fib，老形状**完全幂等**、新形状只长 63%；
加上 fib，老形状也长（4.14×），新形状变成 108.83×。**最小标本比真用例贵 5.4 倍**——量级对程序形状是
混沌的，这正是 ⑤ 段那句话的来源。

探针与四个 `.cml` 标本在树外（`D:\Temp\optperf\`），不随提交。

## ⑦ 两条不要混进来的读数

- **那张成本表不能用**：`internal/reports/2026-11-20-where-the-opt-twice-cpu-goes.cn.md` 的
  `drafts=944 / rewrites=5254` 取自 11-11 构建（旧 `timeit` 形状）。`96d8c9ea3`（11-15 15:36）改写
  `stdlib/perf.cml` 的 `timeit` 之后，那个程序**已不再被编译**；基线同一读数是 `1082 / 10857`。
- **每次 rewrite 的代价（0.063 → 0.44 ms）没有复现**：在同一规模图上第二遍只贵 1.14×。它要么是另一个
  原因，要么是图更大时的表现，**本条不拿它当结论**。

## 默认建议（写死）

**默认建议**：按 **（b）** 开工 —— 不动预算作用域，只改这件事的报法：`InlineDecline::GrowthBudget`
（`src/passes/opt/inline/engine.cpp`）改成**显式警告 + 停止**，`src/passes/opt/generic/generic.cpp` 那行终止判据
在预算用尽时**不许再打 `std::opt: fixpoint after N round(s)`**。
**判据**：① `-Plan feat/opt,...` 六计划 `-Jobs 8` 的 `total/pass/fail` 与改前**逐字相同**（（b）不动产出图，
一个字节都不该变）；② 同一轮数出「这个警告出现在多少条计划、各多少次」，把那个数列出来 ——
**有了那个数，（a）才有依据**。反过来说死：（b）**不修成本**，`opt_twice` 仍然慢，它慢这件事只是从此有一句
自己的话；`perf_fib_smoke.opt_twice` 的 `cpu_budget_ms` 在幂等性修好之前**不许重定**（§⑤：那只是把红线
挪到今天的病态值上）。
