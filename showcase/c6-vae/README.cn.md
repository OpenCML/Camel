# C6 — 变分自编码器（VAE）

一个最小的 VAE：二维点 → 16 维隐藏 → 均值/对数方差两个头 → 重参数化采样 → 解码回二维。
损失是重建误差加闭式 KL。它专门展示两件事：

1. **重参数化**：`z = mu + exp(0.5 * logvar) * eps`，采样写成对 `eps` 的普通张量运算，梯度
   可以正常穿过；
2. **两个头共享编码器**：`mu` 与 `logvar` 是同一隐藏层的两个线性输出，参数树里是四个张量，
   梯度树自然同型。

模型只有 198 个参数，数据是二维合成点；这是为了让整个“编码—采样—解码—KL”在几秒内跑完，
并且能和 PyTorch 逐项对照。

## 目录

```
showcase/c6-vae/
├── train.cml                 Camel 程序
├── baseline/train_torch.py   PyTorch 对照
├── make_data.py              四团高斯点、初始化权重、固定的 eps 文件
├── compare.py                跑两边、比数值、量时间/内存、写 RESULTS.md
├── RESULTS.md                standard 配置的实测结果（脚本生成）
├── README.cn.md
└── README.md
```

## 数据与随机数

`make_data.py` 用固定种子生成四团二维高斯点（中心 `(-2,-1)`、`(1.5,2)`、`(-1.5,2)`、
`(2,-2)`），1024 行训练、256 行验证；另外生成 40000 行标准正态 `eps`，按步切给两边使用。

**为什么 eps 在数据里**：语言里现在只有推进全局状态的 `random::randn`，没有键式随机数，
程序内采样会让两次运行的噪声不同，PyTorch 对照就没有意义。H20-3 会加
`random::key(seed)` / `split` / `permutation`（Philox，纯函数）；落地后这里改成循环内
`split` 出每步的键，不再预生成。README 与报告都记了这个依赖。

## 运行

```powershell
python showcase/c6-vae/make_data.py --out showcase/c6-vae/artifacts/small
$env:CAMEL_HOME = "D:\Projects\Camel-wt\h13-showcase\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/c6-vae/train.cml --arg data=showcase/c6-vae/artifacts/small `
    --arg steps=200 --arg batch=32 --arg lr=0.005 --arg log_every=20 --arg threads=1 `
    std::macro std::nvm
python showcase/c6-vae/compare.py --profile small
python showcase/c6-vae/compare.py --profile standard   # 写 RESULTS.md
```

## 结果（standard，1000 步，单线程）

| 指标 | Camel | PyTorch | 差 |
|---|---|---|---|
| 初始验证损失 | 7.212991182 | 7.212991089 | 1.3e-08 |
| 记录步最坏损失（第 800 步） | 2.647558995 | 2.647559404 | 1.5e-07 |
| 最终验证损失 | 1.996342750 | 1.996342555 | 9.8e-08 |
| 最终重建 MSE | 0.213992766 | 0.213992611 | 7.2e-07 |
| 最终 KL | 1.782349985 | 1.782349944 | 4.1e-08 |

训练时间 0.034 s vs 1.629 s，峰值内存 18.7 MiB vs 274.8 MiB，编译 0.046 s。完整表格在
`RESULTS.md`。

**性能说明**：这个模型只有 198 个参数，PyTorch 的每步时间主要是 Python/autograd/优化器
开销，Camel 的循环在 C++ 里解释；0.021 的比值说明小模型上框架开销占主导，**不是**“Camel
的矩阵内核快 40 倍”。C2 的 MLP（784→128→10）才是计算量占主导的对照，那里 Camel 慢
17–21%。峰值内存同理：274.8 MiB 主要是 PyTorch 运行时本身。

## 容差

- 初始损失 1e-05 相对（只差在矩阵乘与偏置加的顺序）；
- 每个记录步、最终损失、重建 MSE 都是 1e-02 相对。实测差都在 1e-6 以下，因为模型小、
  Adam 的偏差修正两边一致；
- 不是逐位相等：设计原则 7 只对同一后端的同一算法承诺逐位相等。

`compare.py` 任一界限越界即非零退出。

## 并排看

| 事情 | Camel | PyTorch |
|---|---|---|
| 编码器 | `relu(x @ ew1 + eb1)` 再乘两个头 | 两个 `nn.Linear`＋一个隐藏层 |
| 采样 | `mu + exp(0.5 * logvar) * eps` | 同式 |
| KL | `-0.5 * mean(sum(1 + logvar - mu² - exp(logvar), 1))` | 同式 |
| 更新 | `adam(m, gradient, opt, lr)`，优化器状态在循环里 | `optimizer.step()` |
| 循环 | `loop m, opt, step ... do sync { ... }` | `for step in range(...)` |

### 求导的写法

设计原则 9.9（H21 第 3 项已撤回，见 H25）：`with <m: Vae>` 是最终形态，
`with_grad(loss)<m>(x, eps)` 对第一个 `with` 参数求导，返回与 `m` 同型的梯度；其余
参数是数据。本示例把 `with_grad(loss)` 绑定在循环外。

## 遇到的问题

- **没有键式随机数**：见上，靠预生成 eps 绕开；H20-3 落地后应改掉。这不是本示例想推广的
  写法，只是当前唯一能保证两边同噪声的办法。
- **没有检查点**：`save_tree` 的“整体替换”是 H20-4；C2 里已经遇到“目录已存在就报错”。
  C6 不需要检查点，所以没有重复这个绕行；如果下一步要存 VAE 参数，等 H20-4。
- 采样用 `exp(0.5 * logvar)` 而不是单独的 `std` 头，少一个张量；这是写法选择，不影响语义。

## 测试

`test/plans/perf/showcase_c6.plan.toml`（`tier = "slow"`）跑 `--profile small`：1024 行、
200 步、batch 32，整条计划约 6 秒（主要是两次 PyTorch 导入）。数值越界即失败。

## 不提交的东西

`artifacts/`、`*.npy`、`__pycache__/` 被 `.gitignore` 排除；`RESULTS.md` 提交。
