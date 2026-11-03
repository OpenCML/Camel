# 把 `mean_axis` 的切线写成一次缩放：墙换了一处，没有消失

**一句话**：照 s2wall 给 `relu` 的做法，把 `mean_axis` 的切线从"再走一遍 `mean_axis`（三操作数）"改成"乘一个由形状算出的标量"——**三操作数节点确实从线性半边消失了，但用例一条也没转绿**：裸 `transpose(f)` 转的是**值图**，那里 `mean_axis` 这个节点本身必须被转，而它的伴随需要 `xs`。所以 `skip=arity` 是症状，真正的墙是残差约定。

## 1. 尝试的写法（已回滚，未留任何语义改动）

`stdlib/rulesets/jvp.cml#jvp_mean_axis` 的右半边：

```cml
// 改之前
mean_axis(xs, axis, keepdims) => (dxs: T[..]) => mean_axis(dxs, axis, keepdims)

// 改之后（内联，不写 helper）
mean_axis(xs, axis, keepdims) =>
    (dxs: T[..]) => dxs * T(1.0 / float(shape(dxs)[axis]))
```

两者是同一个函数（均值对每个元素除以同一个数），但后者把三操作数节点换成了一个 `mul_tensor`——正是 `rulesets.transpose` 已有律（`mul_tensor_t`）的那一种。

## 2. 三行实测（`a17769388` + 本笔）

| 走法 | 结果 |
| --- | --- |
| 前向 `jvp`：`float(sum(mean_axis(x, 0, false)))`，`x = float[2,2]` | **过**，印 `5.0`（前向没被弄坏） |
| `vjp`（= `transpose(split(jvp(f)))`） | **仍拒，但换了理由**：`split does not build this function (an operand of the node is not reachable from the half's exit)`——不再是 `skip=arity uri=tensor:mean_axis` |
| 用例原样的 `transpose(loss_mean)`（裸转置） | **一点没变**：`TRANSPOSE function=loss_mean skip=arity uri=tensor:mean_axis operands=3` |

**helper 版本还多一条墙**：把缩放写进具名函数 `mean_tangent` 后，`vjp` 报 `TRANSPOSE function=rulesets.jvp::mean_tangent<float> skip=signature` 与 `skip=call-no-transpose node=8 callee=rulesets.jvp::mean_tangent<float>`——**线性半边的具名调用，走查转不了**。所以这种写法必须内联。

## 3. 形状读数（回答"先贴形状，别硬套"）

`mean_axis(x, 0, false)` 在 `x: float[2, 3]` 上：

| 量 | 值 |
| --- | --- |
| 输出 | `float[3]` = `[2.5, 3.5, 4.5]` |
| 车道长度（平均除的那个数） | `shape(xs)[0] = 2` |
| 缩放因子 | `1/2 = 0.5`，**是标量** ✓ |
| 正确余切 | `[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]` |

⇒ **`1/numel` 这条路在形状上成立**（`1/2` 是标量，广播进 `float[2,3]` 没有歧义），不是硬套。轴语义也没有问题：降到 `keepdims=false` 后输出秩仍是 1，但除法按轴长度，不按输出元素数。

## 4. 为什么还是不通：两种转置不是同一张图

- `vjp(f)` 走 `split(jvp(f))`，转的是 **jvp 的切线半边**——这个半边里现在只有 `mul_tensor`，所以 `mean_axis` 不再出现；改后它卡在 `split`（残差的闭包约定）。
- 用例里的 `transpose(loss_mean)` 是**裸转置**，它转的是 `loss_mean` 自己的**值图**，`mean_axis` 是其中一个节点，必须被逐节点转。它的伴随要 `xs` 的形状（严格说也要值），而 `mean_axis_t` 的右半边读 `xs`。

`modules/transforms/transpose.cpp` 在 arity 计数旁已经把这件事量过并写下来了：**把计数删掉，走查会建出一个取 `(x, ct)` 的图，而函数值的签名只承诺 `(ct,)`，调用死在 `nargs.size() == normPorts.size()` 上——那是信号，比这条拒绝更糟。**

## 5. 结论与不做的事

- **回滚**：前向切线的舍入会因此改变（`sum/n` vs `x*(1/n)`），却没有任何一条用例转绿，所以不留这处语义改动。
- **不许靠改断言**：断言仍是逐元素 `[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]`，本次没有动它。
- **真墙有归属**：裸 `transpose` 要带上"前向算出来的值"，就是作者开放问题 `docs/handover/questions/2026-11-03-who-carries-the-forward-halfs-intermediate-values.cn.md` 的那一条（残差怎么进闭包）。那是改约定，不是改律；由作者裁。
- 若日后约定落地，本页 §1 那个写法可以直接拿来用：三操作数节点已证明能从线性半边里去掉，`vjp` 会从 `arity` 前进到 `split`。