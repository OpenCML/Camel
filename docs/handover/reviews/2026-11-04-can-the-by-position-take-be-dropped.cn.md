# 按位置取值删不掉：11 条命中里 10 条真需求、1 条空转，而空转那条也丢不得

## 问什么

作者 2026-11-03 14:55 要求删掉 `modules/transforms/transpose.cpp#buildTransposeMember` 里 `forward`
的 `closurePorts[at]`（`split` 与 `transpose` 之间的私下约定：闭包槽号 = 参数下标）。量下来的结论是：
**这句删不掉，因为"取不到的值"不能靠"少给一个值"来表达，只能靠拒绝。** 要作者裁的是：这条约定留着
并写清，还是换一条路（三条路各带代价，见 §默认）。

## 判据（可复核）

"向上追溯"追的是**余切的流动方向**：从被问的那个值出发，沿它的 normal inputs 往下走（一个值的余切
只会流到它由之算出的值上），看这条链最后停在哪些叶子上。

- **真需求**：这条需求服务的余切最后落在 `normPorts` 里**参与微分**的端口上 —— 那一格就是答案；
  值不给，这条余切就算不出来。
- **空转**：那条链只落在闭包成员、`with` 端口或常量上 —— 余切算出来也没人读，答案里没有它的位置。

## 一整份 `test/plans/feat/compile/transforms.plan.toml` 上的 11 条（逐条）

| # | 用例 | 被转置的函数 | 谁问的 | 追到谁 | 层 | 结论 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `grad_array_int_parameter_first` | `split_of_jvp_of_read_at_backward` | 调用点捕获（`transpose_of_index_into` 要 param0 的值） | `port0(p=1,part)` | 0 | 真 |
| 2 | `grad_array_no_int` | `split_of_jvp_of_both_backward` | 同上 | `port0(p=0,part)` | 0 | 真 |
| 3 | `grad_array_one_element` | `split_of_jvp_of_g_backward` | 同上 | `port0(p=0,part)` | 0 | 真 |
| 4 | `grad_array_product` | `split_of_jvp_of_g_backward`（5 个残差） | 同上 | `port0(p=0,part)` | 0 | 真 |
| 5 | `grad_array_sum` | `split_of_jvp_of_g_backward`（5 个残差） | 同上 | `port0(p=0,part)` | 0 | 真 |
| 6 | `grad_array_two_elements_first` | `split_of_jvp_of_g_backward`（2 个残差） | 同上 | `port0(p=0,part)` | 0 | 真 |
| 7 | `grad_array_with_int` | `split_of_jvp_of_mixed_backward` | 同上 | `port0(p=0,part)` | 0 | 真 |
| 8 | `grad_of_a_division` | `split_of_jvp_of___g_325___backward` | 调用点捕获（`transpose_of_div_tangent` 要 idx=3 的值） | `port0(p=0,part)` | 0 | 真 |
| 9 | `grad_of_an_array_parameter` | `split_of_jvp_of_g_backward` | 调用点捕获（`transpose_of_index_into`） | `port0(p=0,part)` | 0 | 真 |
| 10 | `relu_gradient_through_the_library` | `split_of_jvp_of_total_backward` | 规则 `sum_t`，node 9 position 0 | `oper:tensor:multiply` → `port0(p=0,part)` | 3 | 真 |
| 11 | `transpose_hand_closed_lambda` | `__g_317__`（手写闭包 `(dx) => 2.0 * x * dx`） | 规则 `mul_t`，node 4 position 0 | `port0(p=0,part)`（问的是 `dx` 的值） | 0 | **空转** |

**9 条真需求的形状**：律的右手边要那个操作数**自己的值**才建得出形状 —— `stdlib/rulesets/transpose.cml#idx_arr_t`
是 `scatter_into(zeros_like(xs), i, ct)`，`#div_t` 要另一个操作数，`#sum_t` 是 `spread_like(xs, ct)`。
`split` 的线性那一半把参数换成了切向量，切向量**没有值**，于是走法按位置取到**同号的残差**（前向的原值）。
类型检查能过是因为形状一致（`float[]` 对 `float[]`、`float` 对 `float`）；这正是"值是真的需要、给的却是代理"
的形状 —— 不是空转。

**第 11 条的原文读数**（`__g_317__`，`(dx: float) => float`，一个闭包成员）：
`POSPROBE-HIT ... ask=rule:mul_t|node:4|pos:0 target=port0(p=0,part) ...` 与成功收尾的
`POSPROBE-LAND fn=__g_317__ ports=[p0@0:part:ct] closure=[c0@2:ct] stray=2` —— 闭包成员 `x` 收到了一个
余切，之后没有任何人读它。所以它算的那份数是死的：**余切空转、值也取错了（取到 `x`，问的是 `dx`）**。

> **按语（这条缺陷的形态，值得进坑表）**：一个错误的值之所以没被任何用例抓住，是因为读它的那笔余切根本
> 没人要 —— **两处缺陷互相遮盖**。这类缺陷不会以红的形式出现，只会以"这段代码看起来多余"的形式出现，
> 所以删它之前的判据必须是**"有没有人读这笔余切"**，不是"它有没有接住参数"。今天按后者判就会判成空转
> 并删掉，然后死在签名上（见下一节）。

## 空转那条为什么也丢不得（实测，不是推断）

只丢这一条（`fn=__g_317__ node=4 pos=0`），其余一律不动，跑 `feat/compile/transforms`：

```
[FAIL] transpose_hand_closed_lambda.same_answer_as_the_cut  expected exit 0, got exit 1
[stdout] Runtime indirect call norm-arity mismatch: caller graph 'main' node 21 has 1 args,
         target graph 'transpose_of___g_317__' has 2 norm ports
```

**机制（这一段就是要给作者的那句话）**：一个成员需要的值如果取不到，它会被写成成员的**捕获端口、进签名**
（`transposeSignature` 从需要的值清单加端口）；而调用点仍按原签名传参 → 运行期 norm-arity 不匹配。
**所以"取不到"不能靠"少给一个值"来表达，只能靠拒绝。** 这就是 `transpose_loop_escape.two_values_of_two_kinds`
与 `.second_state_of_the_same_loop` 那两条长红的同一个机制（`Runtime indirect call norm-arity mismatch:
caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports`）——
"按位置取值删不掉"和"那两条长红"是**同一机制的两个症状**，不是两件事。

推论：**11/11 都不可按现状丢**。全部删掉得到的是那条已量过的 `84/2/1 → 74/12/1`，根因就是上面这句。

## 口径（引用时别跨口径比较）

同一个入口有两种口径的量法，**两个数字都对，但不是一回事**：

- **22 = 20 成功 + 2 取不到**：`CAMEL_POSPROBE=1`（env）版，把**探针草稿**（`TransposeState::inProbe` 那次
  试探性构建）里的重复也数进去；
- **11**：marker 版，只数**真实构建**（`probe=0`），另 2 条是探针草稿里的同一命中（`probe=1`）。

**不是同一批数**：marker 版这 11 条里 `from=call-capture` 占 **9 条**（`split` 的后向半在调用点捕获
`index_into`/`div_tangent` 成员要的值），与 env 版 22 条里那 20 条**不是同一批** —— 前者只数真实构建，
后者含草稿重复。只看"11 vs 22"会觉得差了一倍，差的是**数了哪些构建**，不是有没有漏掉命中。

跨口径比较数字，比不报数字更坏。

## 工具备注（下一个人会踩）

`camel-remote.ps1` 在主机上**生成**一份 shell 再跑计划，**工作机的环境变量到不了被测进程**。探针若按
`CAMEL_POSPROBE=1` 做，会量到**零命中**，然后被读成"这条路径没被走到"。这份读数的探针改成**marker 文件**
（`/tmp/camel-posprobe.on` 在则开，日志 `/tmp/camel-posprobe.log` 追加，一行一次 `fwrite`，故 `--jobs 4`
并发下不串行），补丁与日志在工作机 `D:\Temp\posprobe.patch`、`D:\Temp\posprobe-run3.log`，**不入 git**。

## 本树读数（树 + 时刻 + load，`-Tier fast -Jobs 2`）

树 `D:\Projects\Camel-wt\reach` @ `c69a110fa`（origin/develop），探针已回退、工作树干净；主机负载
18–22 / 32 核（属于"读数不与空载可比"的区间，已标注）。

| 目标 | [SUMMARY] |
| --- | --- |
| `test/plans/feat/compile/transforms.plan.toml` | total 91, pass 88, **fail 2**（`transpose_loop_escape.two_values_of_two_kinds`、`.second_state_of_the_same_loop`）, known failures 1 |
| `test/plans/feat/compile` | total 2222, pass 2217, **fail 2**（同上两条）, known failures 3 |
| `test/plans/feat/transforms/split.plan.toml` | total 19, pass 19, fail 0 |
| `test/plans/feat/showcase_s2.plan.toml` | total 7, pass 7, fail 0 |
| `test/plans/feat/compile/rulesets.plan.toml` | total 67, pass 67, fail 0 |

`transpose_hand_closed_lambda.same_answer_as_the_cut` 全程 **绿**（`cut ct1= (6.0)`、`cut ct3= (18.0)`、
`hand ct1= (6.0)`、`hand ct3= (18.0)` 四行齐）。

墙钟：`transforms` **5.0 s**（91 例）、`feat/compile` **103.7 s**（2222 例）、`split` 1.1 s、`showcase_s2` 0.5 s、
`rulesets` 4.0 s。**这两个大数也是满载下量的**（load 18–22 / 32 核），要拿它们当性能基线，必须同时注明
当时的负载；满载墙钟与空载墙钟不是同一个数，别把今晚的 103.7 s 当成 `feat/compile` 的基线引用。

## 默认（本轮按它开工，被推翻时只回退依赖它的那一层）

**不改代码。** 结论是"存在真需求"，判据第 3 条：分布写进本页，不删。

三条路，供作者选：

1. **留着并写清约定**（默认）：`split` 的残差槽号 = 参数下标这条约定写进
   `modules/transforms/split.cpp` 与 `transpose.cpp` 的注解，并把"取不到只能靠拒绝"写成成员签名的一条规则。
   代价 0，但那条私下约定仍然是私下的。
2. **按身份而不是按位置**：让 `split` 把"这个残差是哪个参数的值"记在图上（`34788b554` 曾这么做，
   手写闭包记录不到于是红了 —— 已回退）。代价：手写闭包没有这张表，要么给它一条通用写法，要么承认
   "手写闭包与切出来的那一半不同源"。
3. **让律不要值、只要形状**：把 `zeros_like(xs)` / `spread_like(xs, ·)` / `sum_to(·, operand)` 换成从
   余切/形状就能建的写法（`:tangent/zeros_arr` 已经在做这件事）。代价：动 `stdlib/rulesets/transpose.cml`，
   而 `sigmoid`/`tanh` 正在那条车道上 —— 本树禁改，跨车道协调才动。