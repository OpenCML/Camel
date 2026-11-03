# 被微分的 lambda 以「调用带 `with` 的函数」收尾时，`split` 该剪什么？

**优先级：这条墙正挡住作者"下一批"第 3 条**（lambda 写 `with` 包（13:50）、库层 `grad`/`with_grad` 写成组合、S2 改用库层 `vjp` 训练、三方对照）。前半已做完（`10d4bc768`：包能解析、能转发、`vjp` 能打到带 `with` 的函数，6.0/2.0、9.75/11.0、5.0/7.0 三组手算数）。后半——库层 `with_grad` 用组合写出来——就停在这里，数字（0.125 / -0.75，即 `6e6cf3ff9` 的引擎读数）拿不到。

## 一、现象与原文

最小程序见第二节。编译输出逐字（`transforms.with_grad_composition_meets_the_split_wall` 的 `stdout`/`stderr`，宿主 `20261103_092127_158406`）：

```
(vjp)/…/out/latest/stdlib/vjp.cml:85:24: [Error]: An error has occurred: transforms: split does not
build this function (a lifted function answers with the pair (result, tangent); this graph's exit is
not a two-member tuple fill, so there is no pair to cut). …
(name=RuntimeDiag::RuntimeError, code=0x04000000)
```

`stderr` 里 `jvp`/`split` 自己的两行：

```
REINTERP jvp function=__g_243__ graph=252 liftedFrom=243 type=(float, float) => (float, float)
  nodes=6 kinds=6/2 exit=Tuple{chain,tangent} tangent=computed source=ruleset rules=-
REINTERP jvp built graph=252 nodes=10 exit=7 regions=1 function=__g_243__
REINTERP jvp callees=0
REINTERP jvp op signature=(f: (..) => any) => (the parameters and their tangents) => (value, tangent)
  resolved=(f: (ww: float) => float) => (float, float) => (float, float)
  lifted=(float, float) => (float, float) graph=(float, float) => (float, float)
REINTERP split function=jvp_of___g_243__ skip=exit (a lifted function answers with the pair
  (result, tangent); this graph's exit is not a two-member tuple fill, so there is no pair to cut)
```

**读数**：错的不是类型，是**形状**。`exit=Tuple{chain,tangent}` 这个字段是 `jvp` 每行都打的固定标签，不是读数（第一版页里我把它当证据用了，这是错的，见第六节）；真正的证据是 `skip=exit` 加上一条临时探测行读到的：出口结点是**应用结点** `kind=10`（`GCNodeKind::Func`），两个 `with` 输入、两个普通输入，它抬升后的副本类型是 `(float, float)`（探测行测完已删，读数就只有这三个数；探针文本没进仓库）。`__g_243__` 就是被微分的那个 lambda `(ww: float) => loss_with<ww, b>(x, t)`：它的体以一次「带 `with` 实参的调用」收尾，抬升后这个对是**那次调用的作答**，不是本图里一个把两半拼起来的 `Fill`。

这落到 `modules/transforms/split.cpp#exitShapeOf` 的第三个格：它只认 `GCNodeKind::Fill` + `GCFillKind::Tuple` + `withInputsOf(exit).size() == 2`，其余都归 `ExitShape::NotAPair`；而 `NotAPair` 那一格的注释写的是**"Defensive, and unreachable from `.cml`"**。这条例子**从 `.cml` 走到了它**——那句注释是假的，这也是这条墙第一次被一个真实程序钉住（此前只有 `showcase_s2` 的 `tensor_vjp_stops_at_split` 钉着 `split` 的别的形状）。

> **已修（2026-11-03 晚，`modules/transforms/split.cpp#exitShapeOf`）**：注释已按事实改——写明它可以从 `.cml` 到达，路径是"被微分的 lambda 体以调用带 `with` 的函数收尾"，并指向本页；报文也改成指名墙的那句（"…here the pair is a call's result … not the two-member tuple fill this cut takes its two halves from … This shape is not accepted"），不再有 `Defensive` 或"内部状态"的暗示。**行为未动**：不做 B、不做 A，等本页的裁定。

## 二、最小程序（就是库层 `with_grad` 的组合，不精简）

`test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml` 全文即是；要害三行：

```
with <w: float, b: float>
func step(x: float, t: float): (float, float) {
    let (value, pullback) = vjp((ww: float) => loss_with<ww, b>(x, t))(w)
    return (value, pullback(1.0).0)
}
```

要读的数：`step<0.5, 0.75>(1.5, 2.0)` = `(0.125, -0.75)`（`loss = 0.5 * (x*w + b - t)^2`，`d/dw = (pred - t) * x`；`library_grad_is_the_composition.cml` 钉的正是同一组数）。模型本身是设计 9.9 的写法：`vjp` 只微分普通形参，所以把权重用 lambda 挪到一个普通形参上——这一步必然要在 lambda 体里**调用带 `with` 的 `loss_with`**，绕不开。换成单 `with` 参数、单普通参数的函数同样如此。

## 三、三条路与代价

**A. 让 `split` 认"出口是一个调用的对"**（把该调用当成两元 fill 的等价物，进 `exitShapeOf` 的判据）。代价：`split` 的判据会多一种可剪的形状，而这一种与前三种不同——`Fill` 的两半是**本图里各自有结点的值**，一个调用的对不是：两半要靠投影或复制拿到各自那一半，前向调用会在两半里各跑一次（或者后向半边拿到的切线不是本图的线性函数，而是被调函数的内部）。`split` 认得越多，它的判据越难说清，这条尤其难：它等于承认"切线可以来自一个我看不见内部的结点"。

**B. 抬升图**强制**以 tuple fill 收尾**（在 lowering 里对"对来自调用作答"的出口插一次水合：把两半取出来、写一个两元的 tuple `Fill`）。代价：每个这样收尾的抬升图多一个 fill（和两条投影，若调用结果可投影）；好处是 `split` 仍然只有一种可剪的形状，那条"出口必须是本图的两元 fill"的判据一个字不用改，`NotAPair` 那格也就仍然是真"不可达"。**我建议这条**：出口形状属于 lowering 的事（谁造出这个对，谁把它写成 `split` 认得的形状），而 `split` 的判据属于语义不变量的事，能不动就不动。

**C. 库层绕开**（把 lambda 写成先 `let` 出值、再返回 tuple）。代价：**没试过**，而且我判断它多半不解决——`let v = …; return (v)` 里的 `v` 仍是同一个结点的引用，出口还是那次调用的作答；真要让 C 成立得让体在调用之后再做一次本图内的 tuple 组装，那就已经是在库层手搓 B 的效果，还把一次变换的形状要求写进了每一个库函数。C 只算绕。

## 四、默认建议

**B 优先，A 次之，C 只是绕。** 理由一句：出口形状是 lowering 的产物，`split` 的判据是语义不变量——在产物上补，`split` 的判据不动；在判据上补，`split` 就多认一种"切线来自看不见内部的结点"的形状，这条口子以后难收。

若作者判 A 更对，请把"`split` 会因此多认一种可剪形状、且该形状的两半要复制调用"这句代价一并记在 `split` 的判据旁边。

## 五、这一轮的处理

库层 `with_grad` **没有**进 `stdlib/vjp.cml`（写了会在任何调用点报上面那段报文）；组合写在上面那个用例里，作为**负向用例**钉住 `skip=exit` 与那句 reason。出口能剪的那天，把 `stdlib/vjp.cml` 末尾（`grad` 之后）补上同一个体，把这条用例改成正向、钉 `(0.125, -0.75)`，用例注释与 plan 条目里的 desc 都已写明这一步。

## 六、B 试过了：第一堵墙让开了，后面还有第二堵

按本节默认（B）改了一版：抬升时若出口结点的副本**就是**那个对、且结点是应用结点（`Call` / `Func`），就用 `valueOf`/`tangentOf` 已经算好的两个投影**水合**成一个两元 tuple `Fill` 当出口——`split` 的判据一个字没动。结果：

- **第一堵墙确实让开了**：`skip=exit` 消失，抬升图从 `nodes=9 exit=6` 变成 `nodes=11 exit=10`（多出的正是那个 `Fill`）。
- **紧跟其后是第二堵墙**，在 `split` 的**前向走**里：

```
REINTERP split function=jvp_of___g_242__ skip=forward construct=a port uri=- why=the port is not a
parameter of the graph, so neither half takes it
(vjp)/…/stdlib/vjp.cml:85:24: [Error]: An error has occurred: transforms: split does not build this
function (the port is not a parameter of the graph, so neither half takes it). …
```

判断（未证到根，但形状对得上）：这个 lambda 的**抬升类型没有 `with` 列表**（`(float, float) => (float, float)`），而它的图仍然通过 **port** 读捕获进来的 `x`/`t`/`b`；`split` 的 `addWithPorts` 按声明里的 `with` 个数列半边自己的端口，于是那些 port 既不在 `withPortOf` 也不在 `portIndex` 里，前向走就拒了。也就是说：**B 是必要的，但不充分**——B 之后还要回答"被抬升的闭包读的那几个 port 算什么"（算参数？算残差？这是 `split` 判据侧的问题，比 B 更接近 A 的性质）。

- 已**回滚**（树与 `7432f65cc` 相同，`git status` 空）。读数：`transforms` 87 = 84/2/1 复原；那一版里除了本页对应的负向用例因报文变化而红，**84 条通行用例一条没红**（`grep '[FAIL]'` 只有两条已知红 + 本用例）。

所以本页的排期变成两件：**B（水合出口，本页默认）→ 闭包 port 归类**。两件都不做，库层 `with_grad` 出不了数。
## 七、库层 `with_grad` 今天不存在，第 3 项是从零写起（2026-11-03 23:5x，`lane-withgrad`）

**前提事实，先记下来**：库层 `with_grad` 现在**没有**。`3a2e8fc90` 删掉了那一行导出——它声明 `F`、实际返回 `Vjp<F>`、也从不对方程里的 `with` 参数求值——`stdlib/vjp.cml` 只剩 `vjp` 与 `grad`。所以 `grad_first_with_parameter` 的两组用例（`.macro.nvm`、`.runtime.nvm`）都 `import { grad, with_grad } from autodiff`，走的是**引擎**那一份（`stdlib/signatures/autodiff.cml` 声明、`modules/autodiff` 实现），两组读数相同、无 skip 报文：

```
grad_first_with_parameter gx=3.0 vg=(9.0, 3.0) gy=3.0 gl=3.0
grad_first_with_parameter pg={ a: 4.0, b: 1.0 } pvg=(11.0, { a: 4.0, b: 1.0 })
grad_first_with_parameter wg=[4.0, 5.0, 6.0] wvg=(32.0, [4.0, 5.0, 6.0])
grad_first_with_parameter sk=6.0 sx=4.0
```

**第三行原本两组用例都没断言**（只断到 `gx/vg`、`pg/pvg`、`sk/sx`），这正是 15:15 说的"判据分辨不了对错"：一个错的律也能印出前三行那些数。现已按裁定补齐，四行全断言、`wg` 逐元素。

**三步走到哪**：

1. **B（出口水合）已落**：`modules/transforms/jvp.cpp` 里"出口的副本就是那个对、且那个对是应用结点作答"时，把 `valueOf`/`tangentOf` 这两个已经算好的投影写成一个两元 tuple `Fill`。读数：那堵 `skip=exit` 不再出现，抬升图 `nodes=9 exit=6` → `nodes=11 exit=10`（多出的正是 `Fill`），与本页第六节试出来的数一致。
2. **捕获声明成闭包端口：本页第六节说的"第二件"的一半其实已经在树里**。`jvp.cpp` 早有一段把 `function.closureNodesOf()` 的每个捕获做成 port 并 `appendClosureNode`、再 `setClosureType`（`skip=body` 那一版就是它修的）。缺口不在"声明"，在 **`split` 认不认这些 port**。
3. **第 ③ 步（`split` 把后一半读到的闭包端口当残差）未做**，`split.cpp` 归另一支。四个形状在这一步之后停下的原文：

```
# 无捕获的 float 形状（vjp((ww) => weighted<ww>(2.0))(3.0)）
REINTERP split function=jvp_of___g_247__ skip=backward construct=a parameter uri=- why=the backward half reads the value of parameter 0, and it is not one of the values the cut carries
# 捕获了数组的 tensor 形状（vjp((ww) => dot<ww>(x))(w)）
REINTERP split function=jvp_of___g_248__ skip=forward construct=a port uri=- why=the port is not a parameter of the graph, so neither half takes it
```

两个形状各停在一处，**都不是出口形状**了，也都不在库层能改的范围：一处是后一半读参数原值没进残差清单，一处是闭包 port 没被当成半边的端口。

**路线 C 有了读数**（本页第三节说"没试过"）：把体的结果包一层（`vjp((ww) => weighted<ww>(2.0) + 0.0)`）绕出口形状，报文变成 `skip=residual (the forward half does not compute a value the cut carries)`——**绕不过去**，与第三节的判断一致。

另：结构体形状（`point_loss`）与两个 `with` 参数形状（`scaled`）更早就停，报文是 `REINTERP jvp function=point_loss skip=body` 与 `transforms: jvp has no forward transform for this function`，连 `split` 都没到。这两条是 `jvp` 侧的另两件，不在本页的三步里。
