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

## 6. 作者 15:15 已裁过一句同族的话，但它救不了本例

**裁定原文**（`docs/handover/reviews/questions-2026-10-29.md`，2026-11-03 15:15，随 `ffa360a96` 落在 `origin/fsh-dev`）：

> **带 `with` 的函数，pullback 带不带 `with` 列表——不带，闭包捕获**。……规则只有一条：**后一半要读的东西都是残差，进 `lin` 的闭包**，`with` 值也在内。……`split` 在切点把后一半读到的 `with` 端口当残差捕获。`lin` 不再带 `with` 列表，`transpose(lin)` 的类型就是 `(ct) => (..Tangent<ParamsOf<F>>)`。

**它为什么不能直接解决本例**：两条路的形状根本不同。

| | `vjp(f)` = `transpose(split(jvp(f)))` | 裸 `transpose(f)` |
| --- | --- | --- |
| 有没有切点 | **有**：`split` 在切点把两半分开 | **没有**：整张值图直接倒着走 |
| 残差放哪 | 有地方：**后半段图的捕获口**，长度可以当残差被闭包捕获（本页 §2 已证到 `split` 为止） | **没有地方**：`Transpose<F>` 的类型就是 `(ct) => (..Tangent<ParamsOf<F>>)`，一个实参进、一组切向出 |
| `mean_axis` 的长度从哪来 | 前向算、闭包带 | **推不出来**：`ct` 是 `float[3]`，轴长 `2` 不在里面；`shape(ct)` 给的是 `[3]`，不是 `[2]` |

所以 15:15 那条裁定是**关掉"带 `with` 列表"这个口子**、把残差统一进闭包；它成立的前提是**有切点**。裸 `transpose` 没有切点，残差无处可放，这不是同一条规则能覆盖的情形。

## 7. 要把问题问到点子上：一句话的可裁问句

**问：直接 `transpose(f)`（不经 `split`）时，律需要而 `ct` 里没有的前向值（本例是 `mean_axis` 沿轴的长度；轴语义下它不能从 `ct` 的形状反推），是否也应当允许 `Transpose<F>` 带一份残差列表？**

**默认建议：暂不。** 理由三条：

1. **`Transpose<F>` 保持 `(ct) => (..Tangent<ParamsOf<F>>)`**，`mean_axis` 记为已知红（XFAIL），等作者裁。
2. 一旦允许，**`Transpose<F>` 与 `Pullback<F>` 就变成同一个东西**——两者都要"切点 + 闭包 + 残差"，而作者 15:15 刚把 `Pullback<F>` 收成不带列表的那一行（`let (y, pb) = vjp(scale)<2.0>(3.0); pb(1.0)`，`with` 值只写一遍）。这等于把刚收起来的东西在另一头重新放开。
3. **代价要写清**：走这个默认，`mean_axis`（以及同族的 `reshape`、`softmax` 那三处角度位）的转置**在库层永远做不到裸 `transpose`**，只能经 `vjp`/`split` 那条路；要改就只有两条——**改律的写法**（像本页 §1 那样把三操作数算子换成一次缩放，让切线半边不含它）或**改类型**（给 `Transpose<F>` 加残差位）。两条都不是本车道能自主决定的。

## 8. 复核（lane-bcast，2026-11-04）：拒绝与轴无关，广播本身也不静默

问题：`stdlib/rulesets/transpose.cml#mean_axis_cotangent` 的 `ones_like(xs) * (ct / lanes)` 是不是"只对某些轴成立"？量它是**拒绝**、**错数**还是**崩**。

读数环境：`c69a110fa` 的干净构建（`Camel v0.2.43 (Build 261103_155934_c69a110fa)`），主机 `88zjwei` load 19–20/32。探针 `x = [[1,2,3],[4,5,6]]`（`float[2,3]`），`loss(x) = sum(mean_axis(x, axis, false) * w)`。

| 走法 | 读数 |
| --- | --- |
| 裸 `transpose(loss)(1.0)`，`axis=1`，`w=[10,20]` | `TRANSPOSE function=loss skip=arity uri=tensor:mean_axis operands=3` |
| 裸 `transpose(loss)(1.0)`，`axis=0`，`w=[10,20,30]` | **同一句**，逐字相同 |
| `vjp(loss)(x)`，`axis=1` | `TRANSPOSE function=split_of_jvp_of_loss_backward skip=arity uri=tensor:mean_axis operands=3`，报在 `stdlib/vjp.cml#vjp` 的 `transpose(lin)` 那一行 |
| `vjp(loss)(x)`，`axis=0` | **同一句** |

⇒ **属"拒绝"，且与轴无关**：两条路都在 `modules/transforms/transpose.cpp` 的 arity 计数处停，律的右半边一次也没执行。所以本项**不改代码、不动期望**。

**律的右半边自己也不是静默错数**——直接量它的算术：`ones_like(x) * (ct / 3.0)`，`ones_like` 是 `[2,3]`、`ct / 3.0` 是 `[2]`：

```
error[RuntimeDiag::TensorDimensionMismatch]: Array shape mismatch in 'multiply': cannot broadcast [2, 3] with [2].
```

Camel 的广播是右对齐（NumPy 口径），`[2,3]` 配 `[2]` 是 3 对 2，拒。**没有一条能给出 `[[1.6667,…],[3.3333,…]]` 那种错数的路**。

**精确的限制面（只关于轴，不关于数）**：

| `keepdims` | `axis` | 展开能不能成立 |
| --- | --- | --- |
| `false` | `0`（首轴） | 成立。秩 2 读 `[[5.0, 10.0, 15.0], …]`、秩 3 读 `[[[10.0, 20.0, …], …], …]`，与手算同 |
| `false` | `1` / `2` | **拒**。秩 2 运行期 `cannot broadcast [2, 3] with [2]`；秩 3 在静态就 `cannot broadcast extents 3 and 2`（`[2,3,4]` 配 `[2,4]` 与配 `[2,3]` 都是） |
| `true` | 任意 | 成立：`ct` 在被缩的轴上长度 1，右对齐直接展开 |

`keepdims=true` 那条读到的正是手算基准：`ones_like(x) * ([[10.0], [20.0]] as float[2,1] / 3.0)` =

```
[[3.3333333333333335, 3.3333333333333335, 3.3333333333333335], [6.666666666666667, 6.666666666666667, 6.666666666666667]]
```

**本页 §3 有一句要限定**：那里说"`1/numel` 这条路在形状上成立（`1/2` 是标量，广播进 `float[2,3]` 没有歧义）"——那只对 `axis=0` 成立。律现在写的是 `ct / lanes`（形状随 `ct`，不是标量），`axis=1`、`keepdims=false` 时它是 `[2]`，展不开。

**真正的缺口在这里（这一格的结论）**：`unsqueeze<1>(ct / lanes)` 得到 `[2,1]`，展开后与基准逐元素相同——但 `unsqueeze` 的轴只写在 `<>` 里（`unsqueeze<1>`），而律的 `axis` 是运行期端口，所以**律今天写不出"按运行期轴展开"这句话**。这比"广播不通"深一层：广播不是缺陷的理由，缺的是一个"轴本身是值"的展开。这一条记作既有限制，与残差的约定（§7 那条问句）同源；改律在墙倒下前既不能验也不该改。

**防误导：换写法换不掉展开。** 上面量的是 `c69a110fa` 的 `#mean_axis_cotangent`（`lanes = numel(xs) / numel(ct)`）。另一条车道已经在另一支上把这段改成一读原值的内联写法、读 `shape(xs)[axis]` 并删掉了 helper（那支还没合）。**若比例改成一读原值的写法，`lanes` 仍算车长、缺陷仍在展开那一步**——比例本来就算对了，缺口不在比例。

**为什么续在本页 §8、不新开一页**：新开一页会在 `docs/handover/questions/README.md` 的待裁表里多一行，而这一格不是问句（判定是拒绝，且拒绝与轴无关，律的右半边从未执行）。待裁表是作者的预算，不是垃圾桶：读数放读数的地方，问句才进 `questions/`。