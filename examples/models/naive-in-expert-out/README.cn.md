# 朴素写法进，专家写法出：一个 Transformer 训练步

这是旗舰示例的前一半：朴素程序、它的三个 PyTorch 对照、测量脚本与精度框架。把朴素源码自动
变成专家写法的规则流水线（`rules::accuracy`、`rules::fusion`、`explain`）是另一半，不在本目录。

程序只按一个形状写：批 8、序列 16、d_model 64、4 个头每个宽 16、FFN 128、词表 32、两个 pre-LN
块、68,288 个参数。扫掠把同一个程序放到 d_model 64、256、1024 上测，批与序列按[扫掠](#扫掠)一节
的规则放大。损失是逐位置的 softmax 交叉熵；标签是每个位置前 32 个特征里最大的那个。种子固定，
模型里不出现墙钟时间。

## 运行

```sh
# 朴素 Camel 训练步，三个 CPU 后端任选。程序从命令行取输入；空目录表示"不写任何文件"。
camel -t 1 examples/models/naive-in-expert-out/train_step.cml \
    --arg bench=false --arg init_dir= --arg final_dir=
camel -t 1 examples/models/naive-in-expert-out/train_step.cml std::fvm \
    --arg bench=false --arg init_dir= --arg final_dir=
camel -t 1 examples/models/naive-in-expert-out/train_step.cml std::jit \
    --arg bench=false --arg init_dir= --arg final_dir=

# 单一尺寸的表：三个 PyTorch 对照与 float64 参考。
python examples/models/naive-in-expert-out/measure.py

# 结论所依赖的扫掠：三个宽度，单线程与每个物理核一个线程各一遍。
python examples/models/naive-in-expert-out/measure.py \
    --d-model 64,256,1024 --threads 1,16 --repeats 3 \
    --output results/2026-10-23-sweep-linux.json

# 为什么这台机器上的"满核"是 16 线程而不是 32 个硬件线程。
python examples/models/naive-in-expert-out/thread_scale.py \
    --output results/2026-10-23-thread-scaling-linux.json

# 精度测量（bf16/fp32/fp64；给 --camel 时也跑 Camel 的 f32 部分）。
python examples/models/naive-in-expert-out/accuracy.py \
    --camel out/latest/bin/camel
```

`measure.py` 需要一个已构建的 Camel 安装和带 torch 的 Python；它从检出目录与 `sys.executable`
找到两者。单个格子把 `results.json` 写在脚本旁边（被 git 忽略）并打印表格；`--output` 改为写一个
要提交的文件。Camel 程序不读任何环境变量：`bench`、`init_dir`、`final_dir` 都是 `main` 的参数，
上面第一条命令把三个都显式传入；`measure.py` 自己也这样传（`--arg bench=true --arg
init_dir=... --arg final_dir=...`）。

线程预算只来自命令行的 `-t`：一次运行的每个池（图调度器与各模块自己的工作者池，张量内核的池
也在内）都从这一个数取大小，`CAMEL_NUM_THREADS` 已不再读取。`measure.py` 给 Camel 传 `-t N`，
给 PyTorch 用 `torch.set_num_threads(N)` 加 OMP/MKL 变量——两个框架各用自己的机制跑 N 线程。
`--threads all` 就是 `os.cpu_count()`，在下面这台机器上是 32。

## 扫掠

本目录原先那张表只测了一个点：d_model 64、每步 128 个 token、单线程，Camel 2.09 ms/步，eager
PyTorch 4.76。一个点撑不起它被挂上的结论，因为它分不清赢的是张量运行时，还是"一步只有几毫秒
时每个算子都要付的固定开销"。扫掠才是这个结论需要的表：同一个程序、同样的四个实现，三个宽度
（`d_model` 64、256、1024），每个宽度跑单线程与每个物理核一个线程。

**宽度怎么变**。从程序自己的形状出发，`d_model = d` 的格子取 `s = sqrt(d / 64)`，于是
`batch = 8 * s`、`sequence = 16 * s`、`d_ff = 2 * d`、`d_head = 16`、`vocabulary = 32`、两个块：

| d_model | 批 × 序列 | 每步 token | 头数 | d_ff | 参数量 |
|---|---|---|---|---|---|
| 64 | 8 × 16 | 128 | 4 | 128 | 68,288 |
| 256 | 16 × 32 | 512 | 16 | 512 | 1,059,584 |
| 1024 | 32 × 64 | 2,048 | 64 | 2,048 | 16,821,248 |

`batch` 与 `sequence` 彼此按同一个倍数增长，token 数随宽度增长，于是一步的算术量随宽度的立方
增长（每放大 4 倍变 64 倍）。这就是这个问题的诚实选择，而且写出来而不是默认：一步里算子的个数
由程序固定，所以每算子固定开销在每步里大致不变，而算术量在长；要看到它不再占主导，算术量必须
同时随宽度和数据增长。只放大宽度会测一个更窄的命题；两个轴都按宽度比放大（各 ×4，每步 ×64 个
token）会让最大的步子是最小的 65,536 倍，每个格子要跑几个小时去回答同一个问题——交叉点若在这个
范围里，远在那之前就到了。`measure.py` 的 `shape_for` 就是这条规则；`scaled_source` 从
`train_step.cml` 按格子点名的每个维度做替换生成 Camel 程序，逐条统计替换次数，记录生成文件的
SHA-256，并在接受一次计时前用程序自己打印的 `naive config` 行核对格子。

**一个格子怎么测**。每个实现跑 16 步，前 3 步预热，步时取其余 13 步的中位数，在进程内围绕一次
前向、反向与 Adam 更新测量。四个实现按轮转顺序交错跑 3 轮（第 *r* 轮从第 *r* 个实现开始）；
表里的 ms/步 是 3 轮各自中位数的中位数，范围是它们的最小值–最大值。float64 参考每个格子跑一次，
只用于精度比较，所以它的行带着那一次运行碰到的负载：d=256 16 线程那格的 3233 ms 就是这样一次运行
（同一个程序在单线程那一格是 116 ms）。两个条目只有在差距大于双方的范围时才有先后；轮次范围重叠
的，表里的数字排不出先后，结论会照实说。

<!-- sweep-table:begin -->
**ms/步, 单线程** (3 轮交错的中位数；括号内为轮次范围)

| 实现 | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel（`std::jit`） | 2.085 (2.072–2.089) | 60.980 (60.167–67.704) | 3186.178 (3066.772–3268.403) |
| PyTorch 朴素 | 4.569 (4.474–4.570) | 57.113 (56.472–62.645) | 2947.278 (2918.314–4617.991) |
| PyTorch 专家 | 3.803 (3.777–3.830) | 58.879 (54.927–59.230) | 2397.578 (2328.843–2415.207) |
| `torch.compile(朴素)` | 2.938 (2.929–2.955) | 49.779 (49.350–50.593) | 2393.691 (2383.373–3522.431) |
| float64 参考 | 5.561 (单次运行) | 116.069 (单次运行) | 6780.466 (单次运行) |

**ms/步, 16 线程** (3 轮交错的中位数；括号内为轮次范围)

| 实现 | d=64 | d=256 | d=1024 |
|---|---|---|---|
| Camel（`std::jit`） | 4.466 (3.714–4.616) | 42.194 (41.467–48.164) | 1897.177 (708.758–2134.288) |
| PyTorch 朴素 | 8.087 (6.582–8.138) | 35.831 (33.301–72.997) | 473.269 (453.214–10953.000) |
| PyTorch 专家 | 7.669 (5.150–7.740) | 32.433 (29.952–34.261) | 398.472 (362.432–2628.657) |
| `torch.compile(朴素)` | 4.943 (4.827–5.270) | 25.840 (24.102–3289.390) | 693.676 (446.420–15543.034) |
| float64 参考 | 11.166 (单次运行) | 3232.954 (单次运行) | 1209.214 (单次运行) |

<!-- sweep-table:end -->

**交叉点**。单线程下 Camel 只在 d=64 是唯一最快的实现（2.085 ms，`torch.compile(naive)` 2.938），
到 d=256 就不再是：`torch.compile(naive)` 49.8 对 Camel 61.0，11 ms 的差距比双方的范围都宽，而
Camel、PyTorch 朴素与专家形式三者互相落在对方范围内。d=1024 的中位数保持同样的顺序
（`torch.compile` 2394、专家 2398、朴素 2947、Camel 3186），但 `torch.compile` 有一轮到 3522，
范围把 Camel 包住，所以按范围只有专家形式排在 Camel 前面。16 线程的交叉点在同一个宽度：d=64
Camel 唯一最快（4.47，`torch.compile` 4.94）；d=256 它的 42.2 ms 是四个中位数里最高的（25.8、
32.4、35.8），不过那一格有一轮撞上负载尖峰、四个范围互相重叠。所以交叉点是 **d_model 256**，两
种线程数都一样：单线程下可分辨地落后于 `torch.compile(naive)`，16 线程下按中位数落后但范围分不
开。64–1024 里没有哪个宽度、哪种线程数下，Camel 在 d=64 之外还是唯一最快。

**并列，以及把它们印成并列的代价**。六格里四格出现并列组，其中三格是全体并列（d=256 的 16 线程、
d=1024 的两种线程数）。原因在范围里看得见：d=256 16 线程那一格，`torch.compile` 的一轮 3289 ms，
另外两轮 24–26 ms——主机的负载在一轮中途翻倍——而一轮尖峰就足以把范围撑到与所有其他范围重叠。
把并列印成并列，代价是两面的：它拒绝只看中位数时能说的那句话（"16 线程下 PyTorch 快 1.6 倍"），
也拒绝相反的那句（"Camel 在 256 还领先"）：d=256 往上 Camel 不再是可分辨的最快，这次运行也没有
界定它落后多少。d=1024 16 线程下 Camel 对 PyTorch 朴素，按中位数是 4 倍，按最快一轮是 1.6 倍，
而这一格的负载从 12 涨到 109；两个数字都不是对框架的测量。这次运行支持的只是形状：d=64 对 PyTorch
朴素 2.2 倍的优势没有活过第一次放大。

**条件**。主机 `tangshan88`：AMD EPYC 7343，16 个物理核、32 个硬件线程，503 GiB 内存，Ubuntu
22.04（内核 6.8.0-138），Python 3.10.12、NumPy 2.2.6、PyTorch 2.14.1+cu130（CUDA 构建，所有变体
都跑在 CPU 上），Camel v0.2.24。这些数字测自一个指纹为 `808a7e445efc` 的二进制，构建时间
21:07:38 UTC，源码是 `2adf18c04` 加上本目录的 `is_numeric` 改动（该改动在扫掠进行中才提交为
`539ad3808`；重建那棵树得到同一个指纹）。这棵树后来合并了 develop（`bf6fe3a54`），而这里提交的
这棵树重建出来的指纹是 `8d3a616b7faa`：所以这些数字属于更早的那次构建，合并之后没有重测任何一行。
测量脚本就是随这张表提交的 `measure.py`；它是 Python，被测二进制在取数期间没有变。主机是**共享**
的：测量期间其他构建与测试一直在跑，1 分钟负载均值在 12 到 109 之间（32 个硬件线程）；每格的起始、
结束与逐轮采样都在 JSON 里，两个 d=1024 的格子赶上了最糟的一段。

"16 线程"是每个物理核一个线程，不是全部 32 个硬件线程。在 16 核的 SMT 机器上要 32 个线程，测的是
主机而不是框架：在
[`results/2026-10-23-thread-scaling-linux.json`](results/2026-10-23-thread-scaling-linux.json) 里，
2048×1024 的 matmul 在 16 线程是 49.4 ms、在 32 线程是 47.4 ms（一点没快），而 64×64 的 matmul 在
单线程是 0.0145 ms、16 线程 32.3 ms、32 线程 48.8 ms——这个模型由小算子拼成，每多给一个线程就多付
一整份唤醒开销。那次运行的负载是 72；它的绝对值与扫掠一样属于共享主机，能复现的是"第 32 个线程
没有带来收益"。所以这里的"满核"是 16。绝对值本来也不可移植：同一个 d=64 单线程 Camel 行在这里是
2.09，早先一次探针是 2.55；这里每一行都是共享主机的数字。

**d=1024 是计时格，不是收敛格**。在那里所有 fp32 实现在第 8 步离开有限数：Camel 与 PyTorch 到第
7 步损失相同到五位（3.531943823 与 3.531949043），而同初始化、同学习率的 float64 参考收敛到 0.25。
朴素源码的文件头写着大 logits 会让 `exp` 溢出；这里发生的不是那件事（logits 一直在 O(1)），是 fp32
的轨迹自己发散。于是四个实现里有三个的计时有一部分（16 步里的第 8–15 步）跑在非有限值上。算子序列
在边界处不变，每步时间也不变（d=1024 单线程 Camel 第 2 轮：3050–3086 ms，跨边界是平的），所以
ms/步 成立；与 float64 的损失和参数比较在那几行不存在，JSON 记的是 `diverged_at_step: 8` 而不是
数字，这里也没有测过那个尺寸下一个**稳定**的 fp32 步有多快。性能结论说的是算术，而算术不因值是
数字还是 NaN 而改变。

**扫掠没有覆盖什么**。d_model 1024 的内存不是这里的对象：用运行时默认的 2 GiB 块空间，2,048 token
的格子根本跑不起来（33.5 MB 的张量被拒，当时已用 1,911 MB），所以 `measure.py` 给每个格子都传
`--block-limit 8192`，让各格子可比。峰值 RSS 按格子记录——d=1024 时 Camel 与 PyTorch 朴素是
2.8 GiB、专家形式 2.5、`torch.compile` 2.8、float64 参考 4.3——但没有任何东西画分配器、找某个格子
从哪里开始放不下，或者把 2 GiB 这个限制本身当作测量对象。规则流水线的效果没有覆盖，因为它还不能
端到端使用：`rules::accuracy` 与 `rules::fusion` 作为库存在、有自己的测试，但朴素源码还没有被改写
成专家形式，所以裁决里要的"Camel + 规则"那一行测不了。GPU 不在覆盖范围。

**更早的数字**。此前报告过 2.062 ms/步（更早的修订）与 2.314 ms/步（繁忙主机）。两份报告都没有在
树里留下机器、命令或修订——本仓库的文件与提交信息里都找不到这两个数字——所以它们是背景，不是基线。
单一尺寸那张表的 2.091（2.029–2.195，见
[`results/2026-10-23-linux.json`](results/2026-10-23-linux.json)）与两者都相容；2.062 与 2.091 的
差小于那次测量的范围，不能当作回归的证据。

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

- The public `Tensor` element types are f32, f64, i32, i64 and bool: a tensor stores the element
  type it is written with, so the fp64 error of the step can be measured in Camel today. There is
  still no bf16/f16 element type, so bf16 overflow cannot be measured in Camel; its f32 thresholds
  match PyTorch's f32 thresholds exactly.
- **两层块现在是一个 `Block[]`，前向是普通的 `loop h = x for b in m.blocks`。** 数组的切向量
  就是元素切向量组成的数组，所以 `for` 能对依赖可微参数的数组求导：反向把每一轮的梯度按该轮
  的下标散射回数组，优化器与 `save_params` 逐元素走过它。只有当**元素类型本身没有切向量**
  （string、函数、`Map`）时数组才没有切向量，拒绝发生在 `loop` 上，并指名数组类型、元素类型
  与数组的写入位置。检查点文件名不变（`blocks.0.wq.npy`、`blocks.1.wq.npy`），数组元素按
  下标命名，旧检查点仍可加载。
- **`f32[b, t, d]` 还进不了张量算子。** `exp`、`@`、`sum` 等接收的是 `Tensor`，不是核心数组
  类型；`let x: f32[2, 3] = randn(...)` 是 `DeclaredTypeMismatch`。示例用带形状的
  `f32[8, 16, 64]`（以及 `TokenStates`、`HeadStates` 等别名），每个形状都写全；
  但 `f32[b, t, d]` 里的维度名不绑定（检查器警告 `DimensionNameUsedOnce`），所以
  只写常量形状，由别名与顶部注释说明含义。
- **没有写成类型的 dtype 转换。** `f32(oh)` 没有张量重载，`oh as f32[...]` 是
  `DynamicCastForbidden`；one-hot 乘积用数值提升（f32 * int64 得 f32）代替旧的
  `f32(oh)` 字符串。带类型的张量转换仍是缺口。
- **没有融合 QKV/注意力算子，也没有 `rules::accuracy`/`rules::fusion` 流水线**，所以专家
  写法不是从朴素源码自动得到的。`softmax`、`log_softmax` 与融合交叉熵作为普通算子已经存在，
  PyTorch 专家实现与精度探针会用到它们。
- **`explain`（规则改写链）已经可用**（`stdlib/rewrite.cml` 的 `run`/`explain`），但本目录
  还没有展示它。
