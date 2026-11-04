# `with` 值该不该有自己的切向？

**状态**：待裁；我方默认见第 5 节（**甲：给 `with` 值一个切向槽，且任何有切空间的类型都有一个零**）。

写这页的直接原因：派工要做的是 `2026-11-04-where-the-library-and-the-engine-still-differ.cn.md` 的第 3 项（张量形状的 `with` 参数），判据是"库层腿与引擎腿逐位同答"。**第 3 项的 split 那一半已经修好并且量到了逐位同答的数**（第 3 节，`32.0` 与 `[4.0, 5.0, 6.0]`），但同一条派工里要一起交代的第 2、4 项，加上报告里的第 1 项，量下来**是同一个问题**：**`with` 值没有切向**。这四项不是四个缺口，是一个决定的四个出口，所以它们要么一起答，要么一起不答——分开修会得到一个"值对、pullback 是 0"的错数，比拒绝更差。

## 1. 读数（实测，含树、时间、负载）

树 `/home/zjwei/camel-work/withport`，宿主 88zjwei（32 核），2026-11-04 21:15–21:25 UTC（宿主本地 05:15–05:25+08），1 m load 17.97/23.58/23.77。基线是**现场量的** `origin/develop` = `34b659107`（`git rev-parse --short HEAD` 在同一棵树里读的，不是记下来的 sha）。探针在宿主 `/tmp/wp-probes/`，程序文本见第 8 节。

三条形状，引擎腿与库层腿各一次：

| 形状 | 引擎腿（`grad` / `with_grad`） | 库层腿（`vjp`）改前 | 库层腿改后 |
| --- | --- | --- | --- |
| 2：`with <p: Point>`，`point_loss(y) = p.a*y + p.b` | `p02e grad={ a: 4.0, b: 1.0 } with_grad=(11.0, { a: 4.0, b: 1.0 })` exit 0 | `skip=body` → `jvp has no forward transform for this function.` exit 1 | 同左（未修） |
| 3：`with <w: f64[..]>`，`dot(x) = sum(w*x)` | `p03e grad=[4.0, 5.0, 6.0] with_grad=(32.0, [4.0, 5.0, 6.0])` exit 0 | `skip=forward ... the port is not a parameter of the graph` exit 1 | 见第 3 节（split 那一半已答，数字仍被第 5 节的问题挡住） |
| 4：`with <k: float>`，`scaled() = k*x*2` | `p04e first=6.0 second=4.0` exit 0 | `jvp has no forward transform for this function.` exit 1，**没有 `skip=` 行** | 同左（未修） |

手算（比较之前先算，不从输出抄）：

* 第 2 项：`p.a*y + p.b = 2*4 + 3 = 11`；`d/dp = (y, 1) = (4, 1)`。
* 第 3 项：`sum(w*x) = 1*4 + 2*5 + 3*6 = 32`；`d/dw = x = [4, 5, 6]`。
* 第 4 项：`k*x*2 = 2*3*2 = 12`；`d/dk = x*2 = 6`。

引擎腿的三条读数与这三行手算逐位相同；库层腿一条都答不出。

## 2. 第 3 项那句拒绝：从哪来，是哪一种端口，两半各自由什么组成

**句子从 `modules/transforms/split.cpp#Half::makeCopy` 来**（`Port` 那一支，`refuseAt(..., "a port", "the port is not a parameter of the graph, so neither half takes it")`；`Refusal` 的 `construct=a port` 也在那里拼的）。它由读这个端口的节点触发：`Half::copyHalf` 从出口那一侧往下走，走到一个身上没有生产者的端口就停在那里并拒绝。

**哪一种端口**：不是 `with` 的，是**闭包（捕获）端口**。被切的那个图是 `jvp_of___g_328__`，也就是 lambda `(ww: f64[..]) => dot<ww>(x)` 的升格图；里面 `x` 是 lambda 从定义处捕获的值，`jvp` 把它声明成闭包端口（`jvp.cpp#liftGraph` 的捕获那一支，`GCGraph::closureNodes()` 就是这张表），`dot` 的 `with` 参数 `w` 是**另一条路**：它是调用点的一个实参（`with <w>` 的槽）。两者在图里长得一样（都是一个 `Port`，`parameter.kind` 不同），这才是这件事难认的原因。

**为什么"不是参数"就让两半都不要它**：两半的端口表是**两张分开的表**，而捕获不在这两张里的任何一张：

| 半边 | 它取的端口 | 造它的地方 |
| --- | --- | --- |
| 值半（forward half） | 升格签名的**前一半**普通参数 = `valuePortsOf(calleeType)` 取 `normTypesCount()/2` 个 | `Half::halfInputsOf` 的 `answersAPairAt` 那一支，`values` 只从 `all[port]` 里取 |
| 值半 | 调用点的 `with` 实参 | `halfInputsOf` 末尾把 `withInputsOf(ref)` 接在 `values` 后面 |
| 切线半（backward half） | 升格签名的**后一半** = 切线端口；加上 `tangentArgumentsOf(call)`（一个"回答一对"的被调，它的切线读取取调用点的切线实参） | `halfInputsOf` 的 `tangent` 那一支 |
| 切线半 | 残差（`residualsOf` 数出来的那些），从 `lin` 的闭包里读 | `addResidualCaptures` / `Half::projectResidual` |

`portIndex` 只认得 `normPorts()`（普通参数，`parameterOfPort` 按对半切编号），`withPortOf` 只认得 `addWithPorts` 造的那些（作者 2026-11-03 15:15：**with 端口只由前半声明**）。捕获端口：不在 `normPorts`，不在 `withInputsOf`，所以 `portIndex.find` 和 `withPortOf.find` 都落空，`makeCopy` 走到最后那句拒绝——**前半拒绝它是因为前半没有为它造端口，后半拒绝它是因为它不是残差**，同一句话，两个不同的空缺。

**两半各自由什么组成、该怎么改**：`split` 的两半应当**各取自己那一半的端口**（值半取普通端口，切线半取切线端口），而一个值**不在两半任何一半的参数里**时，它由**调用点填**：`with` 值由调用点当实参填，闭包值由**函数值自己的闭包**填——`splitKernel`（`modules/transforms/split_operator.cpp`）造前半的函数值时用的是 `function->tupleType()`，把每一个闭包槽都照抄过去，所以"调用点照常填"这件事在算子那一层已经是事实。作者 2026-11-04 02:50 对闭包端口的裁定就是这句：**转发成成员自己的端口、调用点照常填**（同一句裁定在 `2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` 第 4 节里被引过，方向是 `transpose` 的 `forward`）。

**所以第 3 项是不是同一件事**：**是**。报告第 3 项那句拒绝就是这一句，被拒的端口就是捕获端口，改法就是上表补上第三张表。第 3 项**不是** `with` 端口的取法问题——`with` 端口的取法今天是对的（第 3 节第一条读数就是证据）。

## 3. 修了什么，量到什么

**改在 `modules/transforms/split.cpp`，一处机制、三个落点**（101 行加）：

1. `Half::capturePortOf`：源图里的捕获节点 → 这一半为它造的端口。
2. `addCapturePorts(Half&, const GCGraph*)`：按源图 `closureNodes()` 的顺序，为**每一个**捕获造一个端口并 `appendClosureNode`；源图**写明了**闭包类型（`GCGraph::closureType()`）时才 `setClosureType`，没写明就不写——一个"写在定义处"的 lambda 的闭包布局只在承载它的函数值里（`tupleType()`），`src/runtime/graph_types.cpp#validateRuntimeGraphTypes` 容忍空闭包类型。**只由前半调用**，理由与 `addWithPorts` 同：后半读到的东西是残差。
3. `residualsOf` 的走法多一条：切线侧读到的一个捕获，与 `with` 端口同等对待（`!behindCut && (isWithPort(ref) || isCaptured(ref))` → 进残差表）；后半的残差成员查表也接着查 `forward.capturePortOf`。

量到的数（同一棵树，宿主 21:0x–21:2x UTC，load 见第 1 节）：

| 程序 | 改前 | 改后 |
| --- | --- | --- |
| `vjp((ww: f64[3]) => sum(ww * x))(w)`，`w=[1,2,3]`，`x=[4,5,6]`（捕获张量） | `skip=forward construct=a port uri=- why=the port is not a parameter of the graph, so neither half takes it` → `split does not build this function`，exit 1 | `captured value=32.0 pullback=([4.0, 5.0, 6.0])`，exit 0 —— **与引擎腿逐位同答**（手算 32 / [4,5,6]） |
| 同上，捕获换成 `x2=[-1,2,0.5]` | 同上 | `captured value=4.5 pullback=([-1.0, 2.0, 0.5])`（手算 `-1+4+1.5 = 4.5`；同一组 `w`、不同捕获答不同的数组，说明读的是捕获而不是背下来的答案） |
| 同上，但**开区间**（`f64[..]`） | 同上 | `split` 过了（`forward=364 backward=365 residuals=3`），`transpose` 拒：`skip=cotangent-needs-a-value uri=tensor:multiply position=0 reason=the rule's right side reads a value the caller does not carry` → `a parameter holds no cotangent: nothing uses its value.`，exit 1 |
| 第 3 项的 `with` 实参拼法：`vjp((ww: f64[..]) => dot<ww>(x))(w)` | `skip=forward ...` exit 1 | `split` 两刀都过了（`residuals=4` / `residuals=1`），**前半单独跑答 `p03lf value=32.0`**；带 pullback 时崩在 `autodiff: a zero tangent array needs an array with a tangent.`（第 6 节第 1 条，与 `with`、闭包都无关的最小复现是 `vjp(const_of)(a)`） |
| 标量的 `with` 组合（`with <w: float, b: float>`，`vjp((ww) => loss_with<ww, b>(x, t))(w)`） | `skip=forward ...` exit 1 | `skip=backward ...` exit 1（同一个句子，换到后半；原因见第 4 节第 3 条，不是 `split` 的） |

开区间那一行是本条改动的**边界**，不是它的缺陷：`transpose` 侧的 `forward` 还不把闭包端口转发成成员自己的闭包端口，那一页已经裁过、也派了活（同一天的 `2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` 第 4 节）。所以这一条改动的收益是确定的（捕获拼法从拒绝变成逐位同答），代价是零：四个计划的红数一条没增（第 7 节）。

## 4. 第 1、2、4 项的墙，与第 3 项是不是同一处

**不是同一处，但同一个问题。**

1. **第 2 项**：墙在 `modules/transforms/jvp.cpp#liftGraph` 的 `with` 端口那一支（`withPortOf` 命中后那句：`tangentOf[ref] = zeroConstantFor(node->dataType())`，取不到再 `zeroOfValueAtRunTime`）。`zeroConstantFor` 只写三种类型的零：`Float64`、`Float32`、`Array`（`jvp.cpp#zeroConstantFor` 的 `switch`），`zeroOfValueAtRunTime` 只处理 `Array`；`Point` 是 `Struct`，两者都答不出，于是这个 `with` 值没有切向可交，整个 body 的升格放弃 → `REINTERP jvp function=point_loss skip=body` → `jvp has no forward transform for this function.`。
2. **第 4 项**：墙在 `modules/transforms/jvp.cpp#jvpSignature` 的 `if (params.empty()) return nullptr;`。`scaled()` **只有 `with` 参数、没有普通参数**，`params` 是空的，于是根本没有升格签名；调用它的那个 lambda 在 `liftGraph` 的 `Func` 那一支拿到 `calleeSignature == nullptr` 就 `return fail(refusal)`，**一个字都不打**——这就是第 4 项为什么没有 `skip=` 行（第 2 项有）。这一条同时说明：`with` 参数的**值**已经在签名里了（`withs`），**切向**不在（`jvp.cpp#jvpSignature` 里那段注释写得很清楚：`with` 参数只写一个值端口，一对不写）。
3. **第 1 项**（报告的）：库层 `vjp` 对一个 `with` 值答 `pullback=(0.0)`，值对。原因与上面两条同源：`with` 值没有切向槽，`lin` 对它**恒为常量**，所以余切落不到它身上。第 3 节的标量组合那次 `skip=backward`（第 3 节最后一行）是这条的**另一个**出口：本页之外的一条缺陷让后半先拒了（`jvp` 写一个调用的实参时按 `(值, 切向)` **逐个参数交错**，而签名是"先全部值、再全部切向"，`jvp.cpp#liftGraph` 的 `Func`/`Call` 两支；一个参数时两者相同，两个参数就错位，运行期自己的边不变量检查会拒这种调用：`passes a Point as ordinary argument 1 of 'jvp_of_point_loss_plain', which takes a float`）。**这一条不该单独修**：只把顺序改对，那次拉回会答 `0.0`（第 1 项的数），比拒绝更差。

四项的共同答案：**`with` 值要么有切向，要么这四个出口都不通。**

## 5. 两条路与我方默认

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：给 `with` 值一个切向槽**（默认） | `jvpSignature` 的 `withs` 里每个 `with` 参数写**两个**（值 + 切向，与普通参数同序：先值后切向），`liftGraph` 的 `with` 端口那一支把它当普通参数读；同时把 `zeroConstantFor` 补到"**任何有切空间的类型**都有零"（`Struct` 逐成员递归，与 `struct.cpp#__tangent_zeros_arr__` 同样的走法），第 2 项就不再靠"没有零"拒 | **签名变了**：所有 `Jvp<F>` 的 `with` 位置都多一个槽，`with` 调用的 arity 全变；`stdlib/types.cml` 的 `Jvp<F>` 要跟着写。这是**语义改动**，得作者裁（设计原则 9.9 那句"被捕获的值在正向模式里切向为零"要不要改） |
| 乙：保持零 | 库层不解第 1–4 项，继续拒绝（第 3 项的 `with` 实参拼法就是拒绝 + `split` 那一半今天的答案） | 什么都不变，但四个出口永久不通，而且"库层能求 `with` 参数的导"这件事**没有别的拼法**：`vjp` 只对普通参数求导，要把权重搬到普通参数上，捕获的那条路已经走通（第 3 节），`with` 实参这条路走不通 |

**我方默认是甲**，理由不是"引擎答得出来所以库层也该答得出来"（那是期望，不是理由），而是三条量出来的事实：

* 零的缺口是**实现**的缺口，不是判决：`zeroConstantFor` 的注释自己说"哪些类型有零是 `tangentSpaceOf` 的答案，不是写在这里的一张表"，而 `Struct` 有切空间（`struct.cpp` 的 `addTangent`/`zeroTangent` 都在），零却写不出来——这一条与 `with` 无关也该补。
* `with` 值的切向槽是**判过的**：`jvpSignature` 的那段注释写着"一个 `with` 参数一个值端口，一对不写"，但同一段上方的裁定（2026-11-03）说两半"先全部值、再全部切向"，两者合起来才让 `with` 值**在签名里根本没位置**——这不是某一次改动的选择，是两句话碰出来的空位。
* 第 3 项已经量到：**split 那一半答得出**（`32.0` / `[4.0, 5.0, 6.0]`），缺的只是"这个值能不能动"。

若裁乙，请同时裁一句：**库层 `vjp` 对 `with` 实参的拼法应当是明确的拒绝**（今天第 3 项那个拼法在 `split` 修好之后走到运行期的一个崩溃，见第 6 节第 1 条——那不是拒绝，是崩，按 `对 > 明确拒绝 > 错数 > 崩` 得先把它变成拒绝）。

## 6. 路上量到的两件别的事（不是本页的问题，但都挡在同一个路口）

1. **`transpose` 给"没被读到的数组参数"造零时用了错的算子，运行期崩。** 最小复现（无 `with`、无闭包、无捕获，`develop` 上就崩，`vjparity` 树上也崩）：

   ```
   func const_of(x: float[..]): float { return 1.0 }
   let (y, pb) = vjp(const_of)(a)      // a = reshape<[3]>([1.0, 2.0, 3.0])
   pb(1.0)
   ```

   读数：`TRANSPOSE function=split_of_jvp_of_const_of_backward graph=333 type=(float) => (float[..],) nodes=2`，然后 `autodiff: a zero tangent array needs an array with a tangent.` exit 1。位置在 `modules/transforms/transpose.cpp#buildTransposeMember` 造答案的那一段（`space.space->code() == TypeCode::Array` 那一支）与同一个函数里的 `zeroOf`：它把算子名写死成 `:tangent/zeros_arr`，而参数的类型是**模块自己的** `float[..]`（零算子是 `tensor:zeros_like`，`TangentSpace::zerosLikeUri`），`__tangent_zeros_arr__`（`src/builtin/operators/struct.cpp`）要求第一个实参的 `code()` 是 `Array`，模块类型不是 → 抛。改法是一处：**用 `space.space->zerosLikeUri` 而不是写死的名字**（`src/builtin/operators/struct.cpp#moduleZeroTangent` 早就是这么做的）。这是 `transpose.cpp` 的活，不是本车道的行，本车道没动。
2. **`jvp` 写一个调用的实参时顺序与它自己的签名不一致**（第 4 节第 3 条后半）。证据两处：`jvp.cpp#jvpSignature` 的注释与 `stdlib/types.cml` 的 `Jvp<F>` 都写"先全部值、再全部切向"，而 `jvp.cpp#liftGraph` 的 `Func`/`Call` 两支按 `(值, 切向)` 逐参数交错；运行期在 `p02l2`（结构体参数）上直接拒：`Runtime graph 'jvp_of___g_327__' breaks its edge invariants` / `passes a Point as ordinary argument 1 of 'jvp_of_point_loss_plain', which takes a float`。一个参数时两种写法相同，所以单参数的一整片用例都盖不住它。

## 7. 派工判据的读数

| 判据 | 读数（同一棵树，宿主 88zjwei，21:2x–21:3x UTC，load 17–32） |
| --- | --- |
| `feat/compile` 红数不增 | 基线 2243 total / 2230 pass / **2 fail** / 11 known；改后 **同**（两条红仍是 `transpose_loop_escape.two_values_of_two_kinds`、`.second_state_of_the_same_loop`，久墙） |
| `feat/compile/transforms` 红数不增 | 基线 105 / 98 / **2** / 5；改后 **同** |
| `feat/transforms` 红数不增 | 基线 29 / 21 / **0** / 8；改后 30 / 22 / **0** / 8（+1 是新用例） |
| `feat/transforms/split` 不退 | 基线 **19/19**；改后 **20/20**。派工里写的是"24/24"，**这个计划文件今天只有 14 个 `[[tests]]`、19 个执行项**（`nvm`/`fvm`/`jit`/`prl` 多后端），加上本用例是 14 个条目 / 20 个执行项 |
| 新用例先红后绿 | 用例 `test/cases/transforms/split/split_capture_of_a_tensor.cml` 在基线树上跑：`skip=forward construct=a port uri=- why=the port is not a parameter of the graph` → `split does not build this function`，exit 1；改后 `captured value=32.0 pullback=([4.0, 5.0, 6.0])` / `captured value=4.5 pullback=([-1.0, 2.0, 0.5])`，exit 0 |
| `git diff -w` 判重排 | `split.cpp` 101 加 3 减，三处落点都是新代码，没有重排；两个计划文件是新增条目与一处 `expect_output_contains` 的改写 |

## 要作者答的一句

**「`with` 值应不应该有自己的切向（以及任何有切空间的类型应有零）？我默认『甲：给』——`jvpSignature` 的 `with` 位置写两个（值 + 切向），`zeroConstantFor` 补到所有有切空间的类型（`Struct` 逐成员递归）。这一条不改，第 1、2、3、4 项就都只能停在拒绝上；而第 3 项的 `split` 那一半已经答出 `32.0` / `[4.0, 5.0, 6.0]`，缺的只是这个值能不能动。」** 若裁乙，请同时裁"库层对 `with` 实参的明确拒绝"应当写成哪一句——今天它在 `split` 修好之后是一个运行期崩溃，不是拒绝。
## 8. 附录：四条探针的全文（宿主 `/tmp/wp-probes/`）

第 3 项（`p03l.cml`），开区间那一行把它换成 `f64[3]` 就是第 3 节第一行那条：

```
import { vjp } from vjp
import { reshape, sum } from tensor

with <w: f64[..]>
func dot(x: f64[..]): float {
    return float(sum(w * x))
}

func main(): int sync {
    let w = reshape<[3]>([1.0, 2.0, 3.0])
    let x = reshape<[3]>([4.0, 5.0, 6.0])
    let (v, pb) = vjp((ww: f64[..]): float => dot<ww>(x))(w)
    println(`p03l value={} pullback={}`->format<v, pb(1.0)>)
    return 0
}
```

第 2 项（`p02l.cml`）：

```
import { vjp } from vjp

type Point = { a: float, b: float }

with <p: Point>
func point_loss(y: float): float {
    return p.a * y + p.b
}

func main(): int sync {
    let (v, pb) = vjp((pp: Point): float => point_loss<pp>(4.0))({ a: 2.0, b: 3.0 })
    println(`p02l value={} pullback={}`->format<v, pb(1.0)>)
    return 0
}
```

第 4 项（`p04l.cml`）：

```
import { vjp } from vjp

with <k: float, x: float>
func scaled(): float {
    return k * x * 2.0
}

func main(): int sync {
    let (v, pb) = vjp((kk: float): float => scaled<kk, 3.0>())(2.0)
    println(`p04l value={} pullback={}`->format<v, pb(1.0)>)
    return 0
}
```

第 6 节第 1 条（`p05.cml`）：

```
import { vjp } from vjp
import { reshape } from tensor

func const_of(x: float[..]): float {
    return 1.0
}

func main(): int sync {
    let a = reshape<[3]>([1.0, 2.0, 3.0])
    let (y, pb) = vjp(const_of)(a)
    println(`unread value={} pullback={}`->format<y, pb(1.0)>)
    return 0
}
```