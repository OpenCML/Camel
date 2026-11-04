# 秩 0 的余切被当成指针读：`mean_axis_stated_t` 的端口写着 `float[..]`

**状态**：已裁（2026-11-04 04:20，`docs/handover/reviews/questions-2026-10-29.md`「三、秩 0：只有一个转换点」）：**端口保持 `float[..]`／`T[..]` 照实写，转换由编译器在可赋值转换那一处做一次**——也就是第 4 节的**乙**方，而不是本页当时的默认**甲**（甲＝把端口写成元素）。落地读数：端口回到 `T[..]` 之后这条路径**不再崩**，8 条 stated 用例 `killed by a signal` 一条都没有；转换点是 `camel/core/type/composite/array.h#isRankZeroEdge` → `ArrayType::castSlotFrom` → `Array::createRankZero`，标量进入 `float[..]` 位置时 `numel` 答 `1`、`shape` 答 `[]`（2026-11-04 实测）。下面第 1–3 节保留当时的崩溃帧与读数；第 4 节的甲、乙两案留作记录，甲已被裁掉。

写这页的原因只有一个：**一条 8/8 稳定复现的 SIGSEGV**。它今天就能撞上，不是"暂不支持"，也不是某个用例的特殊形状——是"类型说是数组、槽里装的是 double"这一类不一致里**第一个会崩的**。

这件事是我这条车道**改出来的可见面**，不是我改坏的地方：在 `modules/tensor` 之前，`mean_axis` 的结果类型是 `T[..]`，秩 0 的值被 `support.h#wrap` 保持成**秩零数组**，谁读它都没事。我把类型改成诚实的 `float`（元素）之后，值还是那个值，**装法变了**——余切槽里从此是一个 double，而律的端口还写着数组。

## 1. 最小程序

```camel
import { vjp } from vjp
import { mean_axis } from tensor

func total(x: float[]): float {
    return mean_axis(x, 0, false)
}

func main(): int sync {
    let (value, pullback) = vjp(total)([1.0, 2.0, 3.0, 4.0] as float[])
    let d = pullback(1.0).0
    `value={} dL/dx={}`->format<value, d>->println
    return 0
}
```

十二行，没有模块依赖的巧合：`x: float[]`，唯一那根轴归约、不 keepdims ⇒ 结果是**那个元素**（`float`，设计原则 5.4"秩零数组就是它的元素"）⇒ `vjp` 的 `pullback` 收一个 `float` 余切 ⇒ 崩。

同一段程序写成 `return sum(mean_axis(x, 0, false))` 就是 `test/cases/compile/transforms/stated_mean_axis_gradient.cml` 原来的样子——**今天编不过**，因为 `sum` 要 `T[..]` 而均值给的是 `float`（那是另一条读数，见 `2026-11-04-axis-reductions-state-their-extents.cn.md`）。

## 2. `8/8` 是怎么数的，gdb 帧原文

树 `/home/zjwei/camel-work/statetypes`（`lane-statetypes`，二进制 `2026-11-03T15:52:32`），宿主 88zjwei，2026-11-04 04:5x，load 22.7。命令：

```bash
for i in 1 2 3 4 5 6 7 8; do
  out=$(camel <上面那段>.cml std::nvm 2>&1); echo "run $i rc=$? :: $(echo "$out" | head -2)"
done
```

八次的读数**逐字相同**：

```
run 1 rc=139 :: REINTERP jvp function=total graph=330 … rules=jvp_mean_axis
run 2 rc=139 :: …（同 run 1）
…
run 8 rc=139 :: …（同 run 1）
```

崩溃前的最后三行（`std::nvm` 的 REINTERP/TRANSPOSE 日志）钉住了是哪条律：

```
TRANSPOSE function=split_of_jvp_of_total_backward graph=346 type=(float) => (float[],) nodes=9 arms=0 rhs=mean_axis_stated_t
TRANSPOSE op signature=(f: (float) => float | (float, ...) => float) => (cotangent) => (cotangents..) resolved=(f: (float[]) => float pure) => (float) => (float[],) transposed=(float) => (float[],) graph=(float) => (float[],) same=yes
```

`gdb -batch -ex run -ex "bt 25"` 的帧：

```
Program received signal SIGSEGV, Segmentation fault.
#0  camel::tensor::ops::(anonymous namespace)::numelKernel(ArgsView&, ArgsView&, camel::core::context::Context&)
      from /home/zjwei/camel-work/statetypes/out/latest/stdlib/tensor.cmo
#1  NodeVMSchedPass::evalNativeOperator(...)          libcamel.so
#2  NodeVMSchedPass::call(...)                        libcamel.so
#3  NodeVMSchedPass::call(...)                        libcamel.so
#4  NodeVMSchedPass::call(...)                        libcamel.so
#6  NodeVMSchedPass::apply(...)                       libcamel.so
```

**链路**：`transpose.cml#mean_axis_stated_t` 把余切交给 `mean_axis_cotangent::<T>(ct: T[..], at)`；那具函数第一句是 `float(extent_product(extents)) / float(numel(ct))`。`numel` 的参数类型是 `T[..]`（数组），于是 `support.cpp#tensorArg` 走 `isTensorType` 分支，`args.get<Array *>(index)` 把槽里的 **double 位**当指针读，`->numel()` 立刻段错误。**不是空指针**（空指针会被那句 `if (!tensor) throwRuntimeFault` 拦住），是野指针——所以任何"加一句 null 检查"的修法都拦不住它。

## 3. 为什么 `sum_stated_t` 同情形不炸（这一节是本页最有用的一条）

同一个文件、同样"归约的余切是一个标量"的情形，`sum` 那条律**一直是对的**：

```camel
export rule sum_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {
    sum_stated(t, at) => (ct: T) => spread_over(ct, at)     // 端口是 T —— 元素
}

func spread_over::<T>(ct: T, at: int[]): T[..] when is_float::<T>() {
    return full(at, ct)                                      // 从标量铺成 at 的形状
}
```

对照崩的那条：

```camel
export rule mean_axis_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {
    mean_axis_stated(t, at) => (ct: float[..]) => mean_axis_cotangent(ct, at)   // 端口是 float[..] —— 数组
}

func mean_axis_cotangent::<T>(ct: T[..], at: int[]): T[..] when is_float::<T>() {
    let extents = at[2:len(at)]
    let lanes = float(extent_product(extents)) / float(numel(ct))               // 把余切当数组问元素个数
    return full(extents, 1.0) * (ct / lanes)
}
```

**差别只有一处，而且和"余切是不是标量"无关**：拿到余切之后，**先把它当元素用**（`sum`：`full` 填充，标量是 `full` 的合法输入），还是**先把它当数组用**（`mean`：`numel` + 除法）。

也就是说：**`sum_stated_t` 不是"运气好"，是端口写对了**；`mean_axis_stated_t` 的端口是这同一份文件里唯一的例外。顺带说一句算术：`lanes = extent_product(extents) / numel(ct)` 里 `numel(ct)` 数的是**结果**的元素个数，秩 ≥1 时它是对的（`float[3]` ⇒ 3），只有在秩 0 时 `numel` 要问一个标量要元素个数，那个问题本身没有意义（元素就是 1 个）。

**第二个同类端口（今天够不到，改一下就到）**：`take_stated_t` 也是 `(ct: T[..])`，`take_cotangent` 里 `zeros_of_shape(ct, …)` 与 `scatter_rows(base, …, ct, 0)` 都把余切当张量传下去。今天不炸**只因为** `tensor:take` 的声明仍是 `T[..]`（秩 0 的值被 `wrap` 保持成数组）；`take` 一旦也写精确形状，它会是第二个。

## 4. 我方默认

**甲：律的端口按元素写；`numel` 这一侧按类型判"有没有形状"，不按"有没有值"。** 两条一起做，因为它们是同一个问题的两面：

1. **端口侧**（`stdlib/rulesets/transpose.cml`，本车道 Forbid，要另一条车道）：`mean_axis_stated_t` 的 `(ct: float[..])` 改成元素（`ct: T` 或 `X`，与三行之外的 `sum_stated_t` 一致），需要"一个元素铺开"时在律体里显式 `full([1], ct)` 或 `reshape<[1]>(ct)`；`take_stated_t` 同一个改法。**这一步的判据很清楚：端口写的必须和算子实际能给出的秩一致**，而 `sum_stated_t` 已经把正确写法摆在同一份文件里了。
2. **读侧**（`modules/tensor`，本车道能动）：`numel`／`shape`／`dim` 这些"问形状"的算子，入口应当**按类型**判"这个值有没有形状"——类型是数组就按数组读（并且 `wrap` 一侧必须保证"类型是数组 ⇒ 槽里是数组"），类型是元素就回答 1／`[]`。今天的 `tensorArg` 是先按类型分支、再按值兜底，恰好把"类型说数组、值是 double"这一格留成了野指针；**这一格要么在 `wrap`/装箱处堵死（类型是数组就一定装箱），要么在 `numel` 入口按类型答 1**。我倾向**两处都做**：装箱处堵死是根因，入口按类型答 1 是让任何漏网的组合都不会崩。

**不取的第三条**：把 `mean_axis` 的结果类型退回 `T[..]`。那是拿"不知道"换"不崩"，而这里知道——秩 0 就是那个元素。

## 5. 附：六个"现在会写形状"的算子，端口秩 vs 结果秩（逐算子自查）

这张表就是本页存在的理由：`numelKernel` 那次崩只是**表里第一条能崩的**，其余五条今天没有答错，是因为它们**根本没有反向律**。

| 算子 | 反向律（`stdlib/rulesets/transpose.cml`） | 余切端口 | 现在能写出的结果秩 | 今天的行为 |
| --- | --- | --- | --- | --- |
| `tensor:mean_axis` | `mean_axis_stated_t` | `(ct: float[..])` **数组** | 秩 ≥1 → 数组（秩 2 操作数）；**秩 0 → 元素**（秩 1 操作数、keepdims=false） | **SIGSEGV，8/8** |
| `tensor:sum_axis` | **无**（`sum` 整体走 `sum_stated_t`；`sum_axis` 没有律） | — | 同上 | 按名字拒绝：`skip=rule uri=tensor:sum_axis reason=unbound`，**没有答错** |
| `tensor:max_axis` | **无** | — | 同上 | 拒绝，同上 |
| `tensor:min_axis` | **无** | — | 同上 | 拒绝，同上 |
| `tensor:argmax_axis` | **无** | — | 同上 | 拒绝，同上 |
| `tensor:argmin_axis` | **无** | — | 同上 | 拒绝，同上 |
| （整体）`tensor:sum` | `sum_stated_t` | `(ct: T)` **元素** | 永远秩 0 → 元素 | **恰好对，而且是对的写法**：`full` 从标量铺 |
| （整体）`tensor:mean` | 无 | — | 永远秩 0 → 元素 | 拒绝 |

**"静默答错"今天为零**，这是量出来的：六个算子里只有一个有反向律，就是崩的那个；其余五个在 `TRANSPOSE` 里按名字拒绝（`asked=1 found=0`），不回答问题。**静默风险在第二个 `T[..]` 端口（`take_stated_t`）**，它今天够不到，够到了就和 `mean` 一样崩。

## 6. 我这一轮怎么处置的（**是绕开，不是修好**）

`test/cases/compile/transforms/stated_mean_axis_gradient.cml` 里那句 `total` 现在写成 `sum(mean_axis(x, 0, true))`——**保留那根轴**，于是余切是 `float[1]`（数组）而不是标量，律的端口不再被冒充，读数一位不变（`value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]`）。用例自己的注释、`stated_extents.plan.toml` 的 `desc`、以及本页都写明**这是绕开**：

> **谁把那个 `keepdims` 去掉，谁就会崩。**

我没有把类型退回 `T[..]`，也没有把这条洞藏进"暂不支持"。

## 要作者答的一句

**「秩 0 的轴归约答元素之后，`mean_axis_stated_t` 的 `(ct: float[..])` 端口把一个 double 交给 `numel`，8/8 稳定 SIGSEGV 在 `numelKernel`（同文件的 `sum_stated_t` 写的是 `(ct: T)`，不崩）。端口按元素写、读侧按类型判，这两条要不要一起落？我默认『都要，且端口这一条与 `sum_stated_t` 对齐』，并把 `take_stated_t` 一起改。」**