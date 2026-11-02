> **读这份清单之前先知道一件事：起点处上游梯度全为 0 是结构性的，不是丢了环。**
> loss 是 `sum(layer_norm(second, gout, bout))`，起点的 `gout = [1, 1]`：归一化后每行坐标和为零，
> 乘同一个 gamma 再整片求和就全抵消，`sum ≈ rows * sum(bout)`，**与 `second` 无关**。
> 于是 `d loss / d second ≡ 0`，它上游的一切（两个 block、embedding）梯度都为 0，只剩 `gout`/`bout`。
> 一旦 `gout` 离开 `[1, 1]`（第一步就会动），抵消只剩部分，上游才真正开始学。
> 详见文末「stage4 的第一步梯度」；**看到一个全零梯度，先问它为什么是零，再决定它是不是错了。**
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

**墙 1 的根因之一（2026-11-03 补，另一条车道的读数）**：类型包拼接把**数组参数按类型合并**。`(..ParamsOf<F>, ..Tangent<ParamsOf<F>>)` 在 `F` 有三个数组参数时给出 **2 个成员**（应 6 个）⇒ 调用点按 2 个实参填、被调方按 6 个读 ⇒ 上面那句 `cannot be called with argument types: <>`。
- **判据**：纯函数最小例，`Swap` 对 1 / 2 / 3 个数组参数**全为 true** —— 分解本身没错，**拼接之后重数丢了**。
- **现状**：拼接在类型层完成，重数在有数组参数时必错；与 `stdlib/vjp.cml#vjp` 的那处调用是同一堵墙的两面，不改这里它也不倒。
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

**与墙 9 的关系（2026-11-03 查重）**：同族、不同修法，不合并；整模型那一侧见墙 9。

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

**与墙 8 的关系（2026-11-03 查重）**：同一族（张量的元素类型说不清），但**修法不同** —— 墙 8 要的是"常量张量怎么带元素类型写"，这里是"整个模型的元素类型要一致"⇒ 不合并，只互相参见。

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

**与墙 11 的关系（2026-11-03 查重）**：同一族（`layer_norm`）的**前向**与**梯度**两侧，但**修法不同**（这里是推断门，11 是梯度按输入元素类型写）⇒ 不合并，只互相参见。

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

**与墙 10 的关系（2026-11-03 查重）**：同族、不同修法，不合并；前向那一侧的推断门见墙 10 的更正。

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

**结论与三条证据（2026-11-03 复核，撤稿成立）**：

1. **控位**：stage2 跑 3 步，引擎 `-0.008000314590` 对参照 `-0.008000314670`，**绝对差 8.0e-11**（对 ~8.0e-3 的量 ⇒ 相对 ~1e-8）；到第 50 步放大成 3.0e-6。
2. **复步参照 12 位一致**：用 `grow.py --exact`（`loss(x + ih)` 的虚部即导数，无相减抵消）重算，参照与引擎在 12 位上一个字都不差 ⇒ 原来的中心差分参照自己是误差源。
3. **`matmulGradGemm` 的 dtype 门**：`lhs/rhs/dy` 任一不是 `Float32` 就 `return nullptr` ⇒ f64 物理上走不到那条 f32 快路（`modules/tensor/kernels/gradient.cpp#matmulGradGemm`）；且真有 f32 存货会给出 ~1e-7 相对（≈8e-10 绝对），比实测大一个量级。

**关于 `grad` 的一个调用前提（2026-11-03，更正一次误判）**：`grad` 有两个 —— `stdlib/vjp.cml#grad` 是**单 `float` 实例**（`(x: float) => float`），`stdlib/signatures/autodiff.cml#grad` 是泛型，**只对函数的第一个 `with` 参数求导**（设计原则 9.9：权重是 `with` 参数，普通参数是数据）。我最初按"`grad` 只接受一个 float 参数"记，那是**调用方式不对**（导错了 `grad`、也没给 `with` 参数），不是签名限制，故不成墙。

**第四项：闭式标量算术（2026-11-03，不用 `vjp`/`grad` 的矩阵路径）**：`A = [[1,2],[3,4]]`、`B = [[5,6],[7,8]]`、`dC = 全 1` 时纸面 `dL/dA = dC·Bᵀ = [[11,15],[11,15]]`、`dL/dB = Aᵀ·dC = [[4,4],[6,6]]`。用八个**单参数标量函数**（把待测元素当唯一参数、其余写死）跑 `grad`，逐个取该元素的导数：`dL/da00` 实测 **11.0**，与纸面逐位一致；其余七个在此树上撞到**另一堵**既有墙（`transforms: split does not build this function (an operand of the node is not reachable from the half's exit)`），与 `matmul_grad` 无关，故只报 1/8 已测。

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

## 20 步轨迹：引擎 vs 复步参照

第一步梯度对只证明回抽对；**20 步都对**才证明更新、Adam 的两个平均、下一步的梯度串起来了。
stage2 与 stage4 各跑 20 步：

- **20 步的 loss 逐步对比：没有任何一步不一致**（两个 stage、各 20 个数，全部 12 位相同，
  已钉进用例 `grow.stage2_residual_ln` / `grow.stage4_full_model`，第 1/2/…/10、11…19、20 步全在）。
- **第 20 步的参数逐张量比**（用例 `grow.params_after_20_steps`，参照
  `grad_first_step.py --stage 2 --params 20`）：最大绝对差 **1.19e-10**，落在 `b1[0]`
  （引擎 `0.028394963497274293`、参照 `0.02839496337818086`）；多数张量的差 ≤ 3.3e-15，
  例如 `wq1[0][0]` 3.3e-15、`b2` 6e-17。
- **那 1e-10 不是接错了线，是第一步 `v = 0` 的直接后果**：第一步大部分参数的梯度恒为 0（结构原因见
  文首），Adam 的 `sqrt(v) + 1e-8` 于是把分母压到 `1e-8`，一个 1e-16 量级的梯度舍入差被放大成
  `lr * 1e-16/1e-8 ≈ 2e-11` 的更新差，20 步累积到 1e-10 量级。 loss 对它几乎不敏感，
  所以 20 步的 loss 仍然 12 位一致 —— 两边算的是同一件事，只是从不同的舍入位出发。

### 这条 1e-10 会不会随步数长大？——不会，它饱和（200 步实测）

「末位噪声被随机放大」会随步数增长（√N），「两边有系统性差异」会长得更快（N）。 实测两者都不是：

| 步数 | 参数最大绝对差（`b1`） | loss 差 |
| --- | --- | --- |
| 20 | 1.344e-10 | 5.0e-13 |
| 50 | 1.331e-10 | 2.9e-13 |
| 100 | 1.327e-10 | 7.7e-13 |
| 200 | 1.327e-10 | 1.5e-12 |

**它是饱和的，不随 N 增长。** 原因是那点放大只在 `v ≈ 0` 的头几步存在：一旦 `v` 长到
`O(grad^2)` 远大于 `eps^2`，`sqrt(v) + 1e-8` 就等于 `sqrt(v)`，1e-8 不再放大任何东西，
之后的差只剩梯度本身的 1e-16 量级、不再累积。 所以 1.33e-10 是**第一步建立起来的一次性偏移**，
不是每步都在长的误差。

**判据（写下来给下一个人）**：第一步 `v = 0` 让 Adam 的分母是 `1e-8`，末位噪声被放大成
**~2e-11 的更新差**；20 步 1.19e-10、200 步 1.33e-10 正是这个机制的正常量级，**不是接线错误**。
**验证方法**：加大步数看它长不长 —— **不随 N 增长 ⇒ 一次性偏移（正常）；随 N 增长 ⇒ 有系统性差异，才值得查。**

## stage5：给它一个标签之后（LM 头 + 交叉熵）

stage4 的 loss 里没有标签，所以"它预测对了吗"根本不是那个 loss 能回答的问题。 于是加了 stage5：
`rows → logits = rows @ head`，loss = `sum(log(sum(exp(z))) - z_label)`（每行一个目标类，两行 2×2，
用 `exp(z) @ ones` 取行和），标签取下一个 token、末尾绕回。 **旧 loss 的用例一字未动** —— 它证明的
是另一件事（那条 loss 的梯度对），现在仍在。

**① 前 20 步与参照 12 位一致** —— 新头走的是同一条梯度通路，这条硬证据在
`grow.stage5_lm_head` 里（20 行 loss 全钉）。

**② 但模型学到的是"打平"，不是"预测"。**

| | 第 0 步 | 第 200 步 |
| --- | --- | --- |
| logits | [[2.3123, -2.3123], [0.4914, -0.4914]] | [[0.0288, -0.0288], [-0.0288, 0.0288]] |
| argmax | [0, 0] | [0, 1] |
| 第 0 行目标类(1)概率 | 0.009712 | **0.485588** |
| 第 1 行目标类(0)概率 | 0.727659 | **0.485618** |

loss 单调从 **4.952361566340** 降到 **1.444728431190**，但**它的地板是"弃权"值**：两行各 log 2，
合计 **1.386294**。 训练把 logits 压到 ±0.03、概率 0.514/0.486 —— **它学会的是不再押注**。
argmax 训练后两行**仍然和目标相反**（第 0 行要 1 给了 0，第 1 行要 0 给了 1，差距 0.4856 vs 0.5144，
等于抛硬币）。

**原因（不是梯度错）**：躯干把两行的输出压成了几乎相等的坐标（`rows = [[1.590, 1.597], [1.509, 1.688]]`），
2×2 的线性头无法把两个位置分开，于是下降最省力的方向就是把 logits 收敛到 0。 这是两 token 玩具模型的
容量/结构问题，**不是接错线** —— 接线的正确性由 ① 那 20 步证明。

**这一条要记住的**：`loss 下降` 不等于 `学到`。 若 loss 的地板是 log(类别数)，那它学的是弃权；
判据是**把 target 类的概率与 argmax 分开看** —— 概率升了但 argmax 还是错，是"在学，但容量不够"；
概率降了，才是"目标配错了"。

## stage6：加一个位置嵌入，loss 就跨过了弃权线

stage5 停在 1.4447（弃权地板 2·log2 = **1.386294**），原因是躯干给两个位置的行几乎一样
（`rows = [[1.590, 1.597], [1.509, 1.688]]`），2×2 的线性头分不开它们。 试的最小改动：**在 `rows` 进头
之前加一个可学习的逐位置嵌入**（`pos`，2×2，初始 0），**其余一字不动**。

| | 第 0 步 | 第 200 步 |
| --- | --- | --- |
| logits | [[2.3123, -2.3123], [0.4914, -0.4914]] | [[-0.2472, 0.2472], [0.2510, -0.2510]] |
| argmax | [0, 0] ✗ | **[1, 0] ✓** |
| 目标类概率 | 0.009712 / 0.727659 | **0.621136 / 0.622910** |

- **loss：4.952361566340 → 0.949558421527**，200 步单调下降，**越过了 1.386294** —— 这是判据：
  **越过弃权线才叫"学到了一个不弃权的答案"，没越过就还是弃权，哪怕 loss 更低。**
- **argmax 训练后两行都对**（位置 0 要类 1、位置 1 要类 0）。
- **但置信度只到 0.62**（0.6211 / 0.6229），不是 0.9 以上 —— 这是"学对了方向、还没学狠"，
  属于学习率/步数的事，**没有顺手去调**。
- 前 20 步与 `grow.py --stage 6` 仍 **12 位一致**（新参数 `pos` 也同步进了参照），用例
  `grow.stage6_pos_emb` 20 行全钉。

**这一节串起来的三个数字**：1.386294（弃权） → 1.4447（stage5 没跨过去） → 0.9496（stage6 跨过去了）。
**一个自变量之差**（位置），不是宽度之差。

## stage7：加了一层注意力，反而更差 —— 而且差回了弃权线

stage7 = stage6 + 一层单头自注意力（`Q = x W_q`、`K = x W_k`、`S = Q Kᵀ/√2`、`A = softmax(S)`、
`O = A (x W_v)`），`x = second + pos`，`O` 再进头。 **全部用现成算子**（`@`、`transpose`、`softmax<1>`），
**没有新造任何一个**。 没有加 mask（这是被要求的形状）。

| | stage6（位置嵌入） | stage7（位置 + 注意力） |
| --- | --- | --- |
| 第 1 步 loss | 4.952361566340 | **2.300166876862** |
| 第 200 步 loss | **0.949558422** | **1.387116155841** |
| 弃权地板 | 1.386294 | 1.386294 |
| argmax（200 步后） | [1, 0] ✓ | **[0, 0] ✗** |
| 目标类概率 | 0.621136 / 0.622910 | 0.501034 / 0.499376 |

**结论：更差，而且差回了弃权线。** 1.387116155841 只比 2·log2 = 1.386294 高 0.0008 —— 也就是
"两行又不知道该押哪个了"，argmax 也回到 [0, 0]，概率 0.501/0.499 就是抛硬币。 **stage6 花了一个
自变量（位置）换来的东西，被这一层抹掉了。**

**诊断（不是接线错）**：前 20 步与 `grow.py --stage 7` **12 位一致**，所以注意力块走的是同一条回抽。
问题是**没有 mask**：两个位置互相看，`A` 的两行都接近 `[0.5, 0.5]`，`O` 的两行于是被平均成几乎一样的
向量 —— 正好把 stage6 用来区分两个位置的那一维抹平。 起点 loss 也因此低（2.30 对 4.95）：一开始
输出就已经"平均化"了，后面的下降只剩把 logits 压到 0 这一条路。

**下一步（没有做，等点名）**：给它一个因果 mask（模型里已经有 `mask()`），位置 0 只看自己、
位置 1 才能看到位置 0 —— 那时注意力才有可能学"把位置搬过去"，而不是"把位置抹掉"。

## stage8：因果 mask 生效了（`A` 第一行 [1.0, 0.0]），loss 越过弃权线，但没追上 stage6

只加一个 mask（`S = Q Kᵀ/√2 + mask()`，与模型里已有的两处注意力同一个 `-1000`），其余一字不动。

**先钉不依赖训练的数**（这是"mask 真的生效"的硬证据，用例 `grow.stage8_causal_attn` 已钉）：

| | 第 0 步 | 第 200 步 |
| --- | --- | --- |
| `A` 第 0 行 | **[1.0, 0.0]** | **[1.0, 0.0]** |
| `A` 第 1 行 | [0.5784, 0.4216] | [0.3270, 0.6730] |

位置 0 只看自己，位置 1 仍然混合 —— mask 按预期生效。

**再跑 200 步**：loss **2.600174800352 → 1.281279765502**，两行目标类概率 **0.536596 / 0.517488**，
argmax **[1, 0]（两行都对）**。

**三档判据里的位置**：

| 档 | 区间 | stage8 |
| --- | --- | --- |
| 优于 stage6 | < 0.949558422 | ✗ |
| 越过地板但没追上 stage6 | 0.949558422 ≤ loss < 1.386294 | **✓ 1.2813** |
| mask 也没救回来 | ≥ 1.386294 | ✗ |

**读数**：mask 把 stage7 抹掉的位置信号**拿回来了**（argmax 回到 [1, 0]、loss 越过 1.386294），
**但没有在 stage6 之上带来任何新东西** —— 1.2813 比 stage6 的 0.9496 差，置信度也更低
（0.5366 对 0.6211）。 换句话说：**mask 让它回到"能学"，没让它"学得更多"**。
前 20 步与 `grow.py --stage 8` **12 位一致**，所以这不是接线问题。 按判据这是读数，**没有调参、没有加步数**。

**三个 stage 并排**：stage6（位置）**0.9496** ← stage8（位置 + 因果注意力）**1.2813** ← stage7（位置 + 无 mask 注意力）**1.3871** ← 弃权地板 **1.386294**。

## 墙 13（新）：端口由体加、由调用点填，中间没有一处数个数（norm 侧与 with 侧）

- **判据**：成员构造时按"体需要的值"加端口，调用点仍按原形状填；**两侧的个数没有一处对齐校验**，于是
  norm 侧与 with 侧都以内部故障收场，而不是以一条语言诊断。
- **证据（norm 侧）**：`transpose_loop_escape.reads_outer` 读数 `carried=2 captures=2` 对调用点 `has 1 args`
  ⇒ 抛 `Runtime indirect call norm-arity mismatch`（运行期内部错误）。
- **证据（with 侧）**：`grad_of_a_division` 的 with-arity 读数（该族之外的第一条），结果不是诊断而是错值/内部错误。
- **现状**：两侧同根，**一堵墙两个面**；只改一面会留下另一面同样的故障。

## 墙 14（新，暂记）：`split` 拒绝"操作数不在半程出口可达集里"的函数

- **判据**：`split(jvp(f))` 对某些形状的 `f` 报 `transforms: split does not build this function (an operand of the node is not reachable from the half's exit)` —— 是**具名拒绝**，不是崩溃，也不是类型错。
- **原文（完整）**：`error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: split does not build this function (an operand of the node is not reachable from the half's exit).` 位置 `stdlib/vjp.cml#vjp` 的 `let (y, lin) = split(jvp(f))(..xs)`。
- **证据**：单个函数的四个"把待测元素当唯一参数、其余写死"的标量函数里，`fa00` 通过（导数 11.0 = 纸面 `dC·Bᵀ[0][0]`），`fa11` 在 `split` 这一步被拒；同一份程序、同一棵树。
- **现状**：同一个 `vjp` 组合，对同构但不同的求和形状一处通过一处被拒 —— **这是形状相关，不是类型相关**；根因未定（可能与"半程出口的可达集"如何收集有关），**判准与否待裁**，故按候选记录。