# `Jvp<F>` 是「(value, tangent) 那对」，还是「一个提升后的闭包」

**问句（一句）**：外层 `jvp`（`transpose` 同理）遇到内层 `split` 交出的、**代码静态可读**的闭包时，`Jvp<F>` 该继续是「`(value, tangent)` 那对」，还是改成「**一个提升后的闭包**」（提升后的闭包自己背着值的一半与切向的一半）？

## 默认建议（写死：本轮按此开工）

**改成「一个提升后的闭包」**，并把 `transforms:transpose` 算子在 `jvp` 里的规则一并补上（`transpose` 一侧同样要认出提升后的闭包）。
理由：**函数没有切平面是法则级的，不是识别处的**。

> 这是 `stdlib/signatures/transforms.cml:13-27` 与设计原则 9.9 的裁定，不是识别处一改。

## 一、读数一：闭包的代码**今天就已经能读**

`split` 交出的线性一半 `lin` 正是那个 `FILL(FunctionClosure)` 值（静态源 = backward 图，捕获 = 残差）。把一个这样的值直接交给 `jvp`，它**成功**——2026-11-20 09:12 本车道实测，`std::nvm`，`--trace transforms`，逐字：

```
REINTERP jvp function=split_of_jvp_of_quartic_backward graph=344 liftedFrom=343 type=(float, float) => (float, float) pure nodes=7 kinds=7/2 exit=Tuple{chain,tangent} tangent=computed source=ruleset rules=-
REINTERP jvp built graph=344 nodes=17 exit=16 function=split_of_jvp_of_quartic_backward
REINTERP jvp function=autodiff::mul_tangent<float> graph=345 type=(float, float, float, float) => (float, float) pure nodes=7 kinds=7/2 exit=Tuple{chain,tangent} tangent=computed source=ruleset rules=jvp_mul,jvp_add
REINTERP jvp callees=1
probe_jvp_closure y=16.0 lin=32.0 pair=32.0 slope=32.0
```

跑的是这一段（临时程序，已撤）：

```camel
import rulesets.jvp
import { jvp, split } from transforms

func quartic(x: float): float { return x * x * x * x }

func main(): int sync {
    let f4 = split(jvp(quartic))
    let (y, lin) = f4(2.0)          // lin: the FILL(FunctionClosure) value
    let jl = jvp(lin)               // ← 这一句今天就能跑
    let (pair, slope) = jl(1.0, 1.0)
    ...
}
```

⇒ **⇒ 所以「`calleeIdOf` 不认 `FunctionClosure`」不是这堵墙**：`liftFuncValue` 只对 `Func` 节点跑（`modules/transforms/jvp.cpp` 的 `liftGraph`），**Fill 节点从不问 `calleeIdOf`**；闭包的值一旦被交给 `jvp`，它的 `Function` 记录就答出 backward 图，lift 照常进去，连内层反向图里的 `autodiff::mul_tangent` 都有了规则（`rules=jvp_mul,jvp_add`）。

## 二、读数二：真正的墙是**类型**

函数、以及含函数的元组**没有切平面**（`src/core/tangent_space.cpp#aggregateAnswer`、`answerOf`：成员没有空间 ⇒ 聚合没有空间；函数类型落进 `default` 分支，什么都不注册 ⇒ 无空间）。而 `split` 前向一半的答案正是 **`(float, (float) => float)`**：值，加上线性一半那个闭包。

`liftSignature` 的第一件事就是问出口类型有没有切平面，没有就答 `nullptr`（`modules/transforms/jvp.cpp#liftSignature`）。于是 `jvp` 对这样形状的函数**没有签名可用**，拒绝落在「被调者认不出」之前。

## 三、读数三：语言自己已经说了这句话（逐字）

本车道把 `split` 前向一半直接交给 `jvp` 写在一行里，编译器答的是：

```
error[SemanticDiag::ArgumentsRejected]: Invalid call to 'jvp' with argument types <> ((float) => (float, (float) => float pure) pure): `jvp` takes a function whose result has a tangent space, and this one's result is a `(float, (float) => float pure)`, which has none: the lifted signature answers the pair (value, tangent).
```

⇒ 这句话是**签名法则**在拒绝，不是寄存器、也不是识别处。

## 四、读数四：两处死点，各自死在哪儿

`--trace transforms` 再加本车道两个临时探针（已撤），`stdlib/vjp.cml` 的 `grad` 体（`let forward = split(jvp(first_with_as_param(f)))`）在两种写法上各死一处：

| 写法 | 死点 | 逐字 |
|---|---|---|
| `grad(dcube)`（具名函数，体内就是 `grad(cube)<x>()`） | **callee 的出口是闭包** ⇒ `liftSignature` 拒，**走 walk 之前就死**（外层 lift 一行 REINTERP 都没有） | `jvp signature=null function=vjp::grad<<x: float> () => float pure> exitType=<__pack_0_0: float> () => float` |
| `grad(grad(quartic))`（lambda 体，体内调捕获的 `forward`） | walk 停在被捕获 `forward` 的 CALL：答案类型 `(float, (float) => float)` | `jvp walk node=2 kind=8 type=(float, (float) => float)` |

后者的外层图（`first_with_as_param` 的复核，`graph=415`）节点依次是：`float` 常量、`(float) => (float, (float) => float)` 的捕获口、对它的一次 `CALL`（就是死点）、`.1`（取 `lin`）、`transforms:transpose`、又一次 `CALL`、`.0`。

⇒ **⇒ 即使类型这一问答完，还差第三件**：lift 在 `node=5` 会遇到 `transforms:transpose` 算子，`jvp` 里对它没有规则；`transpose` 一侧同样要认出「提升后的闭包」。这一件在 `rulesets.jvp` 里补（不是本次要动的识别处）。

## 五、三条路与代价

1. **`Jvp<F>` = 提升后的闭包（默认建议）**。改 `liftSignature` / `pairTypeOf` / `jvpOperatorSignature` / `Jvp<F>` 的声明，让「答案是闭包」的函数提升后交出闭包而不是一对；同时补 `transforms:transpose` 的 jvp 规则。代价：`Jvp<F>` 是公开类型（`stdlib/types.cml`），凡按「一对」读它的地方（`split`、`transpose`、`has_aux`、导出、goldens）都要一起迁移；好处：`grad(grad(f))` 第一次有**类型可写**。
2. **保持「那对」，改 `split` 前向一半的答案**：让 `lin` 不再是答案里的一个成员（静态源 + 残差作为值交出去，`transpose` 在调用处再拼）。代价：这是甲-1 的产物形态改动，且与作者 2026-11-18 13:40 的修订（「残差作显式参数」作废，回到 GNC 的闭包形状）相反。
3. **维持现状**：`grad(grad(f))` 保持拒绝，把 `jvp(jvp(f))`（前向一半，已绿：x=2 ⇒ `y=16.0 t0=32.0 t1=48.0`）与 `jvp(lin)`（读数一）进测试钉住。代价：二阶导不进 P\*，但墙有了名字、有了读数、有了负例。

## 六、这条车道量到的其余事实（供对照）

- `feat/transforms/nested`（2026-11-20 09:20，`-Jobs 8`，`std::nvm`）：`total: 8, pass: 2, fail: 0, known failures: 6`，`[TREE] clean`。
- `jvp(jvp(quartic))`（f = x⁴，x=2，种子 1）：`probe_jvp_jvp y=16.0 t0=32.0 t1=48.0` ⇒ `t1 = f''(2) = 48`、`t0 = f'(2) = 32`，两层没混淆。
- `calleeIdOf` 本身（`src/runtime/graph.cpp#GCGraph::calleeIdOf`）只认 `GCNodeKind::Func` 且有 payload 的节点；旁边的 `staticFunctionGraphOf` 认 DATA 静态函数，遇到 `FILL(FunctionClosure)` 按名拒绝（`reason="function-closure"`）。`transpose` 认被调者的那一处在 `modules/transforms/transpose.cpp` 的 `Call`/`Func` 分支：先问 `calleeIdOf`，再问 `staticFunctionGraphOf`，「carried」那条路只接受 `Port`。
- `split` 交出的闭包字段（`modules/transforms/split.cpp#buildLinearClosure`）：Norm 输入 0 = 静态源 `Function`（命名 backward 图 + 闭包 `TupleType`），`with` 输入 = 残差按槽位序，payload = `GCFillKind::FunctionClosure` 的槽表。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

选"提升后的闭包"。9.9 说"原本就成对的成员提升后成双出现"，函数成员不成对，而是被提升；核心抽象第 6 节"闭包的提升"给了做法。
