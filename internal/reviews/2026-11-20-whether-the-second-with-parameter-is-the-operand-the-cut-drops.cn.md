# 割说「一个节点的操作数不在它这一半的出口可达集合里」—— 掉的是第二个 `with` 参数，还是切线端口那道墙的下游？

**状态**：开放。2026-11-20 在 `fix/compositions-repin` 上量到，`4d7e1e9be`（基线）与 `2f90e9f03`
（重钉之后）两个树上同一句，`compositions.hessian_of_a_quadratic` 的四条（nvm / fvm / jit / prl）
逐字相同。

## 1. 当前拒法（逐字）

```
(split_of_jvp_of___g_364____first_with_as_param_forward):?:?: [Error]: An error has occurred:
transforms: split does not build this function (an operand of the node is not reachable from the
half's exit).
```

`stderr` 里那一行读数：

```
READING_REFUSAL {"transform":"split","function":"jvp_of_split_of_jvp_of_quadratic__first_with_as_param_forward","cause":"forward","construct":"a node whose input was not copied"}
```

`cause` 仍是 `forward`：**提升发生了**。这一句与「提升根本没发生」的
`jvp has no forward transform for this function` 不是同一道墙，与「值半读参数 0 的切向」那一条
（`construct="a tangent port"`）也不是同一句。变的是 `construct`。

## 2. 同一个计划、同一次运行里的三句（这是让它是问句的读数）

`-Plan test/plans/feat/transforms/compositions -Jobs 8` 在 `4d7e1e9be` 上跑出 `total 26, pass 6,
fail 14, known 6`，那 14 条红不是一句话：

| 用例 | `construct` | 打印的句子 |
|---|---|---|
| `nested_grad_min.*` / `nested_grad_through_a_binding.*`（8 条） | `a tangent port` | 值半读参数 0 的切向，而前向半不收它 |
| `hessian_of_a_quadratic.*`（4 条，本页） | `a node whose input was not copied` | 一个节点的操作数不在出口可达集合里 |
| `jvp_vjp_f.*`（2 条） | 无（不是拒绝） | `Runtime graph 'jvp_of___g_359__' breaks its edge invariants` |

**前两批用例的唯一结构差别是 `with` 表的长度**：`nested_grad_min` 割的是 `with <x: float>` 的
`square`，hessian 割的是 `with <a: float, b: float>` 的 `quadratic`
（`test/cases/transforms/compositions/hessian_of_a_quadratic.cml`）。`grad` 只对第一个 `with`
参数求导（设计原则 9.9），于是第二个参数在提升签名里占第二个值端口与第二个切向端口 —— 与
`internal/questions/2026-11-20-whether-the-value-half-takes-tangent-ports.cn.md` 第 2 节量到的
`exit=((float, float), (float, float) => (float, float) pure)` 同形。

## 3. 它为什么是一句问句

两个解释产生**同一句**打印，读数分不开：

1. **割丢了一个它该拷的操作数**：值半的体读第二个 `with` 参数的值端口，而 `split` 没有把那一路
   节点拷进这一半 —— 这是 `split` 的缺陷。
2. **那个操作数本来就属于切向半**：第二个参数在值半里只以切向身份出现，「不可达」是**正确**的，
   红的是用例的写法 —— 它把一步切向依赖放进了值半。

两条都自洽，代价只有作者能权衡，所以是问句，而**不是**「再钉一句期望」能收的事。

## 4. 三条路与代价

- **A（本页默认）先分离再钉**：写一个最小 `case`（`with <a: float, b: float>`、体内同时读 `a` 与
  `b`、只割一层 `first_with_as_param`），看掉的那个操作数是**值端口**还是**切向端口**。
  代价：一个 case 加一轮 plan（宿主约 2 分钟）。收益：把「第三道墙」与「切线端口那道墙的下游」
  分开 —— 而这决定了要不要动 `modules/transforms`。
- **B 直接钉成第三道墙**：把 hessian 的 `known_fail_msg` 钉成这一句。代价：钉住的是症状；若它是
  A 里第 2 种解释的下游，那道墙修好之后这句会消失，期望要改第二次，而中间这段时间这一行会被
  读成「一道独立的墙」。
- **C 当成 `split` 的缺陷直接修**：代价：本线不许碰 `modules/transforms/*`，而且现在没有任何读数
  说它是缺陷 —— C 只能排在 A 的结果之后。

## 5. 默认（写死，按它开工）

**A。** 先做分离测量，结果写回本页第 2 节下面（掉的是值端口还是切向端口），然后分岔：
值端口 ⇒ C 那一层（`split` 的拷贝），另立一件；切向端口 ⇒ 与
`internal/questions/2026-11-20-whether-the-value-half-takes-tangent-ports.cn.md` 合并成一件，
本页作废。

**在这之前 hessian 的 4 条保持红，`known_fail_msg` 不动。** 那一句是**量出来的实际打印**，不是我们
替它挑的期望；钉它要等 A 说清楚它属于哪一道墙。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

解释 2 不成立。原则 9.9："对 `f` 的第一个 `with` 参数（模型的权重）求导，其余 `with` 参数（如超参数）当作常量"。第二个 `with` 参数没有切向，在值半里必须可得；"不可达"是变换的缺陷。页里提的分离测量不用再做。
