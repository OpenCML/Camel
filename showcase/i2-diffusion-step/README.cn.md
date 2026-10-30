# I2 — 扩散模型的一步训练

展示设计稿里的 I2 是"扩散模型训练：DDPM、UNet、CIFAR-10 或 64×64 人脸"
（`docs/design/showcase/README.cn.md`），GPU 设计稿又把"扩散模型训练步"列为它的验收基准之一。本目录
是那个示例的 **CPU 前置**：同一套训练目标，模型小到三步在一秒内跑完，好让语义在谈设备之前先钉住。

`step.cml` 就是整个示例：DDPM 的一步训练（Ho 等 2020，简化目标），一张 4×4 的图，一个写成参数树
的两层去噪网络。

## 一步训练，逐项对应公式

| 代码做什么 | 它对应的公式 |
|---|---|
| `weights(t)` 返回 `(sqrt(alpha_bar_t), sqrt(1 - alpha_bar_t))` | 噪声表 `alpha_bar_t = prod_{s<=t} (1 - beta_s)`，四个时间步取 `beta = 0.02, 0.05, 0.10, 0.20` |
| `x_t = x0 * keep + eps * add` | 前向过程 `x_t = sqrt(alpha_bar_t)·x_0 + sqrt(1 - alpha_bar_t)·eps`，`eps ~ N(0, I)` |
| `predicted_noise<m>(x)` | 网络 `eps_hat = denoiser(x_t)`：`relu(x @ w1 + b1) @ w2 + b2`，16 → 8 → 16 |
| `loss<m>(x_t, eps)` | 简化目标：`(eps_hat - eps)²` 对所有元素取均值 |
| `adam(m, gradient, opt, lr)` | 对上面这个损失做一步 Adam 更新 |

参数是四个张量组成的结构体，所以 `with_grad` 给出同类型的梯度、`adam` 直接吃它，程序不用点名任何
一个叶子——与 `showcase/c2-mlp-mnist` 同一个形状。

## 跑它

```
camel showcase/i2-diffusion-step/step.cml --arg seed=7 --arg steps=3 --arg lr=0.02 --arg t=1
camel showcase/i2-diffusion-step/step.cml --arg seed=7 --arg steps=3 --arg lr=0.02 --arg t=1 std::prl
```

两档打印同样五行：

```
i2 config seed=7 steps=3 lr=0.02 t=1 bar=0.93100 image=4x4 batch=4
i2 step=0 loss=1.202875
i2 step=1 loss=1.103411
i2 step=2 loss=1.020298
i2 trained t=1 loss=0.944348
```

损失逐次下降，训练后那次读数又低于最后一步；`test/plans/feat/showcase_i2.plan.toml` 断言的就是
这件事，并把五行钉死——把前向过程、目标或更新写错的那一步会红，而不是打出一条别的曲线。

每个数都是 `--arg seed` 的函数：初始权重与噪声都来自 `random` 里那套计数器式生成器（`key`、
`fold`、`normal`），不读时钟，所以同参数两次运行逐字相同，损失可以当断言而不只是看一眼。

**同一次运行里每一步用同一个时间步、同一份噪声**——`--arg t=0..3` 才是走噪声表；三行能连成一条曲线，
正是因为除了参数没有别的东西在变。每步各抽自己那份噪声的曲线会把噪声水平混进读数里。

## 这里没有什么，为什么

- 一步一个时间步，而不是每个样本随机抽：演示的曲线要可比，随机抽样是真正的训练器与它分道的第一处；
- 一张 4×4 的图而不是 CIFAR-10/64×64，两层网络而不是 UNet：示例的主语是"训练这一步"，UNet 加的是
  参数，不是语义；
- 没有采样器、没有 EMA、没有学习率调度：这里只做 I2 的训练那一半。

## 为什么这一步还没上 GPU

派工裁定原文（`docs/handover/reviews/questions-2026-10-29.md`）：**"H237 杀手级示例、H13 官方示例
（D4 MCTS、扩散模型训练步）：GPU 那一路能跑之后接上"**——扩散模型训练步是等 GPU 那一路能跑之后再
接上的，GPU 设计稿把同一个训练步列成验收基准，也是这个原因。所以顺序是：先有这一版 CPU 的，再谈设备
上的那一版。**读数没有被钉住的训练步，分不清设备的算术和它自己的 bug**，所以 CPU 版是前置，不是绕路：
先把语义写对，再谈设备。

## 文件

```
showcase/i2-diffusion-step/
  step.cml                   整个示例（Camel）
  README.md                  English README
  README.cn.md               本文件
```

跑它的计划：`test/plans/feat/showcase_i2.plan.toml`——默认后端与 `std::prl` 两档，打印同样的数。