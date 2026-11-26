# 三条库转置律：每次把值递过去，墙就往前挪一格

本页接 `internal/reports/2026-11-25-the-three-transpose-laws-no-walk-reaches.cn.md`（下称"上一页"）。
上一页证明的是**结果**：没有一条走法能走到 `softmax_t`、`layer_norm_t`、`conv2d_t`。本页记的是**过程**：
把"让法条读到前向值"这件事真做了一遍，每一次读数落在哪堵墙上，以及还差什么。

**先给判据的答案：一条也没走通，六处 marker 一处都没删，`vjp-parity` 的计数没有变成零。**
下面每一行都是本轮亲手跑的读数，不是对照上一页抄的。

## 1. 四条读数：`internal/_tcprobe/probe_walls.plan.toml`（不入 git，§6 抄了内容）

同一份探针程序，同一份计划，`--trace transforms`，backend `nvm`，四个 case 全部 `expect_exit = 0`：

```
TRANSPOSE function=rulesets.jvp::softmax_jvp<float> skip=parameter-shape uri=tensor:multiply node=9 position=1 param=1 param-type=float[...]
TRANSPOSE function=split_of_jvp_of_softmax_total_backward skip=call-no-transpose node=19 callee=rulesets.jvp::softmax_jvp<float>
TRANSPOSE function=rulesets.jvp::layer_norm_tangent<float> skip=parameter-shape uri=tensor:add node=8 position=0 param=3 param-type=float[...]
TRANSPOSE function=split_of_jvp_of_layer_norm_total_backward skip=call-no-transpose node=19 callee=rulesets.jvp::layer_norm_tangent<float>
TRANSPOSE function=split_of_jvp_of_conv_total_backward skip=law-reads-a-value uri=nn:conv2d position=0 operand-types=f32[1, 2, 4, 4],f32[...],f32[...] reason=the rule's right side reads a value the caller does not carry
REINTERP jvp function=conv_total skip=body
READING_REFUSAL {"transform":"jvp","function":"conv_total","cause":"body"}
```

`[SUMMARY] total: 4, pass: 0, fail: 4`；`[camel-remote] transpose-carry exit 1`（run `89136-20261126140736`）。
第四条（`conv2d` 对 `w` 求导）是我探针写错了 —— 程序的 `x` 是捕获值，`jvp` 就停在 `skip=body`，
与这三条律无关；`conv2d_t` 的读数以第三条为准。

**和上一页的差别。** 上一页把 `softmax`/`layer_norm` 记成 `skip=call-no-transpose`，本页把那一层拆开了：
`call-no-transpose` 只是继承来的说法，真正停住的地方在**被调用函数自己的身体里**，
`skip=parameter-shape uri=tensor:multiply` / `uri=tensor:add`，`param-type=float[...]`。
也就是说：停住的不是"call 没有转置"，而是 `softmax_jvp` / `layer_norm_tangent` 的形参类型**不带 extent**，
于是 `mul_stated`/`add_stated`/`sum_axis_stated` 那一类"要一个形状"的律，在 helper 里一个都答不出来。
helper 的形参是 `T[...]`，这不是笔误：泛型形参写不出 extent。

`conv2d` 那条是本轮唯一新测的细节：`operand-types=f32[1, 2, 4, 4],f32[...],f32[...]` ——
被回答的那个位置（`position=0`，`x`）类型**是**带 extent 的，所以 `shape(x)` 这一路是通的；
不通的是**值**：`conv2d_input_grad(x, w, ct)` 读 `x` 的值，而 `x` 在反向图里是切线，谁的闭包里都没有它。

## 2. 把 `jvp_softmax` 写成立地：两次读数，两堵墙

### 2.1 立式全式（`mul_stated` / `subtract_stated` / `sum_axis_stated`）

`softmax_jvp` 是一个 call，call 在线性半里没有转置；照 `jvp_relu` 的写法把切线写进律的右侧
（`shape(x)` 用的是调用者的 extent）：

```
REINTERP split function=jvp_of_softmax_total skip=backward construct=a node whose input was not copied uri=- why=an operand of the node is not reachable from the half's exit
READING_REFUSAL {"transform":"split","function":"jvp_of_softmax_total","cause":"backward","construct":"a node whose input was not copied"}
```

`[camel-remote] transpose-carry exit 1`（run `5796-20261126141635`）。
`uri=-`：卡住的节点没有 uri，不是 `tensor:*` 算子；`split` 的
`split_internal.h` 把它记成"半区里有个节点的操作数既没被抄、也没有跨过切线"
（`crossesCut` 里没有它，`writeConstant` 也写不出来）。lifted 图的节点数从 24 涨到 37。

### 2.2 最小复写（只留 `mul_stated(dx, softmax<axis>(x), shape(x), shape(x))`）

为了分辨"复写算子自己"是不是元凶，把右侧缩到只剩一次复写：

```
(probe_softmax)...probe_softmax.cml:5:23: [Error]: An error has occurred: transforms: jvp has no forward rule for `tensor:softmax` (the rule's right side uses a node written after it).
READING_REFUSAL {"transform":"jvp","function":"softmax_total","cause":"rule","uri":"tensor:softmax"}
```

`[camel-remote] transpose-carry exit 1`（run `65820-20261126143206`）。

**这条是新的、也是硬的**：前向（`jvp`）展开时，律的右侧**不能再写一次被展开的那个节点**。
展开发生在 lifted 图里，那个 `softmax(x)` 已经在那里了，右侧写它就是"用一个写在它后面的节点"，
`jvp` 按名字拒绝。反过来，**转置**侧写 `softmax<axis>(x)` 是合法的 —— 反向图里根本还没有这个节点，
这就是 `softmax_t` 的右侧敢那么写的原因（也说明那条右侧从来没有被展开过：
它唯一的展开机会是被走到，而它从来没被走到）。

## 3. 于是每条律现在缺什么（逐条，按机制，不按名字）

| 律 | 走它需要什么 | 今天缺的那一件 | 缺的那件在哪 |
| --- | --- | --- | --- |
| `softmax_t` | 反向图里有一个 `softmax` 节点，其操作数是**值**且拿到余切 | 前向半只能写"call"，而那 call 的身体里形参不带 extent，`mul_stated` 一个都答不出来 | `lib/rulesets/jvp.cml#softmax_jvp`（形参 `T[...]`）；`transpose/walk.cpp#forward` 的 `parameter-shape` 分支 |
| `layer_norm_t` | 同上 | 它的线性半里是 `layer_norm_grad(x, g, dx, 0)`，一个**没有转置**的算子；而 `tensor:add` 又先因 extent 停住 | `lib/rulesets/jvp.cml#jvp_layer_norm`；`lib/tensor`（`layer_norm_grad` 只有正/反向，无转置律） |
| `conv2d_t` | 被回答位置的操作数**值**（`x`/`w`/`b`），或它的 extent | 值在反向图里是切线，闭包里没有；extent 只在 `position=0` 有（`f32[1, 2, 4, 4]`） | `transpose/walk.cpp#knownValue` → `forward`；`lib/nn`（`conv2d_input_grad` 等三个梯度算子读的是张量而不是 extent） |

三条其实是**同一件东西**的三个面：设计原则 9.9 的"值端口"只做了一半 ——
调用点那一半（`state.calleeNeeds` → `transposeSignature` 的 capture 口 → 调用点按 `calleeNeeds` 传值，
`walk.cpp:3690`）是现成的，**发现那一半没有**：`forward` 在"参与回答的参数被读值"时只记 `shapeWanted`，
不记"这个成员要一个值口"（`walk.cpp:772-834`，注释自己说 "`needs` is therefore empty"）。补上它，
`softmax_jvp` 这类 helper 才有可能被转置；但如 §2.1 所示，补上之后 `mul_stated` 仍然会因 helper 的
`T[...]` 形参停在 `parameter-shape` —— 除非 extent 也作为**参数**递进去（`softmax_jvp(x, dx, axis, shape(x))`），
那又是同一件事的另一处：int[] 形参也要一个值口。

而 `conv2d_t` 与 9.9 无关：它要的是**被回答位置**的值，那个位置在 `transpose(f)` 里永远是切线，
调用者手上没有。这条只能靠"梯度算子也接受 extent"（`conv2d_input_grad(input_extents, kernel, dy)` 这一类）
绕过去 —— 那三个算子的核**只读形状，不读那个操作数的值**（`tensor/kernels/conv.cpp:264/295/323`：
`conv2dInputGradIn` 只取 `input->shapeSpan()`，`conv2dKernelGradIn` 只取 `kernel->shapeSpan()`，
`conv2dBiasGradIn` 连 `bias` 都不收），所以这条是**可以做成**的，代价是 `nn` 多三个 stated 孪生算子
（declare + OpDef + 核），以及 `conv2d_t` 的右侧改成 `conv2d_input_grad_stated(shape(x), w, ct)` 这一种写法。

## 4. 六处 marker 为什么一处也没删

上一页 §6 的判据是三条：`rhs=` 里有这条律、程序里有它的贡献、计数为它减一。
本轮四条探针的 `[SUMMARY]` 全是 `pass: 0`，`rhs=` 一行都没出现（连 `softmax_t` 都没被问过），
所以三条判据一条也不成立。按派工"一条一条删、每条附证据"的要求，一处也不该删。

## 5. `vjp-parity` 的计数：为什么"零"现在还说不出来

- **before（逐字）**：
  `PASS the two derivative tables differ only where the list says so: 179 law(s) filed under 74 key(s), 122 engine registration(s) under 104 key(s), 55 key(s) both answer (each half), 8 forward-only, 0 reverse-only, 5 with a law that is neither half, 6 library-only and 36 engine-only exempt, 0 new, stale=0`
- **after（逐字）**：与 before 完全相同 —— 本轮没有改这张表，也没有改那六处 marker。

要让"零"由机器说出来，值必须来自**走过**，而"走过"在仓库里只有一个记录：某份计划里钉住的
`rhs=` 行。我把这条读数先量了一次（`test/plans/**/*.toml` 里所有 `rhs=` 与
`src/stdlib/rulesets/transpose.cml` 的 61 条反向律取交集）：

```
laws=61 pinned distinct=17
walked(pinned)=13 notPinned=48
```

也就是说：**按计划钉住的 `rhs=` 数，61 条反向律里只有 13 条有"走过"的书面记录**，
而上一页据以立论的只有三条。剩下 48 条并不是都没走过，而是**没有一条 witness 把它们和走法绑在一起** ——
这才是"六处 marker 是唯一机器可读记录"的真正原因。把计数做实要两件事，都不在本轮：
1. 每条律一个 witness（`律名 → 程序`），2. `vjp-parity --walk` 用 `--trace transforms` 跑 witness 并读 `rhs=`。
第 2 件是 `scripts/vjp-parity.mjs` 加一个模式和一张表；第 1 件是 48 条律的检索工作。
**先做 1 再做 2**：没有 witness 表，脚本只能数我手写的字，等于把六处 marker 换成六行 TSV。

## 6. 探针（不入 git，抄在这里以便重跑）

`internal/_tcprobe/probe_walls.plan.toml`（四个 `kind = "behavior"`、`args = ["--trace", "transforms", "${case}", "${backend}"]`、
`backends = ["nvm"]`、`expect_exit = 0`），程序：

```cml
// probe_softmax.cml
import { vjp } from vjp
import { softmax, sum } from tensor
func softmax_total(x: float[3]) => float {
    return sum(softmax<0>(x) * ([1.0, 2.0, 3.0] as float[3]))
}
func main() => int sync {
    let (value, pullback) = vjp(softmax_total)([1.0, 2.0, 3.0] as float[3])
    `probe softmax value={} answer={}`->format<value, pullback(1.0).0>->println
    return 0
}
```

`probe_layer_norm.cml` 与 `probe_conv2d_x.cml` 同形（`layer_norm_total` 用 `layer_norm(x, 1.0, 0.0)` 的
三参拼写，`conv_total` 用 `conv2d(image(), kernel(), bias())`）；`probe_conv2d_w.cml` 写坏了（见 §1 末），
重跑前要改成"把 `w` 声明成形参"的那种写法。

## 7. 本页没有做的事

- 没有改任何源码进 tree：两处 `jvp.cml` 的实验（立式全式、最小复写）都读到了数，读完就 `git reset` 掉了。
  理由：立式全式比现状**更早**失败（`split` 停在 `backward`），留在树上就是把一条读数换成一条更差的读数。
- 没有删 marker，没有改 `vjp-parity`，没有加 witness 表（§5 说明为什么这两件事要一起做才有意义）。
- `max_pool2d`/`avg_pool2d`/`embedding`/`sum_to` 四条律本轮没有再测；上一页的读数是 `law-reads-a-value`，
  本页 §3 的机制同样覆盖它们（都读被回答位置的值）。