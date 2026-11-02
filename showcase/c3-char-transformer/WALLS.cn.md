> **第一个"模型真跑起来"的数：`c3 forward init loss=3.303745`**（两层两头，chars=4278、context=8、
> width=16、heads=2、classes=28、seed=7）。 以前从来没拿到过；拿到它是因为 `layerNormInfer` 不再按
> "形状未知"把 `float[..]` 的归一化结果退回 `f32`（墙 10 的更正）。 墙再多，也比不上这一行。
# C3 的墙

这一项交的是墙，不是训练曲线。 下面每一面墙都有三样东西：一个能单独跑的最小程序、它的原始诊断、
以及「要让它倒，得改哪里」。 诊断文字是 runner 打印的原样，没有改写。

## 测量条件

| 项 | 值 |
|---|---|
| `camel --version` | `Camel v0.2.42 (Build 261102_140104_ef60bd42a)` |
| 分支 | `lane-showcase`，从 `origin/develop` 的 `ef60bd42a8978b80a801f4fcb4e590f7cbf55605` 分叉 |
| 测量所在的树 | 主机 `/home/zjwei/camel-work/showcase`（工作树 `D:\Projects\Camel-wt\showcase`） |
| 二进制构建时间 | 每次运行由 runner 打印，最后一次为 `2026-11-02T09:21:43-05:00` |
| 跑法（一次一个 plan） | `& 'D:\Projects\Camel-wt\camel-remote.ps1' -Path D:\Projects\Camel-wt\showcase -Plan test/plans/perf/showcase_c3.plan.toml -Jobs 8` |
| 全部探针 | `python showcase/c3-char-transformer/probe.py`（在树的根上跑） |

## 十八个探针的答案

`probe.py` 把「该有答案」写进自己（`EXPECTED`），探针翻面就非零退出：该拒的答了是墙倒了，该答的
拒了是回归，两种都得重写这份报告。

| 探针 | 构造 | 今天的结果 |
|---|---|---|
| 13 `13_jvp_scalar` | `jvp` 对单个 float | **有答案**（`9` 与 `6`） |
| 14 `14_jvp_call` | 被求导函数里有一次调用 | **有答案**（`18` 与 `12`） |
| 16 `16_layer_norm_dtype` | `layer_norm` 在 float 上的元素类型 | **有答案**（float 进、float 出） |
| 17 `17_draw_dtype` | `normal::<float>` 与 `zeros::<float>` 相加 | **有答案**（同一元素类型） |
| 18 `18_take_dtype` | `take(table, [0])` 这一行的元素类型 | **有答案**（float 进、float 出） |
| 01 `01_grad_scalar` | 库 `grad` 对单个 float（控制组） | 拒：**墙 1** |
| 02 `02_grad_table` | 数组参数按位置读 | 拒：**墙 1** |
| 07 `07_two_parameters` | 两个参数、一个 cotangent | 拒：**墙 1** |
| 10 `10_with_parameter` | `with` 参数上的模型树 | 拒：**墙 1** |
| 11 `11_grad_of_a_model` | `grad` 指向一个模型 | 拒：**墙 1** |
| 12 `12_attention_block` | 一个 head 的完整块 | 拒：**墙 1** |
| 03 `03_matmul` | 矩阵乘 | 拒：**墙 2**：`jvp` 对 `tensor:matmul` 没有前向规则 |
| 04 `04_softmax` | 沿 key 轴的 softmax | 拒：**墙 3**：`tensor:softmax` |
| 05 `05_reduction` | 沿轴归约（layer norm 的均值与方差） | 拒：**墙 4**：`tensor:mean_axis` |
| 06 `06_layout` | reshape/transpose（切分与合回头） | 拒：**墙 5**：`tensor:reshape` |
| 08 `08_embedding` | 从表里读一行 | 拒：**墙 6**：`tensor:take` |
| 09 `09_causal_mask` | 因果 mask 的加法 | 拒：**墙 7**：`tensor:subtract` |
| 15 `15_mask_in_f32` | 用 `where` 造一个 f32 的常量 mask | 拒：**墙 8** |

`probes total=18 ok=5 refused=13`，拒的落进 8 个互不相同的诊断。

## 墙 1：库级 `vjp` 整块不能用（挡路的一面）

最小程序：`showcase/c3-char-transformer/probes/01_grad_scalar.cml`（`grad` 对 `x*x` 在 3 处求导）

诊断（原样）：

```
(vjp) .../out/latest/stdlib/vjp.cml:78:47: [Error]: Function of type '(float, float) => float'
cannot be called with argument types: <> (float). [in the instance 'vjp<(x: float) => float>'
of the generic function ...] (name=SemanticDiag::ArgumentsMismatch)
(train) .../train.cml:47:1: [Error]: Cannot import module 'vjp': it has errors.
```

- **归 类**：变换拒绝（transform refusal）——拒绝发生在 `vjp` 自己的组合里，不是被求导程序里的构造。
- **挡住的**：`import { vjp } from vjp` 失败，于是 `train.cml`（含它的 `mode=forward` 分支）一行都
  跑不到；六个 API 级探针全部停在这里。
- **性质**：这就是 develop 上那 29 个红的同一个根（另一条 lane 在修），所以这里只记读数，不动
  `stdlib/**`。
- **要让它倒**：修 `stdlib/vjp.cml#vjp` 那处把 `(float, float) => float` 用单个参数调用的地方（第 73、78 行）
  （组合时的参数列表与类型律不一致）。

## 墙 2–7：前向规则缺失（`rulesets/jvp.cml`）

六面墙的形状完全相同，只有算子名不同。 以 `08_embedding.cml` 为例，原样诊断：

```
08_embedding.cml:10:15: [Error]: An error has occurred: transforms: jvp has no forward rule for
`tensor:take` (the operator has no forward rule bound at all). [raised here at 16:28]
C [RuntimeDiag::RuntimeError]
```

其余五个只是把 `tensor:take` 换成各自的算子名。 归 类都是**缺规则**：

| 墙 | 算子 | 最小程序 | 挡住的是 C3 的哪一段 |
|---|---|---|---|
| 2 | `tensor:matmul` | `probes/03_matmul.cml` | 每个 `x @ w` |
| 3 | `tensor:softmax` | `probes/04_softmax.cml` | attention 的权重 |
| 4 | `tensor:mean_axis` | `probes/05_reduction.cml` | `layer_norm` 的均值/方差 |
| 5 | `tensor:reshape` | `probes/06_layout.cml` | 多头切分与合回头 |
| 6 | `tensor:take` | `probes/08_embedding.cml` | 字符 embedding 的查表 |
| 7 | `tensor:subtract` | `probes/09_causal_mask.cml` | mask 与残差的减法 |

**要让它倒**：`stdlib/rulesets/jvp.cml` 里每个算子加一条前向规则（`f` 与切向如何一起走），
`stdlib/rulesets/transpose.cml` 里同一算子再加一条反向规则——`vjp` 是
`transpose(split(reinterp<jvp>(f)))`，两层都缺才是「`vjp` 也走不动」的完整原因。

### 墙 2 已经补过一半：规则写进去了，但抬不起来

2026-11-02 深夜给 `tensor:matmul` 补了前向规则（`jvp_matmul` → `matmul_tangent`，
`d(a·b) = da·b + a·db`，两个切向都在右式里），并加了一条用例锁住它：
`test/cases/compile/transforms/jvp_matmul.cml`（今天是**期待拒绝**的形态）。键不用登记——
`forward.h` 的 `ruleKeyOf` 从算子名自己推出 `matmul`/2——所以 `matmul` 上那句「没有前向规则
绑在这上面」已经不出现了。

它还答不出数：一个常量只有「有 pair 的类型」才有零切向，而 `jvp.cpp` 的 `pairTypeOf`
只给浮点标量写真 pair（`zeroConstantFor` 也只写 `f64`/`f32` 的零），于是 `matmul(A, x)` 里的
常量 A 拿不到切向节点，身体在 `complete = false` 处停下，报的是没有 `file:line` 的
`transforms: jvp has no forward transform for this function`。要补的是**一块**：让抬升能给
数组类型的值一个零切向，不是改 matmul。

顺手试过、并**已回退**的一条路：把 `matmul_tangent` 放进 `stdlib/autodiff.cml`（加
`import { matmul } from tensor`）。它让 6 条本来绿的用例转红（`loop_grad_struct_state` 四后端、
`loop_grad_parameter_array.shape`、`fusion_group.profile.none`），红在 tape 反传图里
`add: the two operands have different element types ('void' and 'f64')`；把同一个函数留在
`rulesets/jvp.cml` 里则 `modules/autodiff` + `modules/fusion_group` 280/280 全绿。
**结论**：那个模块导出的泛型集合是别处依赖的东西，加一个导出函数不是局部改动。

手算已写进用例注释与计划 `desc`，规则一旦能被答出，那条用例从「期待拒绝」翻成
`value=5.0000 tangent=1.0000` / `tangent=2.0000`。

## 墙 8：常量张量没有单一元素类型的写法

最小程序：`probes/15_mask_in_f32.cml`

```
15_mask_in_f32.cml:12:57: [Error]: No matching function found for argument types: <> (int[], float).
```

`full::<f32>([2, 2], -1000.0)` 不成立：形状是 `int[]`、值是 `float`，而 `full::<T>` 要一个 `T`。
模型里唯一的常量张量就是因果 mask，于是：

- `zeros::<f32>` 拿得到 0，`full(-1000.0)` 拿不到 −1000，`where` 又要求 `bool[..]` 的**张量**条件，
  而 `bool[]` 喂不进 `reshape<[8, 8]>`（它的签名只收数值数组）。
- **归 类**：缺规则（**类型/常量规则**），四类里最接近「缺规则」，但它缺的不是微分规则而是
  张量构造规则，报告里单列。
- **要让它倒**：`full`/`fill` 接受与目标元素类型不同的数值字面量（或提供 f32 写法），或让
  `reshape` 收 `bool[]`，或给 `where` 一个标量分枝的重载。

## 墙 9：模型写不成一个元素类型（挡住 loss 的那一面）

最小程序：`forward.cml`（与 `train.cml` 同一个模型、同一种子，只去掉 `import` 与训练循环）

```
(forward)/showcase/c3-char-transformer/forward.cml:177:33: [Error]:
No matching function found for argument types: <> (f32[..], float[..], float[..], ...)
(forward)/showcase/c3-char-transformer/forward.cml:192:45: [Error]:
Invalid call to '__mat__' with argument types <> (f32[..], float[..]):
the two tensor operands have different element types ('f32' and 'float')
```

模型的字段都声明 `float[..]`，第一处不匹配出现在 `attention(layer_norm(x, ga, ba), ...)` 的第一个
实参上：`layer_norm` 的回答被当成 `f32[..]`。 已经排除的候选（各自都有一个今天**有答案**的探针）：
`layer_norm` 的 float 分枝（16）、`normal::<float>`（17）、`take` 的读行（18）。 所以这条的成因
**没有定位**，只剩「`with` 参数里读出的字段类型」与「`+` 之后元素的类型」两个候选——下一个探针该
写的是后者，程序见本节末。

- **归 类**：缺规则（**元素类型传播**）。四类之外。
- **挡住的**：`forward.cml` 的 `c3 forward init loss=`，也就是 step 0 的 loss；`train.cml` 的每一天。
- **要让它倒**：先定位（下面的三行程序），再谈修法。 写法：

  ```camel
  import { sum, zeros } from tensor
  func main(): int sync {
      let a = zeros::<float>([1, 2])
      let b = zeros::<float>([1, 2])
      println(`add keeps = ${sum(a + b):.6f}`)   // a+b 是 float 还是 f32：换成与第三个 float 张量相加即知
      return 0
  }
  ```

## 拿到的读数

| 读数 | 值 |
|---|---|
| 前向：Camel vs 解析值 | `0.455920556651` vs `0.455920556651`，相对误差 `0.000e+00`（容差 `1e-6`） |
| 反向：引擎 `with_grad` 六个参数 vs 解析值 | 全部 `0.000e+00`（容差 `1e-4`，最差参数 `wq.0 = -0.021925348589`） |
| 解析值自身的可靠性 | 与中心差分（`h=1e-6`）最差 `4.828e-10` |
| 库 `vjp` 的同一行 | 拒（墙 1），没有数 |
| 跑法 | `python showcase/c3-char-transformer/compare.py`（`c3 compare: PASS`） |

引擎那一行**不是**这一项要的路径，它只用来证明块的算术是对的：前向与六个偏导都和手推值一致，
所以库拒掉的东西是可算的，问题在库这一侧。

## 拿不到的读数

- **每参数梯度对比（库路径）**：墙 1。
- **loss 在 step 0/1/10/N**：step 0 需要墙 9，step ≥ 1 需要墙 1 与墙 2–7。 一条也没拿到，命令是现成的：
  `camel showcase/c3-char-transformer/forward.cml --arg corpus=showcase/c3-char-transformer/corpus.txt`
  与 `camel showcase/c3-char-transformer/train.cml --arg corpus=... --arg mode=train --arg steps=2`。

模型没有缩小：`train.cml` 就是作者要的两层、两头、宽 16、context 8；拿不到曲线不是模型太大，是上面
九面墙。

## 全量闸门（`-Plan all -Tier fast -Jobs 8`）

| 时点 | total | pass | fail | skip | known |
|---|---|---|---|---|---|
| 分叉点 `ef60bd42a`（动手前） | 8522 | 8473 | **29** | 10 | 10 |
| 本 lane 收尾（引用修好之后） | 8522 | 8473 | **29** | 10 | 10 |

- total 一样，说明新增的 `test/plans/perf/showcase_c3.plan.toml` 没有被 fast 层选中：它的 `tier` 是
  `milestone`，只有点名或 `--tier milestone` 才跑它。那四条今天全绿。
- 29 个红里 26 个是 `vjp`/`grad` 家族（墙 1 的同一个根），另 2 个（`registry.docs_complete`、
  `tools.autodiff_migration_ledger.ratchet`）是读整棵树的守卫，消息里报的也是 `vjp.cml` 那处。
- 中途我看过一次 `8472 / 30`：多出来的那一个是本目录的文档引用写法（带行号地引用库文件，
  `docs.links.citations` 要求引用符号）。 改成符号引用后回到 29——那是**我的红**，不是墙。

## 我自己写错、不算墙的

- 张量算子的附加参数写在**尖括号**里（`mean<1, true>(x)`、`reshape<[3, 2]>(x)`、`softmax<1>(x)`），
  不写在圆括号里。 诊断会直接把该写的写法念出来。
- `read_text` 的 `!!` 要给兜底值（`!! ''`）。
- `if` 的每个分枝都要 `then`（`if mode == 'forward' then { ... }`）。
- 这几处修好之后探针的答案才变成上面那张表：前两轮的红是我自己的拼写，不是墙。

## 墙 10（新）：`nn.layer_norm` 把 `float[..]` 归一化成 `f32[..]`

把注意力块长成 Transformer 时撞到的第一面新墙。 坐标：

```
grow/stage2_residual_ln.cml:45:35: [Error]: No matching function found for argument types:
<> (f32[..], float[..], float[..], float[..]). Check the number and ...
    attention: (x: float[..], wq: float[..], wk: float[..], wv: float[..]) => float[..] pure
 (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

- 说得出来的：`layer_norm(first, m.g1, m.b1)` 的实参三个都是 `float[..]`，它照样接受了，**回来的是
  `f32[..]`**；紧跟着的 `attention`（第一个参数 `float[..]`）就拒绝了。 两个重载都在
  `stdlib/signatures/tensor.cml`：f32 的在 305 行、f64 的在 313 行，浮点实参走到前面那个（隐式窄化）。
- 说显式类型也不行：`layer_norm<float>(x, g, b)` →
  `grow/probe_layer_norm_float.cml:15:24: [Error]: 'float' is used as a value here, but it names an
  operator with 8 signatures [...]`。
- 归类：这是原来「墙 9（元素类型传播）」的第一个**精确坐标**——不是新的一句诊断，是同一根现在有了
  文件名和列号。 整模型（两层，`grow/stage4_full_model.cml`）就停在同一处，`stage4` 的用例把它
  锁成 negative，消息里那句 `f32[..], float[..]` 就是它。
- 清掉之后：`baseline/grow.py --stage 2` 的四个读数是验收值——step1 `0.000000000000`、
  step10 `-0.072004447310`、step25 `-0.192068171421`、step50 `-0.836281312279`、更新后
  `-0.856052891460`。 `stage4` 同理，由 `--stage 4` 给出。

### 没有采用的"绕法"

手写一个 `layer_norm`（`sum`/`sqrt` 拼出来，全程 `float`）能让 stage 2 跑起来，但那是**绕开库的
重载缺陷**，不是修它，所以**没有写**。 能跑的形状（stage 1、stage 3）都是"本来就不需要
`layer_norm`"的形状，不是把 `layer_norm` 换掉。

## 交接给 `splitlift` 车道的一条：`pairTypeOf` 只给浮点标量

- 这条不是本目录的墙，是另一条车道正在拆的同一根：`jvp` 的 `pairTypeOf` 只给浮点标量写 pair，
  `zeroConstantFor` 也只给 `Float64`/`Float32` 造零，于是 `matmul(A, x)` 里那个常量数组操作数
  没有切向节点，抬升在 `complete = false` 处停下，报的是无坐标的
  `transforms: jvp has no forward transform for this function`。
- 本目录已经做了一半：`matmul` 的前向规则（`jvp_matmul`）已经进 `stdlib/rulesets/jvp.cml`，
  键由 `forward.h` 从算子名推出，"没有前向规则"那句已经消失。
- **清掉后 `matmul` 的 `jvp` 会给出数，验收值就在用例注释里**：
  `test/cases/compile/transforms/jvp_matmul.cml` 那六个数（`A=[[1,2],[3,4]]`、`x=(1,2)`，
  `dx=(1,0)` → 切向 1，`dx=(0,1)` → 切向 2）。 那边落完，这边那条 negative 用例自己会变成
  正例，只需把 `expect_exit` 与两条 `expect_stdout_contains` 换成那六个数。

## 墙 10 的更正：不是声明写错，是 `layerNormInfer` 的推断门

我按"声明写错"试过两种改法，**都没有效果**（都实测过，各跑一次 showcase 计划，stage2 的红原样）：

1. 把 `f32` 与 `float` 两个 `layer_norm` 声明**换顺序**（`float` 在前）——`grow/stage2_residual_ln.cml:45:35` 的红不变；
2. **删掉** `f32` 那条声明，只留 `#op('tensor:layer_norm')` 的 `float` 一条——红也不变。

真正的缺陷是一行推断：`modules/tensor/ops/reduction_ops.cpp` 的 `layerNormInfer` 结尾原来写的是

```
x && ctx.facts(0).dtype == TypeCode::Float64 ? Float64 : Float32      // x 是 shape，不是 dtype
```

也就是**形状未知就退回 `f32`**，哪怕 `dtype` 明明是 `f64`。 `float[..]` 实参正是"形状未知、类型 f64"，
于是归一化的结果被声明成 `f32`，下一个 `matmul` 就拒。 改成同文件邻居 `softmax_grad` 已经在用的
`floatLikeFirst(ctx)`（只读 dtype），一行，不动算法。

清掉之后（实测）：`forward.cml` —— **整个两层两头的模型**——跑起来了，打出
`c3 forward init loss=3.303745`（chars=4278、context=8、width=16、heads=2、classes=28、seed=7）。
这是模型自己的第一个 loss，以前从来没拿到过。

## 墙 11（新）：`layer_norm` 的**梯度**对 `f64` 一律回 `f32`

墙 10 清掉之后 stage2 / stage4 都编译通过，停在运行期：

```
(attention.pb):?:?: [Error]: An error has occurred: matmul_grad_lhs: the two operands have
different element types ('f32' and 'f64'): a tensor operator computes in one element type
```

- 坐标：`modules/tensor/kernels/gradient.cpp` 的 `layerNormGrad` —— `asFloat(x)` 收下 f64 转成
  `float`，输出却写死 `createTensor(TypeCode::Float32, ...)`（`Input` 那一支在 433 行），所以
  **f64 的归一化梯度是 f32 的**。 这跟 `docs/handover/reports/code-review-autodiff.md` 的 H3
  （`gelu_grad`/`erf_grad`/`layer_norm_grad` 按 f32 读 f64）是同一处，这里只是第一次在**训练循环里**
  撞到它，而且是在**引擎 `with_grad`** 上撞到的，不是库路径。
- 为什么现在才炸：`layer_norm` 在反向链的**最后一环**时不炸（梯度没再流进 matmul），所以
  `grow/probe_f32_in_pullback.cml` 的三段各自都能过——`sum(layer_norm(x,g,b))`、
  `sum(attention(x))`、`sum(layer_norm(x) @ w)` 都绿；一旦归一化在中间（残差 + 再进注意力），
  那个 f32 梯度就进了 `matmul_grad_lhs`。
- 没修：这是内核的读写宽度，不是声明；按"只改声明"的界，停在这里报。

## 墙 11 的处理：`layerNormGrad` 现在按输入的元素类型写

改的就是这一句（`modules/tensor/kernels/gradient.cpp`，原来是两处写死）：

```
Array *out = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);        // 原来是 433
Array *out = createTensor(TypeCode::Float32, ws->shapeSpan(), allocator, true);  // 原来是 470
```

现在函数体按 `SumElement<Code>` 模板化（`layerNormGradArm<Code>`），由 `x->dtype()` 分发：
`Float64` 进 `double` 那支、`Float32` 进 `float` 那支，输出也写同一个 `Code`。 累加本来就是 `double`，
所以 f32 的读数一个比特没动，f64 的梯度才是真的 f64。 依据是同一文件里 `sumToArm` 的同一套写法
（`SumFunction`/`SumElement<Code>`）。

**普查（`git grep -n 'createTensor(TypeCode::Float32'`）：22 处**
- `modules/tensor/kernels/gradient.cpp`：**5 处**，387、406、433、470、567 —— 这是 H3 那一族
  （`erf_grad`、`gelu_grad`、`layer_norm_grad` 的两支、`matmul_grad`）。 我改了 433/470。
- `modules/tensor/kernels/conv.cpp` 11 处、`gemm.cpp` 1 处、`layout.cpp` 1 处、`tiles/src/module.cpp` 2 处、
  `ops/layout_ops.cpp` 1 处：这些算子的**前向**本来就是 f32 层（conv/pool/batch_norm/gemm），
  不是"收 f64 却写 f32"，先不动。

## 墙 12 的更正：`matmul_grad` **不是**墙，3e-6 是参照自己的误差

我按"下一个写死 f32 的是 `matmul_grad`"报过一次，**这个判断错了**，两处证据：

**一、`matmul_grad` 的 f32 存货对 f64 根本走不到。**
`matmulGradGemm` 一进门就自己把门关上（`modules/tensor/kernels/gradient.cpp#matmulGradGemm`）：

```
if (lhs->dtype() != TypeCode::Float32 || rhs->dtype() != TypeCode::Float32 ||
    dy->dtype() != TypeCode::Float32 || lhs->rank() < 2 || rhs->rank() < 2) {
    return nullptr;
}
```

f64 的乘积返回 `nullptr`，走的是 `matmulGradLhs`/`matmulGradRhs` 里的通用路径
（`matmul` + `sumTo` + `reshape`），那三个都是按元素类型分派的。 所以那一处
`createTensor(TypeCode::Float32, …)` 只服务 f32 快路，**不该动**；"模板化 + 按 dy 分发"会白改。

**二、3e-6 是我的参照算错了，不是引擎偏了。**
先卡位置：把 stage2 跑 3 步，本程序 step2 = `-0.008000314590`，参照 `-0.008000314670`，
**只差 8.0e-11**。 任何残留的 f32 存货都会给出 ~1e-7 的差，8e-11 排除它。
那 8e-11 是中心差分的截断+抵消误差，而这个 stage 的开头恰好最受不了一点误差：
第一步的 loss 对除了最后一个 `beta` 以外的参数**恒为 0**，Adam 的 `v` 也是 0，
除法 `sqrt(v) + 1e-8` 把这点误差放大 —— 8e-11 到第 50 步变成 3.0e-6。

改参照的办法是**复步求导**（`grow.py --exact`）：`loss(x + ih)` 的虚部就是导数，机器精度、
没有相减抵消。 换上之后，参照与引擎在 **12 位上一个字都不差**：

```
stage2 step=50 loss=-0.836278289207     stage2 done steps=50 last=-0.856049871704
```

也就是说：**先前那组"验收值" `-0.836281312279` / `-0.856052891460` 才是噪声大的那一组**
（差分参照自己的误差），引擎一直在给对的数。 这条写进用例了，stage2 十五个数全按 12 位钉死；
stage4 的参照（`grow.py --stage 4 --exact`）也写出来了，十五个里十四个钉到 12 位，
剩下 step1 那个 0 两边符号不同（参照 +0.0、引擎 -0.0），只钉前缀。

## 收尾两条

**一、`f64` 没有 dgemm 快路（性能，未做，不是待修）。**
`matmulGradGemm` 只认全 f32 的输入，f64 一律回落通用路径（`matmul` + `sumTo` + `reshape`）；
前向 `matmul` 的 gemm 快路同理。 这**不是缺陷**：读数与复步参照逐元素一致（见下），只是 f64 走的
是通用实现。 要做的是给 f64 补上 dgemm 快路，那是**性能题**，记在这里当"未做"。

**二、第一步的梯度：引擎 vs 复步参照，逐张量逐元素**

用例 `grow/grad_first_step.cml` 打印引擎的梯度，参照 `baseline/grad_first_step.py` 用复步算同一个
梯度，`--engine` 直接比：

```
compare tensors=10 elements=32 max_abs=1.588e-16 at g2[0] max_rel=2.019e-12 at g2[0]
compare missing=none shape_mismatch=none identically_zero=['wq1','wk1','wv1','g1','b1','wq2','wk2','wv2']
```

- **张量 10/10、元素 32/32**，没有形状对不上的，最大绝对差 **1.588e-16**、最大相对差 **2.019e-12**。
- **恒为 0 的梯度有 8 个张量**：`wq1`、`wk1`、`wv1`、`g1`、`b1`、`wq2`、`wk2`、`wv2`。
  全模型只有两个张量在第一步有梯度：`g2 = [+7.866746175404845e-05, -7.866746175404845e-05]`、
  `b2 = [2.0, 2.0]`（最后一层归一化的 beta 每个元素各收两行，所以正好是 2）。
- 这条同时把 3e-6 那件事解释到底了：**第一步十个张量里八个梯度恒 0**，Adam 的 `v` 就是 0，
  `sqrt(v) + 1e-8` 于是把参照的差分噪声放大四个数量级 —— "恒为 0"正是最容易藏错的地方，
  也是误差被放大的入口。

## stage4 的第一步梯度：25 张量 / 92 元素

同一条读数，换成整模型（用例 `grow/grad_first_step_stage4.cml`，参照 `grad_first_step.py --stage 4`）：

- **张量 25/25、元素 92/92**，`missing=none`、`shape_mismatch=none`。
- 有梯度的只有最后两层归一化的两个参数：**`gout` = [+1.9999783584360868, -1.9999783584360866]**、
  **`bout` = [2, 2]**；引擎给 `[1.9999783584360866, -1.999978358436087]`，差 **2.2e-16 / 4.4e-16**
  （相对 **1.1e-16 / 2.2e-16**）。
- **其余 23 个张量在两边都是"0"**：参照侧最大 \|值\| **3.6e-16**、引擎侧 **2.3e-16**，都是舍入残差。
  所以**最大绝对差 ≤ 5.8e-16**；而**最大相对差在这 23 个上无意义**（真值是 0，0/0），只在
  `gout`/`bout` 上有意义，是 **2.2e-16**。

**为什么是 0（结构性，不是"两边一起漏了"）**：loss 是 `sum(layer_norm(second, gout, bout))`，
而 `gout` 在起点恰好是 `[1, 1]`。 归一化后的每个元素在自己那一行里**和为零**，乘以相同的 gamma、
再整片求和，就整个抵消掉了：`sum ≈ rows * sum(bout)`，**与 `second` 无关**。 于是
`d loss / d second ≡ 0`，`second` 上游的一切（两个 block 的全部参数、embedding）梯度都为 0，
只剩下最后那层的 `gout`（= 每列归一化坐标对行求和）和 `bout`（= 每行一个 1）。
我用 numpy 单独验过这一点：`sum(ln(t, [1,1], 0))` 对任意 `t` 都给出 0（误差 ~1e-16）。
**一旦 `gout` 离开 `[1,1]`（第一步之后它就会动），这个抵消就不完全了，上游梯度才真正开始工作**——
所以 stage4 的第 2 步 loss 才是 -0.015999913354 而不是 0。

**一句可复用的判据**：*全零梯度是最容易被两边一起漏掉的地方；比梯度时，零张量要单独点名**为什么**是零。*
这里能点名的原因是上面的抵消；若某个零说不清理由，那个张量就是嫌疑，而不是"对上了"。
