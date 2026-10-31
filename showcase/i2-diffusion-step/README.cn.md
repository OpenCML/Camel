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

## 这一段在 `std::prl` 下怎么分段

**下降是更新挣来的，不是程序写死的。** 同一 seed 加 `--arg lr=0`，同样三行读数是
`1.202875 / 1.202875 / 1.202875`（平的），所以计划里"损失逐次下降"那条断言在更新不动参数时会红。
**一条没法让它红的断言不值得钉**，这一条能被弄红。

`std::prl segments=true` 打印调度器建出的计划而**不执行**，这是回答"并行在哪里"的诚实办法：

```
$ camel showcase/i2-diffusion-step/step.cml --arg seed=7 --arg steps=3 --arg lr=0.02 --arg t=1 \
    "std::prl segments=true"
[taskflow] segments 'main': no segments, evaluated node by node: it has a loop
[taskflow] segments 'predicted_noise': 1 segment(s), 0 chain(s), 5 operator(s)
[taskflow] segments 'predicted_noise' segment 0 (top): 9 item(s), 0 chain(s), inline
[taskflow] segments 'predicted_noise' oper tensor:matmul #3: segment 0, inline
[taskflow] segments 'alpha_bar': 3 segment(s), 0 chain(s), 4 operator(s)
[taskflow] segments 'alpha_bar' segment 1 (arm of #6): 2 item(s), 0 chain(s), inline, after segment 0 item 2
[taskflow] segments 'loss': 1 segment(s), 0 chain(s), 7 operator(s)
```

逐行读，它说的是三件事：

- **`main` 一段都没有，而且自己说了原因**：训练循环让调度器逐节点求值这张图（`it has a loop`
  ——原因记在计划上，所以能打出来）。循环是一串轮次，不是一棵可以并行的树，没有可切的东西。
- **每个函数都是一段、一条链、`inline`**：`predicted_noise` 是五个算子排成一行
  （`matmul, add, relu, matmul, add`），`loss` 七个，`weights` 三个——就是上面表格里的公式，一个全序。
  网络的层本来就是顺序的。
- **`alpha_bar` 是唯一有多段的地方**：三段，就是 if/else 链的各个臂，后一个挂在前面那个上
  （`arm of #6 … after segment 0 item 2`）。

所以"并行在哪里"的答案是：**这个例子里并行在算子内部，不在任务之间**——线程在 `tensor` 的 matmul
与 relu 内核里，任务图上没有独立的东西可调度。这也正是两档打印同样的数的原因：调度器没有重排的自由。
想看真的被切成链的计划（一个分支臂切成 11 条链、13 条链间边），见
`docs/handover/reports/prl-two-reds-and-segments.md`。

## 在 GPU 上

设备那一半已经在这里，而且能跑：去噪器的前向 `relu(x @ w1 + b1) @ w2 + b2` 经 `tiles` 通道变成内核，
在 GPU 上运行，程序打印设备的答案与主机在同一批数值上相差多少：

```
$ camel showcase/i2-diffusion-step/step_gpu.cml --devices 5
i2gpu device=NVIDIA GeForce RTX 3090 (sm_86)
i2gpu gap=0.0
```

`gap` 是逐元素平方差之和，设备与主机完全一致时它正好是 0。这是设备运行唯一能做出、又不必钉住某台机器的
断言：不断言时长、不断言序号、不断言设备名。

CPU 版是让这个读数有意义的前提：读数没被钉住的训练步，分不清设备的算术和它自己的 bug；所以主机版先写，
并且一直留作对照。

**序号是机器的事。** `--devices 5` 指的是写这份文档那台机器上一张空闲的卡；只有一张空闲卡的机器上就不写它。
`nvidia-smi` 会说哪张空闲——被别的进程占着的卡会 `CUDA_ERROR_OUT_OF_MEMORY`，那是环境，不是示例。

**用例不写设备号**，这一点和上面的命令不同，而且是故意的：计划条目设 `cuda_free_device = true` 并传
`--devices 0`，指的都是子进程能看见的那一张卡，与主机给它的是几号无关；runner 在开跑前挑一张有空间的卡，
所以同一个条目在"0 号正忙"的机器上读出来也是一样的。写进计划的序号是换台机器第一件会坏的事——一个取决于
别人正在跑什么的红，不是测试，是新闻。

## 文件

```
showcase/i2-diffusion-step/
  step.cml                   整个示例（Camel）
  step_gpu.cml               同一个去噪器前向在 GPU 上的那一版，带 `gap`
  README.md                  English README
  README.cn.md               本文件
```

跑它的计划：`test/plans/feat/showcase_i2.plan.toml`——默认后端与 `std::prl` 两档，打印同样的数。