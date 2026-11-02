# 墙 1 挡住了哪一行，清掉之后第一个读数是什么

这份文件是 `train.cml` 的注脚：那个程序是**按作者要的形状写的**（两层、两头、宽 16、context 8，
`vjp` 取梯度、`adam` 更新），今天一行也跑不了。这里记两件事——挡住它的那一行，以及清掉之后
拿到的第一个数应该是什么。

## 挡住它的那一行

`train.cml` 第 47 行：

```camel
import { vjp } from vjp
```

原样诊断（`showcase/c3-char-transformer/WALLS.cn.md` 墙 1，逐字）：

```
(vjp) .../out/latest/stdlib/vjp.cml:78:47: [Error]: Function of type '(float, float) => float'
cannot be called with argument types: <> (float). [in the instance 'vjp<(x: float) => float>' of
the generic function ...] (name=SemanticDiag::ArgumentsMismatch)
(train) .../train.cml:47:1: [Error]: Cannot import module 'vjp': it has errors. The errors
reported for it are above.
```

也就是说：`stdlib/vjp.cml` 自己编译不过（把 `(float, float) => float` 用一个参数调），
`train.cml` 连它的 `mode=forward` 分支都到不了——**导入失败发生在任何一行代码执行之前**。
这不是 C3 的模型或循环的问题：`block_engine_grad.cml`（同一个块、同一个模型算术）用引擎的
`with_grad` 能算出六个梯度，误差全 `0.000e+00`；缺的是库那一侧的规则。

## 清掉之后，第一个读数是什么

墙 1 一清，`train.cml` 的 `mode=forward` 会先打印两行（`c3 config ...` 与 `c3 init loss=...`），
然后是 20 步的 loss 序列。C3 的**整模型**第一个 loss **今天没有读数**（模型写不出一块统一的
元素类型，见墙 9），所以这里给的是同一台机器上能拿到的、同一个块的读数：把模型缩到
"最小注意力块"（`block_train.cml`，六个参数），用引擎 `with_grad` 走 50 步 Adam（lr=0.002，
β1=0.9，β2=0.999，eps=1e-8），起点与 `baseline/block_train.py` 逐字相同：

```
step  1  loss 0.455920556651
step  2  loss 0.449437188361
step  3  loss 0.442951748191
step 10  loss 0.397492874660
step 25  loss 0.299682415252
step 50  loss 0.135265634910     （第 50 次更新之后：0.128649809322）
```

参照实现（`baseline/block_train.py`，同一套 Adam 公式、同一个起点）在同样的步号上给出
**完全相同**的十二位小数，差 `0.000e+00`；50 步里没有任何一步上升。50 步的墙钟时间是
**166 ms**（含进程启动），约 **3.3 ms/步**。

## 这不是"绕开墙 1"

- `train.cml` 一个字没改，仍然是 `vjp` 版本：它今天仍然 `Cannot import module 'vjp'`。
- 训练读数是**另一条路径**（引擎 `with_grad`，正是 `compare.py` 里已经用来证明块算术正确的那条）；
  它的存在是为了说明"循环与模型是对的，缺的是库的规则"，不是替代库路径。
- 库路径的每参数梯度、整模型 loss 曲线，仍然要等墙 1 与墙 2–9。等它们倒了，
  `train.cml --arg mode=train --arg steps=20` 就是直接可跑的那条命令。