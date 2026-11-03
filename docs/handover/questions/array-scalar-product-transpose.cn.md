# 数组×标量那条路：律对了、端口对了，答案在一个 float 位上读成 0.0

`tensor:multiply` 的转置律已经按作者 2026-11-03 10:30 的裁定写成一条（`stdlib/rulesets/transpose.cml`，提交 `5b4f2e1a9`）：

```camel
mul_tensor(x, w) => (ct: T[..]) => (sum_to(ct * w, x), sum_to(ct * x, w))
```

`sum_to` 是引擎里已有的"按广播求和回原形状"（`modules/tensor/ops/vjp.cpp#accumulateOperand`），这一笔给了它标准库里的名字（`stdlib/tensor.cml`）。数组×数组那一半是绿的（`transpose_array_product` 2/2，`d/dxs=(2,3,4)`、`d/dw=(1,2,3)`）。**数组×标量那一半仍然给出 `d/ds = 0.0`**，这一页是它的四条读数，和它卡在哪一处。

## 一、四条读数（都是实测，同一棵树，`5b4f2e1a9` 上改律的右式重跑）

探针：`f(x: float[..], s: float) = sum(x * s)`，`x = (1,2,3)`、`s = 4.0`；`pullback(1.0)`。

| 律的右式第二项 | `d/ds` | 诊断 |
|---|---|---|
| `sum_to(ct * x, w)` | **0.0** | 无 `skip=`、无 `contribution=0`，静默 |
| `sum(ct * x)` | **6.0**（对） | 无 |
| `sum_to(ct * x, 0.0)`（字面量） | 拒绝 | `skip=cotangent-needs-a-value uri=tensor:multiply position=1 reason=the rule's right side uses a node written after it` |
| `f = sum(s * x)`（操作数顺序翻过来） | 崩 | `PassInternalError: norm-arity mismatch: caller graph 'main' node 23 has 1 args, target graph 'transpose_of_split_of_jvp_of_b_prod_backward' has 2 norm ports` |

`d/dx` 在四种情形里都是 `4.0 4.0 4.0`（对）。

## 二、`0.0` 不在端口上（这一条排除了我先前的猜测）

给 `transpose.cpp` 加临时读数（已撤，未提交）后，`split_of_jvp_of_a_prod_backward` 的 dump：

```
params=2 ports=2 partial=0 firstPort=0
param=0 type=float[..] participates=1 portOfParameter=0
param=1 type=float     participates=1 portOfParameter=1
closure=3 carries=1 [0:float[..]] [1:float] [2:float]
node=0 kind=1 type=float[..] hasCt=1
node=1 kind=1 type=float     hasCt=0     <- 闭包里没人读的那个 float
node=2 kind=1 type=float     hasCt=1     <- 体里读的 s
node=3 kind=1 type=float[..] hasCt=1     <- 体里读的 x
node=4 kind=1 type=float     hasCt=1     <- ds 端口
node=5 kind=11 type=float[..] normInputs=0 4     <- x * ds
node=6 kind=11 type=float[..] normInputs=3 2     <- dx * s
node=7 kind=11 type=float[..] normInputs=6 5
node=8 kind=11 type=float     normInputs=7
DBGPROBE answer at=0 portNode=3 part=9  type=float[..]
DBGPROBE answer at=1 portNode=4 part=15 type=float
```

`ds` 的端口（node 4）**有余切**，答案也**没有走** `transpose.cpp:2898` 那条"没到手就答零"的分支：`at=1` 走的是 `2890` 的 `hasCotangent` 分支，`part=15` 就是律右式的那个值，类型记成 `float`。所以 `0.0` 是**那个值本身在 float 位上读出来是 0.0**，不是端口丢的、也不是律没被调用（`rhs=sum_t,add_tensor_t,mul_tensor_t` 三处都在）。

## 三、卡在哪一处：律的答案类型 `T[..]` 与成员位的 `float`

一条转置律的答案类型对所有位置都是 `T[..]`（`transposeSignature`：`(ct: T[..]) => (T[..], T[..])`）。`T = float` 时那个类型的字面是 `float[..]`——按设计原则 5.4（"秩 0 就是元素本身"）它**接受**一个 float，但反过来，一个 `float[..]` 型的值放进 `float` 的成员位，是数组进了 float 槽。实测这个转换的读数是 `0.0`，而 `sum(ct * x)`（类型 `float`，槽也是 `float`）给 `6.0`。

我试过让类型跟着 `like` 走——`sum_to::<T, S>(t: T[..], like: S): S`——读数仍是 `0.0`：成员位的期望类型是 `T[..]`，推断把 `S` 钉成了 `float[..]`，所以声明改了、期望类型没改，什么都没动。

两条可能的修法都不在我这一笔的范围里：

1. **律的答案类型按参数的切空间走**（每个成员一个 `Tangent<Param_i>`，而不是一个 `T[..]` 通吃）。这样 `(sum_to(ct*w, x), sum_to(ct*x, w))` 的第二个成员就是 `float`，`sum_to` 的返回值也就是 `float`，`0.0` 不会发生；同一处改动也顺带让"一个键下两条形参不同的律"不再需要派发——**这正是作者裁过、今天卡在实现上的那件事**。
2. **数组进 float 槽要拒绝**（设计原则 9.9：跑通却给出错值不允许）。今天它是静默的 `0.0`，和 lane-mult 上量到的 `6.5897671638891e-310`（未初始化 double）是同一类：错数不是拒绝。

**同一形状的加法不会错，因为它根本走不到**：`g(x: float[..], s: float) = sum(x + s)`（同一 `s = 4.0`）的读数是**拒绝**，不是错数——

```
error[RuntimeDiag::RuntimeError]: ... transforms: jvp has no forward rule for `tensor:add` (the operator has no forward rule bound at all).
```

所以"数组 + 数字"今天停在 jvp 那一层，`add_tensor_t` 的右式再怎么写都到不了它；这一笔的错数只属于 `tensor:multiply`——**因为它有 jvp 律，却没有一条对方是数字的转置律**。

另外两条副产物，各自是一个名字而不是一个猜测：

- 律里写**字面量** `sum_to(ct * x, 0.0)` 被拒，理由 `the rule's right side uses a node written after it`——字面量被当作"写在它后面"的节点。这条拒绝是对的（拒绝优于错数），但理由是错的：常量不可能是写在后面的节点。
- 操作数顺序翻过来（`sum(s * x)`）时转置出来的图有 **2 个 norm 端口**，而调用点只传 1 个，直接 `PassInternalError`。这一条**不是新症状**：同一棵树上 `feat/compile/transforms` 的红用例 `transpose_loop_escape.two_values_of_two_kinds` 崩的就是同一句读数，逐字相同而只有图名不同——
  `Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports`。
  同一处映射（`portOfParameter` 与闭包 port）的两个症状，与 `lane-resid` 正在做的 `cutLoopGraph` 的 `invariants` 相邻，但这一条发生在 `split` 之后，不在 `split` 里。

## 四、要作者裁的一句

**一条转置律的答案类型，该不该按每个参数的切空间分别给**（`(Tangent<P0>, Tangent<P1>, ...)`），而不是今天这样所有位置一个 `T[..]`？裁"该"，方案 1 就能一次做完两件事：数组×标量算对，同键下多条形参不同的律不再需要派发。裁"不该"，那么 `sum_to` 这种"回到某个形状"的归约在律里就只能用在数组位上，数组×标量那一半得靠别的写法（今天没有）。