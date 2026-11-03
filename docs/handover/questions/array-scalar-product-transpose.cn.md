# 数组×标量那条路：律已就位、形状已按类型拒绝，剩下的是"律的答案类型"这一问

`tensor:multiply` 的转置律已经按作者 2026-11-03 10:30 的裁定写成**一条**（`stdlib/rulesets/transpose.cml`，提交 `5b4f2e1a9`）：

```camel
mul_tensor(x, w) => (ct: T[..]) => (sum_to(ct * w, x), sum_to(ct * x, w))
```

`sum_to` 是引擎里已有的"按广播求和回原形状"（`modules/tensor/ops/vjp.cpp#accumulateOperand`），这一笔给了它标准库里的名字（`stdlib/tensor.cml`）。数组×数组那一半是绿的（`transpose_array_product` 2/2，`d/dxs=(2,3,4)`、`d/dw=(1,2,3)`）。

数组×标量那一半今天**拒绝**（不是错数），理由和读数在下面。剩下的那一问要作者裁，不急在这一笔。

## 一、为什么一笔就够，且为什么必须拒绝这个形状

一条律覆盖两种形状是**实测的**，不是推理：`T[..]` 含秩 0，秩 0 就是那个元素（设计原则 5.4），所以 `sum_to` 的 `like` 可以是一个数，那时它答的就是总和——`sum_to([1.0, 2.0, 3.0], 2.0)` 读 `6.0`，`sum_to(g, g)` 读 `g` 原样。所以"按广播求和回原形状"这句话在律里写成 `sum_to` 就同时是两种形状，不需要"一个键下两条律"、也不需要按类型派发。

但**律被挑中的那一刻没有问操作数类型**：索引只按左式那个算子与它的元数归档（`rule_head.cpp`），`tensor:multiply` 是被两个数组和"一个数组一个数"共用的，而索引里那条律是给两个数组写的。它的右式于是在 `T = float` 上展开，`T[..]` 读作 `float[..]`——一个**接受**数字的类型（5.4，`ArrayType::assignableFrom`）——于是展开成功，并给出一个律从没为之写过的形状的余切：实测 `sum(x * s)` 在 `s` 上的余切读 **`0.0`**，且没有 `skip=`、没有 `contribution=0`。这正是设计原则 9.9 说不能给的那一种读数。

所以这一笔加的是**挑律时的那一问**：`modules/transforms/transpose.cpp#ruleTakesOperands`，照 `jvp.cpp#portsTakeOperands` 的写法与措辞——律的形参表是不是这个节点的形参表？不是就不选它，走既有拒绝路径，并把两边的类型都写出来：

```
TRANSPOSE function=split_of_jvp_of_a_prod_backward skip=rule-shape uri=tensor:multiply node=6 \
  operands=2 reason=the rule takes (float[..], float[..]), and the operator has (float[..], float)
error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: transpose has no transpose for
  this function: the rule the index filed for `tensor:multiply` was written for another shape: the
  rule takes (float[..], float[..]), and the operator has (float[..], float) (node 6).
```
那一句里三件东西都在：**哪个节点**（`node 6`，与上面日志同一编号）、**哪个算子**（`` `tensor:multiply` ``）、**什么不合**（律的形参表对实参表）。它没有退化成"没有律"——那是 `Rule` 的句子（`the rule table has no rule for a step of this body`），描述的是查了一次什么都没查到；这里查到了一条律，而律自己说这一步不是它要的形状，所以是新的 `TransposeRefusalCause::RuleShape`（日志里的词是 `rule-shape`）。三件事实记在 `TransposeRefusal` 上与句子读的是同一份记录，不靠人再解析日志。

只有**走查会真去要余切的位置**参与这一问：常量与无切空间的值本来就不问律（`isConstant`、`hasTangentSpace`），所以 `sum(x * 2.0)` 照旧对——`value = 12.0`、`d/dx = 2.0 2.0 2.0`、`rhs=sum_t,add_tensor_t,mul_tensor_t`。校验只加在 transpose 这一处，索引的判重规则一个字没动。

那一句"`T[..]` 的形参不收非数组实参"单独有名字：`sameOperandShape(written, operand)`（同文件，注释引设计原则 5.4 而**不写行号**）。分开写是因为它判的是另一件事——`assignableFrom` 判"这个口收不收这个值"，它判"这是不是那条律写的那种形状"；5.4 说秩 0 的数组**就是**那个元素，所以两个方向都真而形状不同。它是这一笔唯一一处会拒掉原本能过的程序的地方，所以它有自己的名字、自己的注释、和**两个方向各自的用例**：负的 `(float[..], float)` 上面两条，正的 `float[3]` × `float[3]` 是 `transpose_product_fixed_shape.matches_an_open_rank`（开秩仍收固定长度，`value = 20.0`、`d/dxs = 2 3 4`、`d/dw = 1 2 3`）。

## 二、四条读数（实测，同一棵树）

探针：`f(x: float[..], s: float) = sum(x * s)`，`x = (1,2,3)`、`s = 4.0`；`pullback(1.0)`。

| 律的右式，或调用形状 | 读数 |
|---|---|
| `sum_to(ct * x, w)`，**不做类型校验** | `d/dx = 4 4 4`（对）、**`d/ds = 0.0`**，无任何诊断 |
| 同上，换成 `sum(ct * x)`（`float` 型的答案） | `d/ds = 6.0`（对） |
| 同上，`sum_to(ct * x, 0.0)`（字面量 `like`） | 拒绝：`reason=the rule's right side uses a node written after it` |
| **本笔之后**：`sum(x * s)` | 拒绝，句子见上（带 `node 6`、`` `tensor:multiply` ``、"another shape"），`(float[..], float)` |
| **本笔之后**：`sum(s * x)`（操作数顺序翻过来） | 同样拒绝，`(float, float[..])`——**不再崩** |
| **本笔之后**：`sum(x * 2.0)`（常量标量） | 照旧对：`12.0`、`d/dx = 2 2 2` |
| **本笔之后**：`float[3]` × `float[3]`（具体形状，`transpose_product_fixed_shape`） | 照旧对：`20.0`、`d/dxs = 2 3 4`、`d/dw = 1 2 3` |

倒数第二行原来是一个 `PassInternalError`（`norm-arity mismatch: caller graph 'main' node 23 has 1 args, target graph ... has 2 norm ports`），拒绝把它关掉了。崩比红需要名字，而"拒绝"已经够了，所以那条崩溃形状**没有**进用例库。

## 三、`0.0` 不在端口上（临时读数，已撤，未提交）

给 `transpose.cpp` 加 dump 之后，`split_of_jvp_of_a_prod_backward` 的读数是：

```
params=2 ports=2 partial=0 firstPort=0
param=0 type=float[..] participates=1 portOfParameter=0
param=1 type=float     participates=1 portOfParameter=1
closure=3 carries=1 [0:float[..]] [1:float] [2:float]
node=0 kind=1 float[..] hasCt=1        node=1 kind=1 float     hasCt=0   <- 闭包里没人读的那个
node=2 kind=1 float     hasCt=1        node=3 kind=1 float[..] hasCt=1
node=4 kind=1 float     hasCt=1        <- ds 端口，有余切
node=5 kind=11 float[..] normInputs=0 4   <- x * ds
node=6 kind=11 float[..] normInputs=3 2   <- dx * s
node=7 kind=11 float[..] normInputs=6 5    node=8 kind=11 float normInputs=7
DBGPROBE answer at=0 portNode=3 part=9  type=float[..]
DBGPROBE answer at=1 portNode=4 part=15 type=float
```

`ds` 的端口**有余切**，答案也**没走** `transpose.cpp` 那条"没到手就答零"的分支。所以 `0.0` 是律的答案本身在 `float` 位上读出来的，不是端口丢的、也不是律没被调用（`rhs=sum_t,add_tensor_t,mul_tensor_t` 三处都在）。

## 四、根因：一条律的答案类型对所有位置都是 `T[..]`

`transposeSignature` 让一条律的答案类型对所有位置都是 `T[..]`。`T = float` 时那是 `float[..]`——5.4 让它**接受**一个 float，可值真就是数组，进 `float` 成员位读成 `0.0`；而 `sum(ct * x)`（类型 `float`，槽也 `float`）给 `6.0`。我试过把声明改成 `sum_to::<T, S>(t: T[..], like: S): S` 让类型跟着 `like` 走，**仍是 `0.0`**：成员位的期望类型是 `T[..]`，推断把 `S` 钉回 `float[..]`。所以修法不在 `sum_to` 的声明。

## 五、要作者裁的一句

**一条转置律的答案类型，该不该按每个参数的切空间分别给**（`(Tangent<P0>, Tangent<P1>, ...)`），而不是今天所有位置一个 `T[..]`？

裁"该"：`sum_to` 的返回值在标量位上就是 `float`，数组×标量那一半当场算对（`d/dx=(4,4,4)`、`d/ds=6.0`），并且"同一个键下多条形参不同的律"这件事也一起解锁——那条已经由作者 11:10 裁过（判重按参数类型是否相同，不按 `when` 的文本），实现点就在这同一处。

裁"不该"：`sum_to` 这种"回到某个形状"的归约就只能用在数组位上，数组×标量那一半要换别的写法——今天没有。在那之前，这一笔的状态是"该拒的仍然拒、该对的仍然对"。