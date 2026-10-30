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
