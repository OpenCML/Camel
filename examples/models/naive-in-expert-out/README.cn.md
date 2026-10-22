# 朴素写法进，专家写法出：一个 Transformer 训练步

这是旗舰示例的前一半：朴素程序、它的三个 PyTorch 对照、测量脚本与精度框架。把朴素源码自动
变成专家写法的规则流水线（`rules::accuracy`、`rules::fusion`、`explain`）是另一半，不在本目录。

模型小而固定：批 8、序列 16、d_model 64、4 个头每个宽 16、FFN 128、词表 32、两个 pre-LN 块、
68,288 个参数。损失是逐位置的 softmax 交叉熵；标签是每个位置前 32 个特征里最大的那个。种子
固定，模型里不出现墙钟时间。

## 运行

```sh
# 朴素 Camel 训练步，三个 CPU 后端任选。程序从命令行取输入；空目录表示"不写任何文件"。
camel -t 1 examples/models/naive-in-expert-out/train_step.cml \
    --arg bench=false --arg init_dir= --arg final_dir=
camel -t 1 examples/models/naive-in-expert-out/train_step.cml std::fvm \
    --arg bench=false --arg init_dir= --arg final_dir=
camel -t 1 examples/models/naive-in-expert-out/train_step.cml std::jit \
    --arg bench=false --arg init_dir= --arg final_dir=

# 三个 PyTorch 对照与 float64 参考，外加表格。
python examples/models/naive-in-expert-out/measure.py

# 精度测量（bf16/fp32/fp64；给 --camel 时也跑 Camel 的 f32 部分）。
python examples/models/naive-in-expert-out/accuracy.py \
    --camel out/latest/bin/camel
```

`measure.py` 需要一个已构建的 Camel 安装和带 torch 的 Python；它从检出目录与 `sys.executable`
找到两者。它把 `results.json` 写在脚本旁边（被 git 忽略）并打印表格。Camel 程序不读任何环境
变量：`bench`、`init_dir`、`final_dir` 都是 `main` 的参数，上面第一条命令把三个都显式传入；
`measure.py` 自己也这样传（`--arg bench=true --arg init_dir=... --arg final_dir=...`）。

线程预算只来自命令行的 `-t`：一次运行的每个池（图调度器与各模块自己的工作者池，张量内核的池
也在内）都从这一个数取大小，`CAMEL_NUM_THREADS` 已不再读取。`measure.py` 给 Camel 传
`-t 1`，给 PyTorch 用 `torch.set_num_threads(1)` 加 OMP/MKL 变量——两个框架各用自己的机制，
都是单线程。

## 测得的数字

2026-10-22 19:27 UTC（北京时间 2026-10-23 03:27）的一次测量。表格就是 `measure.py` 打印的那张；
每一步的原始时间与当时的负载在
[`results/2026-10-23-linux.json`](results/2026-10-23-linux.json)。

| 实现 | ms/步 | 3 轮范围 | 峰值 RSS MiB | 最终损失 | \|dloss\|（对 fp64） | max \|dparam\|（对 fp64） |
|---|---|---|---|---|---|---|
| Camel（`std::jit`） | **2.091** | 2.029–2.195 | 40.5 | 1.568042986 | 1.8e-08 | 7.0e-06 |
| PyTorch 朴素 | 4.762 | 4.595–4.849 | 643.7 | 1.568042994 | 1.1e-08 | 7.5e-06 |
| PyTorch 专家 | 3.919 | 3.750–4.013 | 643.7 | 1.568043113 | 1.1e-07 | 1.0e-05 |
| `torch.compile(朴素)` | 3.005 | 2.985–3.287 | 924.1 | 1.568042994 | 1.1e-08 | 1.4e-05 |
| float64 参考 | 5.802 | — | 648.4 | 1.568043004 | 0 | 0 |

**条件**。主机 `tangshan88`：AMD EPYC 7343 16-Core，32 个硬件线程，503 GiB 内存，Ubuntu 22.04
（内核 6.8.0-138）。Python 3.10.12、NumPy 2.2.6、PyTorch 2.14.1+cu130（CUDA 构建，但所有变体
都跑在 CPU 上），Camel v0.2.24，由提交
`ab9cb4579` 干净构建（无未提交路径；构建指纹 `0dc0fec4c9b1`，覆盖 1,152 个源文件）。主机是**共享**
的：测量期间其他构建与测试一直在跑，1 分钟负载均值开始时 31.7、结束时 34.1（32 个硬件线程）。产出
这个文件的测量脚本是本分支的 `38fbcbacc`（测量时它是未提交的同一份内容）；脚本是 Python，被测
二进制里没有任何改动。

**命令**。整张表一条命令：

```sh
python examples/models/naive-in-expert-out/measure.py \
    --camel out/latest/bin/camel --backend jit --repeats 3 \
    --output results/2026-10-23-linux.json
```

它按下面的方式跑，线程预算显式传给两个框架：

```sh
out/latest/bin/camel -t 1 .../train_step.cml std::jit \
    --arg bench=true --arg init_dir=<scratch>/init --arg final_dir=<scratch>/final/camel
python .../torch_impl.py --variant naive|expert|compiled|reference \
    --init <scratch>/init --steps 16 --warmup 3 --threads 1 \
    --save-final <scratch>/final/<variant>.npz
```

每个实现跑 16 步，前 3 步预热，步时取其余 13 步的中位数，在进程内围绕一次前向、反向与 Adam 更新
测量。四个实现按轮转顺序交错跑 3 轮；表里的 ms/步 是 3 轮各自中位数的中位数，范围是它们的最小值–
最大值。峰值 RSS 是各轮 `/usr/bin/time -v` 整进程最大常驻集里的最大值，包含解释器与分配器，
所以它比的是足迹，不是模型大小。

**范围说明了什么**。Camel 2.029–2.195（范围 0.166 ms），PyTorch 朴素 4.595–4.849（0.255），专家
3.750–4.013（0.262），compiled 2.985–3.287（0.301）。相邻差距是 0.914 ms（Camel → compiled）、
0.914（compiled → expert）与 0.843（expert → naive），都大于最大的范围（0.301），所以在这个窗口里
顺序是真的；小于约 0.3 ms 的差异不是。绝对值也不能搬到别的机器：同一行 Camel 在负载从 42 升到 68
的一次运行里是 3.081 ms（范围 2.165–4.555），更早一次没记录负载的单轮探针是 2.552 ms。这就是表里
要有范围列、而不是只给一个排名的原因。

**它说明与不说明什么**。2.091 ms 这一行是**朴素**源码：独立的 Q/K/V、不减最大值、`log(softmax)`，
没有为这个模型写任何 kernel。它比 eager PyTorch 里的同一个朴素算法（4.762）、比 PyTorch 手写的
专家形式（3.919）、也比 Inductor 编译后的朴素写法（3.005）都快。赢的是张量运行时与调度器，不是
手调 kernel，也不是规则：`rules::accuracy`/`rules::fusion` 还不存在，朴素源码没有被改写成专家形式。
比较也有另一面：Inductor 编译后的朴素写法在这里比手写专家形式快，所以"专家"并不等于最快。

**更早的数字**。此前报告过 2.062 ms/步（更早的修订）与 2.314 ms/步（繁忙主机）。两份报告都没有在
树里留下机器、命令或修订——本仓库的文件与提交信息里都找不到这两个数字——所以它们是背景，不是基线。
本次的 2.091（2.029–2.195）与两者都相容；2.062 与 2.091 的差小于本次测量的范围，不能当作回归的
证据。

## 朴素程序做了什么

`train_step.cml` 按教科书写，不做任何数值处理：

- 三个独立的 Q、K、V 投影（`h @ wq`、`h @ wk`、`h @ wv`）；
- `p = exp(scores) / sum(exp(scores), -1, true)`，不减去最大值；
- 交叉熵先 `p = exp(logits) / sum(exp(logits), -1, true)` 再 `log(p)`，没有 `log_softmax`；
- RMSNorm、GELU（tanh 近似）、Adam，以及一次 `with_grad` 反向。

缩放是算出来的 `1 / sqrt(d_head)`，不是写死的字面量 `4.0`：`attention_scale` 由模型实际使用的
宽度算一次。参数计数是带名字的 `param_count`，不是塞在 `println` 插值里的表达式。

标签是 one-hot 乘积；标签是离散值，按定义导数为零（设计原则 9.9）。`one_hot` 注册的是空规则，
标签不需要 `stop_gradient`。

## PyTorch 对照

`torch_impl.py --variant naive|expert|compiled|reference`：

| 变体 | 与 Camel 源码的差别 |
|---|---|
| `naive` | 没有差别：独立的 Q/K/V、`exp/sum(exp)`、`log(softmax)` |
| `expert` | `F.scaled_dot_product_attention`、`F.log_softmax`、一个融合 QKV 投影 |
| `compiled` | 对朴素损失用 `torch.compile`（前向与反向都编译；Adam 保持 eager，且四个变体是同一个显式公式） |
| `reference` | 同一张图用 float64；不属于四个被测实现 |

测量脚本先跑 Camel 程序，由它把初始参数与数据写成 `.npy`；每个 PyTorch 变体加载的正是这些
文件。跨框架的数字之所以可比，原因就在这里。四个实现的 Adam 更新是同一个显式公式（在
float64 中计算、存回 float32），与 `nn.adam` 一致；优化器不是被测差异之一。

## 容差

`measure.py` 要求最终损失与 float64 参考相差不超过 `1e-5`（绝对值），每个最终参数不超过
`2e-4`（绝对值）。逐位相等不可能：同一个 f32 运算在 Camel 内核与 ATen/oneDNN 里的归约顺序
不同，SDPA 算注意力块的方式与显式 softmax 不同，融合 QKV 是一次归约而不是三次，
`torch.compile` 会融合逐元素链，而 float64 参考根本是另一个宽度。实测差距比这两个界低一到
两个数量级（见 `measure.py` 打印的表格）。

## 精度

`accuracy.py` 在输入范围内扫出朴素写法第一次不再有限的值，分别对 bf16、f32、f64，并在同一
输入上报告专家写法；它还测 softmax、log_softmax、交叉熵与矩阵乘归约的 fp64 误差，以及把
`1` 舍掉的 bf16 归约。`accuracy.cml` 在 Camel 里重复 f32 的测量。

## 发现的缺口（Camel 侧）

- **H238（优先）：张量仍只有一种存储类型。** 公开的张量元素类型实际上只有 f32、i64 与 bool；
  f64 在创建和转换时被 `normalizeTensorDType` 归一为 f32，i32 归一为 i64。这段是暂行的：
  **H238** 落地后（分支 `handover/h259-dtype`，拒绝 `Tensor<f64>`/`Tensor<i32>` 而不是替换；
  `handover/h260-tensor-storage`，删掉归一化表）删掉它；若 f64 可用，再给表加一列。在那之前，
  bf16 溢出与训练步的 fp64 误差无法在 Camel 里测量；它的 f32 阈值与 PyTorch 的 f32 阈值逐项
  一致。
- **两层块现在是一个 `Block[]`，前向是普通的 `loop h = x for b in m.blocks`。** 数组的切向量
  就是元素切向量组成的数组，所以 `for` 能对依赖可微参数的数组求导：反向把每一轮的梯度按该轮
  的下标散射回数组，优化器与 `save_params` 逐元素走过它。只有当**元素类型本身没有切向量**
  （string、函数、`Map`）时数组才没有切向量，拒绝发生在 `loop` 上，并指名数组类型、元素类型
  与数组的写入位置。检查点文件名不变（`blocks.0.wq.npy`、`blocks.1.wq.npy`），数组元素按
  下标命名，旧检查点仍可加载。
- **`f32[b, t, d]` 还进不了张量算子。** `exp`、`@`、`sum` 等接收的是 `Tensor`，不是核心数组
  类型；`let x: f32[2, 3] = randn(...)` 是 `DeclaredTypeMismatch`。示例用带形状的
  `Tensor<f32, [8, 16, 64]>`（以及 `TokenStates`、`HeadStates` 等别名），每个形状都写全；
  但 `Tensor<f32, [b, t, d]>` 里的维度名不绑定（检查器警告 `DimensionNameUsedOnce`），所以
  只写常量形状，由别名与顶部注释说明含义。
- **没有写成类型的 dtype 转换。** `f32(oh)` 没有张量重载，`oh as f32[...]` 是
  `DynamicCastForbidden`；one-hot 乘积用数值提升（f32 * int64 得 f32）代替旧的
  `cast(oh, 'float32')` 字符串。带类型的张量转换仍是缺口。
- **没有融合 QKV/注意力算子，也没有 `rules::accuracy`/`rules::fusion` 流水线**，所以专家
  写法不是从朴素源码自动得到的。`softmax`、`log_softmax` 与融合交叉熵作为普通算子已经存在，
  PyTorch 专家实现与精度探针会用到它们。
- **`explain`（规则改写链）已经可用**（`stdlib/rewrite.cml` 的 `run`/`explain`），但本目录
  还没有展示它。
