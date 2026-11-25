# 三条库转置律为什么没有一条能走到：`softmax_t`、`layer_norm_t`、`conv2d_t`

- 状态：**结论页（车道 `transpose-carry` 的停手报）**。不是缺一条律，也不是本车道能改的东西：
  每条都停在「律要读的那个值不在转置成员的签名里」，其中两条还挡在前面一层。
- 树：`D:\Projects\Camel-wt\transpose-carry`，分支 `lane/transpose-carry`，tip `c99d7e74f`
  （= `git fetch` 后的 `origin/integration`）。宿主 `88zjwei`，二进制构建时刻
  `2026-11-25T04:08:25-05:00`，`tier fast`。读这份读数时宿主 1 分钟负载 17–29（**时间不可比，只读名字与数**）。
- 本页只加这一行文件，`src/**`、`test/**`、`scripts/**` 一行未动。探针不入 git（同
  `internal/reviews/2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md` 的做法），
  源码抄在第 5 节。

## 1. 三条读数（逐字，`--trace transforms`）

三份探针各一个程序，跑 `vjp` 的库腿（`stdlib/vjp.cml`），`transforms` 追踪打开：

```text
softmax
TRANSPOSE function=rulesets.jvp::softmax_jvp<float> skip=parameter-shape uri=tensor:multiply node=9 position=1 param=1 param-type=float[...]
TRANSPOSE function=split_of_jvp_of_softmax_total_backward skip=call-no-transpose node=19 callee=rulesets.jvp::softmax_jvp<float>
    → (vjp) out/latest/stdlib/vjp.cml:134:13 [Error]: transforms: transpose has no transpose for this function:
      this call's callee has no transpose of its own, so the call cannot be tu…

layer_norm
TRANSPOSE function=rulesets.jvp::layer_norm_tangent<float> skip=parameter-shape uri=tensor:add node=8 position=0 param=3 param-type=float[...]
TRANSPOSE function=split_of_jvp_of_layer_norm_total_backward skip=call-no-transpose node=19 callee=rulesets.jvp::layer_norm_tangent<float>

conv2d（对 `x` 求导）
TRANSPOSE function=split_of_jvp_of_conv_total_backward skip=law-reads-a-value uri=nn:conv2d position=0
  operand-types=f32[1, 2, 4, 4],f32[...],f32[...] reason=the rule's right side reads a value the caller does not carry

conv2d（对 `w` 求导，同一堵墙的另一半）
TRANSPOSE function=split_of_jvp_of_conv_total_backward skip=law-reads-a-value uri=nn:conv2d position=1
  operand-types=f32[1, 2, 4, 4],f32[3, 2, 2, 2],f32[...] reason=the rule's right side reads a value the caller does not carry
```

五份探针（两条 `conv2d` 各一格、`softmax`、`layer_norm`、`max_pool2d`）的 `[SUMMARY]`：
`total: 5, pass: 0, fail: 5`。**五格全红，没有一格用来说明"路是通的"** —— 阳性对照在树里、
不在探针里，见第 2 节末。

**`softmax_t`、`layer_norm_t` 根本没有被问到。** 走不到它们的是转置的第一步：`split(jvp(f))` 的
**线性半**里放的是**调用**（`rulesets.jvp::softmax_jvp`、`rulesets.jvp::layer_norm_tangent`），
而 `call-no-transpose`（`TransposeRefusalCause::CallNoTranspose`）在律被查表之前就把这一步停了。
`softmax_t`/`layer_norm_t` 是"被档、没人走"，不是"被走、答错"。

**`conv2d_t` 被真正问到了**（`uri=nn:conv2d`，两个位置各一次），停在它自己那一位置的值上：第 2 节。

### 1.1 基线（按名，不按数）

- `node scripts/vjp-parity.mjs`（exit 0）：`178 law(s) filed under 74 key(s)`，`122 engine registration(s)
  under 104 key(s)`，`55 key(s) both answer (each half)`，`8 forward-only`，`0 reverse-only`，
  `5 with a law that is neither half`，`6 library-only and 36 engine-only exempt`，`0 new`，`stale=0`。
- `node scripts/vjp-parity.mjs --closure`：`transpose.cml files 61 law(s) over 61 key(s); jvp.cml files
  85 law(s) over 69 key(s); 0 missing`。
- **`filed` 有这一列，`walked`/`not-walked` 没有**。`vjp-parity` 数的是键；"这条律有没有被程序走到"今天
  唯一的机器可读记录是六处手写的 `library=filed-not-walked`
  （`test/cases/modules/autodiff/library_reverse_{nn,softmax_layer_norm_pow,sum_to}.cml`），
  名字是 `softmax`、`layer_norm`、`conv2d`、`max_pool2d`、`avg_pool2d`、`embedding`、`sum_to`。
  要一列真正的读数，得给 `scripts/vjp-parity.mjs` 加一只会走树的探针，那是另一件活（第 7 节）。
- 四层读数（本页没改一行语义，所以它们就是**基线**，也将来要用来比"没有新增红"）：
  | 计划目录 | 一处一 run | `[SUMMARY]` | 红的（名字） |
  |---|---|---|---|
  | `feat/trans` | ✅ | `total: 106, pass: 93, fail: 13` | `agree_with_fvm`、`narrow_argmax`、`empty_struct_text`、`rc19_seed70_builds_after_specialize`、`failed_evaluation.{error,json}`、`failed_depth.error`、`failed_unreached.error`、`not_static.note`、`round_budget.error`、`dump_after_each_pass`、`macro_body_effect_stage`、`every_generator_writes_source_the_parser_reads` |
  | `feat/modules/autodiff` | ✅ | `total: 290, pass: 274, fail: 15, known 1` | `grad_structures`、`grad_function_value_{branch,static_branch,container}.{default,macro}`、`grad_function_value_recursion.macro`、`grad_function_value_passed.{default,macro}`、四条 `resnet*_vs_torch64`、`vjp_old_annotation_removed` |
  | `feat/linear` | ✅ | `total: 883, pass: 848, fail: 31, skip 2, known 2` | `render_grid.nvm`、`lambda_filter.nvm`、十格 `array_index_{out_of_range,negative}.{nvm,fvm,jit,inline_jit,prl,tf}`、`subscript.step_zero_const`、五格 `var_read_before_region.*`、`loops.native_callback.fault{,.scheduler}`、`loops.zero_leaf_state.{opt_jit,dump}`、`loops.open_rank_round_{scalar_read,iterate_round}`、`loops.grad_loop_collect`、四格 `loops.collect.large.*` |
  | `feat/para` | ✅ | `total: 104, pass: 82, fail: 22` | `bootstrap_medians`、三格 `unordered_foreach_smoke.*`、`unordered_foreach_timing.prl`、三格 `unordered_reduce_sum.*`、三格 `concurrent_alloc.*`、`ordered_operators_inline.prl`、四格 `map_chunks.*`、四格 `fault_deep.*`、`sync_block.predecessors`、`fault.predecessors` |
  `feat/para` 的红大半是同一个原因（`No matching function found for argument types: <(i: int) => string uses var stdout> (Range)`），
  与这件事无关：子树本身在红。
- `node scripts/check-generated.mjs`：exit **0**（`4 of 5 artifact(s) were compared and the tree agrees`，
  第 5 个 `ast_hashes.txt` 由 gate 8 管、这次没比）。`node scripts/check-doc-links.mjs`：exit **0**。

## 2. 值从哪里来：记录里的哪一项、谁写、谁读

树里**已经有**"正向留下中间结果"的通道，名字是**残差**（design principles 9.9：
`split(f)` 把函数拆成"现在能算的"与"读残差的"两段）：

| 谁 | 在哪 | 干什么 |
|---|---|---|
| 写 | `src/stdlib/transforms/split.cpp`（切点） | 把 `lin` 要读而算不出的值做成闭包/捕获端口；`REINTERP split … residuals=5` 就是这份清单的行数 |
| 读 | `src/stdlib/transforms/transpose/walk.cpp#knownValue` | 常量直读；否则 `forward(ref)`（`src/stdlib/transforms/transpose/walk.cpp#forward`）：闭包端口 `closurePortOf`、`with` 端口 `withPortOf`、捕获端口 `capturePortOf`、`Accs`、`Oper` 前向重算 |
| 只答形状 | `src/stdlib/transforms/rule_expand.cpp#answerTheShapeOf` | 律写 `shape(x)` 而 `x` 无值时，**从类型**答（`float[2, 3]` 说得出；`float[...]` 说不出） |

所以"律能读到正向留下的值"**今天就是成立的**：`mul_stated_t` 的 `ct * w`、`add_stated_t` 的
`sum_to(ct, ax)` 读的都是切点带过来的值。

**阳性对照（树里已有、今天绿的一条读数，不是我造的探针）**：
`test/cases/modules/autodiff/library_reverse_softmax_layer_norm_pow.cml` 的第一行是
`cube grad engine=12.0 library=12.0 same=true` —— `vjp((x) => x ^ 3.0)` 在 `x = 2` 上答 12.0，
而 12.0 是 `3x²`，那个系数是从**原值 `x`** 算出来的：线性半算得出它，说明"正向留下的值"能进到
转置之后的这一半。这条读数同时说明**为什么 `pow_t` 没被走**：`^` 的库腿走的是线性半里的算术
（`mul`s），不是 `pow_t`，而 `pow_t`/`pow_d_t` 的右半边和 `softmax_t` 一样读 `x`（被答的那一位）。

**读不到的那一格是"正在被答的那一位"**：`ruleCotangent` 把律的左参数按位置绑定 —— 被答的那一位由
**余切**填（`expandRuleByName(..., {handed}, position, ...)`），其余由 `knownValue` 填。于是
`valueOf(position)` 永远是"没有值"：那一格在转置图上就是余切端口。三条律都正好在这格里动手：

- `softmax_t`：`softmax_grad(softmax<axis>(x), ct, axis)` —— 第 0 位要 `x`（被答的那位）；
- `layer_norm_t`：`layer_norm_grad(x, gamma, ct, which)` —— 第 0/1/2 位分别要 `x`/`gamma`/`b`；
- `conv2d_t`：三个分量分别把 `x`、`w`、`b` 整只张量交给 `conv2d_input_grad` / `conv2d_kernel_grad` /
  `conv2d_bias_grad`，而这三个核**只用它们的形状**，形状又已经在类型里
  （`operand-types=f32[1, 2, 4, 4],…`）。律写成 `shape(x)` 就会被 `answerTheShapeOf` 答出来；
  写成 `x` 就没有人答。

这不是"我们没实现读值"，是**签名里没有那一格**。同一件事已经按"待裁"记在
`internal/reviews/2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md`
（状态：待裁，我方默认甲＝"只给律真的读了值的参数留一个值端口"；代价实测：98 个 `transpose(` 调用点的
arity 全变、4 处 `type=(float) => (float,)` 的 pin 要改）。同一页第 1 节第 1 条还写着最小形状
`g(x) = x * x` 也拒绝 —— 本页只是把它按三条 `nn`/归约律重新量了一遍，没有新语义。

## 3. 读不到的时候，今天怎么表达（以及为什么不能换成"看起来对的值"）

`TransposeRefusalCause::LawReadsAValue`（`src/stdlib/transforms/transpose.h#TransposeRefusalCause`，日志拼成
`law-reads-a-value`，由 `src/stdlib/transforms/transpose/walk.cpp#transposeRefusalWord` 拼出）—— 整次转置**按名拒绝**，`vjp` 抛错、退出非 0，程序不会跑出一个数。
这就是 design principles 9.9"缺规则必须拒绝"的形状，**是对的**，不需要新加"不适用"的表达：
"这条律不适用"在这棵树上就是"这一步按名拒绝，并且拒绝的原因点名到律"。

**不能改成的三种写法，以及它们各自会答错的数**（写在这里是为了让下一个人不要试）：

1. 把被答的那一位绑成余切 —— `mul_tensor_t` 里写得下的只是 `shape(x)`，而那是 `answerTheShapeOf`
   **从类型**答出来的（类型写不出时它同样按名拒绝：`add_tensor_t` 的注里量过
   `skip=law-reads-a-value uri=tensor:add`）；`conv2d_kernel_grad(x, ct, ct)` 要的是 `w` 的**张量**，
   `ct` 是**输出**形状、不是核的形状，形状与数全错。
2. 绑成 `zeros_like` —— `mul_tensor_t` 的注里已经量过：读零载体答出全零。
3. 让律"读不到就跳过、继续走" —— 余切被丢掉，等于把这一步的导数写成 0，是标准的"看起来对的错值"。

## 4. `softmax`/`layer_norm`/`conv2d` 该由谁走

**`softmax`、`layer_norm`：动 `src/stdlib/rulesets/jvp.cml`（正向半），不是动 `transpose.cml`。**
两条正向律的线性半写成了调用（`softmax_jvp`、`layer_norm_tangent`），而 `split` 的切点把"只依赖原值"的
因子留在正向半、把线性半压成直线（`jvp_relu`、`jvp_max_pool2d` 就是这么写的：`mul_stated(where(x > 0, …), dx, …)`）。
按那条路改写之后，`vjp` 能答出与引擎同的数（现在整条拒绝），但**`softmax_t`/`layer_norm_t` 仍然不会被走**：
直线线性半里的 `softmax<axis>(x)` 的操作数是**残差**，残差不收余切（`wantsCotangentAt` 对闭包成员返回
false），位置被跳过，律不会被查。它们的状态与 `exp_t`/`sigmoid_t`/`tanh_t` 完全一致 ——
`transpose.cml` 自己给这三条的注就是"律是给二阶、手写线性函数、以及某天正向律把算子放进线性半的那天用的"。
这条属于 **`jvp.cml` 车道**（`test/plans/feat/modules/autodiff_library_reverse.plan.toml` 的 `desc` 里
写的是同一句话）。

**`conv2d`：两条路，都不在 `transpose.cml` 里。**
- 甲（不待裁、但要动模块）：`src/stdlib/nn` 与其引擎注册（`src/stdlib/nn/tensor_ops.cpp#setVjp`）加一个
  **收形状而不是收张量**的孪生（`conv2d_input_grad_stated` / `conv2d_kernel_grad_stated`），律改成
  `conv2d_input_grad_stated(shape(x), w, ct)`。这条路能走通是因为形状在类型里（`operand-types` 已证），
  而 `answerTheShapeOf` 已经从类型答 `shape(x)`。属于 **`nn` + 引擎车道**；改完 `conv2d_t` 的判据在第 6 节。
- 乙（待裁）：作者在 `internal/reviews/2026-11-04-…-port-for-a-parameter-value.cn.md` 裁甲之后，
  转置成员给"被读到的参数"留值端口，`conv2d_t` 原样就能走。

**`pow_t`/`pow_d_t`/`max_pool2d_t`/`avg_pool2d_t`/`embedding_t`/`sum_to_t` 与本页同族**，但各有独立堵点，
量到的两条：
`max_pool2d` 的**正向**律连端口数都对不上（`REINTERP jvp skip=rule uri=nn:max_pool2d reason=No matching
function found for argument types: <> (f32[?, ?, ?, ?], float[...], int[4], int[4])`），所以
"`avg_pool2d_t` 是今天唯一走得到的 `nn` 律"这句（`src/stdlib/rulesets/transpose.cml#avg_pool2d_t` 的注）在这棵树上**已经不成立**，
它是这棵树之前的事。这一句要改，但它属于 `jvp.cml` 车道。
`pow_t`/`pow_d_t` 的右半边读 `x`（第 2 节那条 `cube` 读数），`^` 的库腿今天是靠线性半里的算术答出来的。

## 5. 探针（不入 git，抄在这里以便重跑）

三个用例都只做一件事：`vjp(f)` 跑库腿，打开 `--trace transforms`。

```cml
// softmax
import { vjp } from vjp
import { softmax, sum } from tensor
func softmax_total(x: float[3]) => float {
    return sum(softmax<0>(x) * [1.0, 2.0, 3.0] as float[3])
}
func main() => int sync {
    let (_, pullback) = vjp(softmax_total)([1.0, 2.0, 3.0] as float[3])
    `probe softmax answer={}`->format<pullback(1.0).0>->println
    return 0
}
```

```cml
// layer_norm
import { vjp } from vjp
import { layer_norm, sum } from tensor
func layer_norm_total(x: float[2, 3]) => float {
    return sum(layer_norm(x, [1.0, 2.0, 1.0] as float[3], [0.0, 0.5, 0.0] as float[3]))
}
func main() => int sync {
    let (_, pullback) = vjp(layer_norm_total)([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]] as float[2, 3])
    `probe layer_norm answer={}`->format<pullback(1.0).0>->println
    return 0
}
```

```cml
// conv2d，对 x 求导；把 conv_total 的参数换成 w 就是对核求导（第 1 节第二条读数）
import { vjp } from vjp
import { conv2d } from nn
import { reshape, sum, tanh } from tensor
func conv_total(x: f32[1, 2, 4, 4]) => float {
    return float(sum(tanh(conv2d(x, conv_kernel(), conv_bias()))))
}
func main() => int sync {
    let (_, pullback) = vjp(conv_total)(image())
    `probe conv2d answer={}`->format<pullback(1.0).0>->println
    return 0
}
```

跑法（一次一个计划文件，`-Plan` 用正斜杠）：

```powershell
pwsh -NoProfile -File <tree>\internal\tools\camel-remote.ps1 `
  -Path <tree> -Plan internal/_tcprobe/probe_walls.plan.toml -Jobs 2 -NoGpuWatch
```

## 6. 判据：这三条名字什么时候可以标成 walked

改完第 4 节的任一条，下面三件事**同时**成立才算走到（今天前两条都不成立）：

1. `--trace transforms` 里**不再有**上面那三条 `skip=…`；出现的是
   `TRANSPOSE function=split_of_jvp_of_<f>_backward graph=… rhs=<law 名>`；
2. 库腿印出的数与引擎腿**逐位相同**（对 `conv2d`：`grad(conv_input)` 与
   `pullback(1.0).0` 同一组数；对 `softmax`/`layer_norm`：`test/cases/modules/autodiff/library_reverse_*`
   里已经印好的引擎数）；
3. `test/cases/modules/autodiff/library_reverse_*.cml` 里那三处 `library=filed-not-walked`
   一起改掉 —— 只改字样不改数、或者只改数不改字样，都不算。

## 7. 本页没有做的事

- 没有改 `src/**`、`test/**`、`scripts/**`，没有动 `scripts/vjp-parity-exemptions.tsv`；
- 没有动 `known_fail`，没有改任何断言；
- `vjp-parity` 里**没有** `walked`/`not-walked` 这一列 —— 它数的是键（`178 law(s) filed under 74 key(s)`，
  `--closure` 读 `transpose.cml` 61 律 / `jvp.cml` 85 律、`0 missing`）。"走没走到"今天唯一的机器可读
  记录是六处手写的 `library=filed-not-walked`；要把它变成一条读数，得给 `scripts/vjp-parity.mjs`
  加一列，那是另一件活。