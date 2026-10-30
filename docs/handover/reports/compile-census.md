# 编译普查：谁能编译、谁只是没人看

**一句话**：`perf` 不在 `-Tier fast` 里，所以 `test/cases/perf/transformer_step.cml` 坏在主干上很久没人知道；这条普查先回答「到底有多少个这样的」，再说它能不能当闸门。

**判据**：**fast 层之外的用例不是「慢」，是「没人看」。一个能编译但从不运行的用例，与不存在没有区别——而它比不存在的更糟，因为它让人以为覆盖了。**

## 读数一：裸编译一遍（3169 个文件）

对 `test/cases/` 下全部 `*.cml` 跑 `camel <file> std::gir`（编译后立刻返回空图，只计编译），12 路并行：

```
files=3169 ok=2102 fail=1067
```

- 其中 **203** 条按文件名就是「故意编译失败」的用例（`err_*` / `*refuse*`）；
- 其余 **864** 条里最大的一类不是缺陷，而是**我的跑法缺东西**：`SemanticDiag::ModuleNotFound` **193** 条（兄弟目录里的模块要 `--module-root`，只有计划文件知道），其后 `NoMatchingFunction` 136、`ArgumentsRejected` 44、`LiteralNotRepresentable` 36、`UnresolvedReference` 32……

## 读数四：runner mode（`--compile-only`）

`scripts/test.js --compile-only` 按**每个条目自己的 args / envs / 临时目录**跑一次编译：pass 词被替换成一个 `std::gir`，
条目不判的情况（`tool:`、`--timeit`、首个参数是 flag 的）标记为**不判**，因为拿编译器跑它们答的是另一个问题。

```
node scripts/test.js test/plans/feat --compile-only --jobs 8
[SUMMARY] total: 7972, pass: 7918, fail: 16, skip: 38
[INFO] wall: 178.3 s, jobs: 8, tier: fast
```

**178 秒看完 7972 个条目**，没有跑任何 perf 体。但**那 16 条红不能当缺陷报**：把 `feat/opt/edges` 单独再跑一次是
`14 / 14 全绿`——**这些红是并发 / 共享 scratch 的产物，不是程序的错**。所以接进 `-Tier fast` 之前还必须先解决
并行下的串扰（或按计划串行）；**今晚不接**。
**结论一：裸跑 `std::gir` 不能当闸门。** 它缺计划文件给的 `--module-root`、环境变量与参数，所以 1067 条里绝大多数是假阳性。要做成闸门，必须**按每个计划条目自己的 args 跑**，而不是按文件裸跑。

## 读数二：与计划期望交叉（1824 条「应当能过」）

把 21 个 `perf` 计划与全部计划一起解析，取「期望成功」的条目（无 `expect_exit = 1`、无 `error[` 期望）：**1824 条**。其中裸跑失败的 **124** 条，抽样全部是 `ModuleNotFound`（模块根由计划给），也就是说**这 124 条不是坏的，是我的跑法不完整**。

## 读数三：坏掉的那一个（真的）

```
[FAIL] training_steps.*                                          （4 / 6）
[INFO] expected exit 0, got 1
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: ...
   --> test/cases/perf/transformer_step.cml:90:28
```

`test/plans/perf/training_steps.plan.toml` **今天在主干上是红的**：6 条里 4 条失败，`transformer_step.cml:90` 的 `adam(...)` 找不到匹配。它没被 `-Tier fast` 覆盖，所以闸门一直是绿的。

**顺带一个读数限制**：这台 Windows 主机不报 CPU 时间，`perf` 层几乎每一条都因此失败（`no reading cpu of N s budget`），所以**perf 层的红在这台机器上不能当作「用例坏了」的证据**——但 `training_steps` 的红是编译错误，不受这条限制。

## 结论二：闸门该长什么样（未做，需裁定）

三条路，代价递增：

1. **只把「纯编译 + 纯运行」的计划（如 `training_steps`）挪进 fast 层**——最便宜，覆盖最小；
2. **写一个 fast 层用例：遍历所有计划条目，按条目自己的 args 只跑编译**——真覆盖，但要复用 harness 的参数/环境解析，约等于在 `scripts/test.js` 里加一个 mode；
3. **按计划运行 `-Tier slow` 的子集**——最接近真实，代价也最大。

**今晚只交读数，不接闸门**：读数二的 124 条说明裸跑这条路不可用，接上去会立刻一片假红。

## 读数五：串行重跑（判决：不是调度）

```
node scripts/test.js test/plans/feat --compile-only --jobs 1
[SUMMARY] total: 7972, pass: 7918, fail: 16, skip: 38
[INFO] wall: 682.5 s, jobs: 1, tier: fast
```

**同样是那 16 条，一条不多一条不少。** 所以它们不是并发串扰，我上一节那句「按 plan 不复现」是**错的**——我当时比的是
`feat/opt/edges`，而 `no_ctrl_edge_kind.gir` 在 `feat/linear/effects_model_edges.plan.toml` 里。**更正后的事实**：其中
`stdlib_registry` 三条**单独跑同样红，而且不加 `--compile-only` 也红**；直接跑那个工具得到的理由是：

```
stdlib-reference.mjs (--verify-ops): refusing to measure an install that is not this tree's build
  [camel-remote] -NoBuild refused: out/latest was built from different sources than this tree.
```

**这台临时工作树的 `out/latest` 是远端构建的，安装印记和本树源码对不上**——所以这 16 条里至少这一族是**机器/工作树条件**，
不是程序，也不是调度。其余各条尚未逐条归因，**不在这份页里宣称**。

**判据**：**一个检查若在自己的调度下会红，它就不是在检程序，是在检调度。这和「在坏程序上变绿」是同一种错。**

**代价**：8 路 **178.3 s**（约 22 ms 一条），串行 **682.5 s**；进 `-Tier fast` 意味着闸门**多约 3 分钟**（8 路）——是否每趟都跑，是裁定。

## 读数六：只扫 fast 层不跑的条目（裁定后的范围）

fast 层已经在跑的条目本身就编译过了，再编译一次是重复付费。所以 `--compile-only` 现在只选 `testTier(plan, test) !== 'fast'`：

```
node scripts/test.js test/plans --compile-only --jobs 8
[SUMMARY] total: 3670, pass: 3527, fail: 9, skip: 134
[INFO] wall: 483.7 s, jobs: 8, tier: fast

node scripts/test.js test/plans/perf --compile-only --jobs 8
[SUMMARY] total: 47, pass: 35, fail: 8, skip: 4
[INFO] wall: 304.7 s, jobs: 8, tier: fast
```

**范围小了，时间没小下来**：3670 条 483.7 s、perf 47 条 304.7 s——慢的不是编译，是 `perf` 里那几个重程序（`collfuzz.large` 单条 30.9 s）与它们自带的验证。**「十几秒」的期待没有实现。**

### 一处口径更正（读者必读）

`registry.signatures_match` **本来就在闸门里跑，一直是绿的**：它比对 550 条重载与声明、参数个数、纯度、以及「不带体」。
所以**不能**把它算作「没人守」。真正没人守的只有两件事：**签名文字是否逐字等于算子类型**，以及反方向的**「注册了但没登记」**。
本页前面把它当例子引用过，这里更正。
### 9 条红分三堆

1. **机器条件（`no reading cpu`）**：`progen_check`、`startup.cost`、`step.budget`、`reduce_layout.ops`、`collfuzz.large`——CPU 预算判定还有一处没被 compile-only 关掉。**不是程序。**
2. **拿不到参照的比较**：`monte_carlo_pi.speedup`、`transformer_step.profile`、`perf.instruction_counts`、`perf.instruction_counts_load`、`gc_tools.gcbench_workloads`——它们的判据是与另一次运行的比值，**这个模式下根本没在判编译**。
3. **安装印记类**：`registry.*` 三条（以及 `edges.no_ctrl_edge_kind.gir`）**都是 fast 层条目**，新范围把它们自然排除——这一堆在新范围下不复存在。

**结论：这个模式现在还不能进闸门**（>1 分钟，且有 2 类非编译判据），按裁定的「单独一条 plan、每趟一次」也还要先把第 1、2 类处理干净。

## 读数七：两栏（编译失败 / 判据不适用），以及 `perf/training_steps` 的复核

**工具的用途只有一句**：**回答「这一片里有哪些程序编不过」，不回答「这些测试是否通过」；不进闸门，不进 fast 层。**

**两栏**：一条「只回答一个问题的工具，输出里只该有两栏：被问的那件事，和『不是这件事』。第三栏（『判据不适用』）必须显式写出，否则它会被读成失败。**

`perf` 两栏读数：

```
[SUMMARY] total: 47, pass: 42, fail: 1, skip: 4
[NOT-JUDGED] … 40 条（--timeit / 工具 / 比较判据）-- not a compilation question
[INFO] does not compile (../../cases/perf/transformer_step.cml): exit 1
```

**「编译失败」这一栏今天不是 0，而是 1——而且正是 `transformer_step.cml`**，就是那条躺在 `perf` 里没人看的坏程序。**这不是工具的瑕疵，是它的第一个真实产出。**

### `perf/training_steps` 复核（在 `lane-gpu` 上，含 `9e01683d6`）

```
[PASS] dependency_chain.nvm / .fvm
[PASS] transformer_step.nvm
[PASS] transformer_step.profile
[FAIL] mlp_step.nvm / mlp_step.fvm   expected exit 0, got 1
[SUMMARY] total: 6, pass: 4, fail: 2
```

**`gpu` 的修复有效：`transformer_step` 两条从「编译不过」变成 PASS。** 但这条计划**不是 6/6/0**：`mlp_step` 的两条仍红（`expected exit 0, got 1`），**诊断原文未取到**，这里不猜原因。**「那个坏程序修好了」有读数；「这条计划绿了」没有。**

### 全量两栏（fast 层外）

```
node scripts/test.js test/plans --compile-only --jobs 8
[SUMMARY] total: 3670, pass: 3535, fail: 1, skip: 134
[INFO] wall: 491.9 s, jobs: 8
[NOT-JUDGED] 295 条
[FAIL] transformer_step.profile -- does not compile (transformer_step.cml): exit 1
```

**fast 层外的 3670 个条目里，只有一个程序编不过——就是 `transformer_step.cml`，那个从没被跑过的。** 其余红全部消失：
它们此前要么是「判据不适用」（现在显式标为 NOT-JUDGED，295 条），要么是我漏关的 CPU 判据。

### 两条诊断原文（照抄，不解释）

`transformer_step.cml`（快层外那条唯一的编译失败，`58dcb949c` 上）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ({ g1: f32[8], g2: f32[8], wq: f32[8, 8], wk: f32[8, 8], wv: f32[8, 8], wo: f32[8, 8], w1: f32[8, 16], b1: f32[16], w2: f32[16, 8], b2: f32[8] }, { g1: f32[..], g2: f32[..], wq: f32[..], wk: f32[..], wv: f32[..], wo: f32[..], w1: f32[..], b1: f32[..], w2: f32[..], b2: f32[..] }, OptimizerState, float).
   --> test/cases/perf/transformer_step.cml:90:28
 90 |             let next = adam(b, gradient, opt, lr)
help: Available overloads: <nn:adam>: (params: P, grads: grad of P, state: OptimizerState, lr: float, beta1?, beta2?, eps?) => (P, OptimizerState)
```

`mlp_step.cml`（`lane-gpu`/`d7b3ed065` 上仍红的那两条）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ({ w1: f32[784, 128], b1: f32[128], w2: f32[128, 10], b2: f32[10] }, { w1: f32[..], b1: f32[..], w2: f32[..], b2: f32[..] }, OptimizerState, float).
   --> test/cases/perf/mlp_step.cml:55:28
 55 |             let next = adam(m, gradient, opt, lr)
help: Available overloads: <nn:adam>: (params: P, grads: grad of P, state: OptimizerState, lr: float, beta1?, beta2?, eps?) => (P, OptimizerState)
```

**判断：程序没写错，是编译器（类型）这一侧的缺口。** 判据两条：一是 `gpu` 的 `9e01683d6` 只改了 `type Block` 的字段形状，`transformer_step` 就从「编不过」变成 PASS——**同一个文件、同一行 `adam(...)`，改的是类型声明，不是程序**；二是 `mlp_step` 是**逐字同构**的 `grad of P` 失配（参数是静态形状的 struct，梯度是无界 `f32[..]`），说明缺口没被那笔修完，而不是两个程序各自写坏。**不修，只记读数**；修要单独立一笔。

**`--compile-only` 第一次跑出 1 条时，那个 1 不是噪声，是这条工具存在的理由。**

## 一个关于「报数」的例子（我自己的账）

今晚我两次报告账本读数都写成 **62 / 491 / 3 / 4**——那是 `gs2b` 三个族合并**之前**的数。同一份账本在**两小时内**从
`62 / 491` 一路降到 **`47 / 395`**（`b5d86e610` 上实测：`files 47 <= 47 references 395 <= 395 constructions 3 <= 3 decode 4 <= 4`）。

**判据：同一份账本在两小时内从 62/491 降到 47/395，任何引用它的页都必须带取数的 sha，否则读者无法判断自己读到的是哪一刻。**

## `perf` 那 47 条里 40 条「判据不适用」这个比例本身是个读数

**`perf` 层几乎不判编译，它判的是比值与预算；所以它本来就不该被这个模式覆盖。** 这也是 `--compile-only` 进不了闸门的
第二个理由（第一个是 8 分钟）：**在这片地方，编译只是它们故事的开头。**

## `mlp_step`：程序把声明写松了，不是编译器把类型抹平了

```
“`grad of P` 已经保形（`derivative.cpp:213` 那条 'a numeric array's tangent is a tensor of the same
shape'）；`f32[..]` 与静态形状的不统一，是程序把声明写松了，不是编译器把类型抹平了。
`transformer_step` 的修复方向是把声明改成与实际值相同的静态形状，与‘接受抹平’正好相反。”
```

**判据**：**当一个「缺口的修法」有两种方向（放宽类型系统 / 收紧程序声明），先问哪一种让系统多一个能检查的东西——今晚的答案是后者。**

按这个方向，`test/cases/perf/mlp_step.cml` 的 `type Mlp` 写成了它实际构建的形状（`w1: f32[784, 128]`、`b1: f32[128]`、`w2: f32[128, 10]`、`b2: f32[10]`），理由是梯度的形状与参数一致，声明写松就会与自己的值不统一。

**读数**：`perf/training_steps` **6 / 6 / 0**（此前 4/6），`mlp_step.nvm` 与 `mlp_step.fvm` 都 PASS，`transformer_step` 两条仍是 PASS。

**顺手一查**：`git grep -n 'f32\[\.\.\]' -- test/cases/perf` 共 **42 处、8 个文件**（`gemm_threads_curve.cml` 17、`transformer_step.cml` 6、`fusion_step.cml` 5、`dependency_chain.cml` 4、`fusion_chain.cml` 3、`gemm_canonical_shapes.cml` 3、`mlp_step.cml` 3、`step_budget.py` 1）。**其中并非都是参数树**（`mlp_step` 剩下的 3 处是 `logits`/`loss` 的形参与返回类型），是否还有同形状的第二处，未查、未改。

## `startup.cost`：单跑 16.22 s，是真的变慢，不是争抢

**判据**：**一条预算类断言在 8 路并行下红了，先问「单跑是多少」；不问就放宽，等于把机器争抢记成程序变慢。**

读数（`lane-gpu-check`，含 `6c8eaa0f1` 与本页那笔 `mlp_step` 修复）：

```
-jobs 8（整层）：[FAIL] startup.cost  16.23 s cpu of 13.00 s budget, 20.17 s wall
-jobs 1（只这条）：[FAIL] startup.cost  16.22 s cpu of 13.00 s budget, 19.17 s wall
[INFO] used 16.22 s of CPU against a cpu_budget_ms of 13.00 s: the test's own work grew
```

**两个数几乎相同，而且超的是 CPU 不是 wall——CPU 是这个指标里不受负载影响的那一半。所以判断是「真的变慢」，不是 8 路争抢。**
**超了多少**：`16.22 - 13.00 = 3.22 s`，**+24.8%**。
**从什么时候开始超**：**未定位**。预算由 `04c451728`「bound the CPU-heavy slow entries by their own CPU time」写下，其依据是 2026-10-23 在 load 106–130 上的读数；**要答「哪一笔开始超」得二分，我没有做**。**未放宽**（单跑即超，没有放宽的依据）。

## `f32[..]` 的 N/M 分类（`test/cases/perf`）

按「是否出现在 `type X { … }` 的字段里」分（脚本按花括号深度判定，`D:\Temp\classify2.mjs`）：

**字段里 19 处、其余 45 处，共 64 处**（`git grep` 的 42 是**行数**，一处一行不等于一处一字面量）。字段里最多的是 `gemm_threads_curve.cml`（13 处，`type Shapes` 的字段）、`fusion_chain.cml`（3）、`fusion_step.cml`（3）。**未改。**

**报计数时数字面量个数，不是行数。** `git grep` 数的是命中行：`f32[..]` 那次 42 是行数，字面量是 64——一行里可以有两处，两处就是两个可以各自成为 bug 的地方。

### 二分表（每题只跑这一条，`--jobs 1`，均为当前主机）

| sha | 日期 | 读数 |
| --- | --- | --- |
| `04c451728`（写下预算那一笔） | — | `15.80 s cpu of 13.00 s budget` |
| `368ae4f27` | 2026-10-22 | `15.59 s cpu` |
| `f3922c5c7` | 2026-10-25 | `15.55 s cpu` |
| `d7e579446` | 2026-10-28 | `15.71 s cpu` |
| `c85878ac9`（`origin/develop` 尖端） | 2026-10-31 | `16.18 s cpu` |

**这五天里没有台阶：全部在 15.5–16.2 s 之间，波动 4%。而写下 13 s 预算的那一笔自己就读 15.80 s。**

**判定**：**不是这八天里变慢的，是那条预算与这台主机的读数从一开始就不符**——按裁定的分支，**这属于「预算放错对象」，不是「程序变慢」**。**没有台阶**这一点也说明不是某笔代码的锅。
**不能由这张表判定的是**：更早（`04c451728` 之前）的读数，以及这台主机是不是换了机器/降了频——**CPU 秒数不受负载影响，但受硬件与频率影响**。要把它做成一条可信的预算，需要在一台固定主机上按单跑 CPU 重写，并在页里记下主机与负载。
