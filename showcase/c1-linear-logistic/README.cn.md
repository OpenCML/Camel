# C1 — 线性回归与逻辑回归

这个示例展示最小的一整套训练：张量、`with_grad`、`do` 循环、参数树上的 SGD。同一个
程序解决两个问题，只换损失函数：

- 线性回归：`y = 2*x1 - 1.5*x2 + 0.5`，加少量噪声；
- 逻辑回归：两团高斯点，用 `sigmoid(x @ w + b)` 分类。

C2 展示的是“参数树 + 优化器状态 + 检查点”，C1 故意更小：模型只有 `w`、`b` 两个张量，
优化器就是 `nn.sgd`，让读者先看清“梯度 → 更新 → 下一轮”这条线。

## 目录

```
showcase/c1-linear-logistic/
├── train.cml                 Camel 程序（两个问题共用一个训练循环）
├── baseline/train_torch.py   PyTorch 对照（nn.Linear + SGD）
├── make_data.py              合成数据与零初始化权重
├── compare.py                跑两边、比数值、量时间/内存、写 RESULTS.md
├── RESULTS.md                standard 配置的实测结果（脚本生成，不手工改）
├── README.cn.md
└── README.md
```

## 数据

`make_data.py` 只用 NumPy，按固定种子生成：

- `linear/`：512 行训练、128 行验证，`train_x/val_x` 是 `(N, 2) f32`，`train_y/val_y` 是
  `(N, 1) f32`；
- `logistic/`：同样的形状，标签是 0/1；两团点的中心在 `(-1, -1)` 与 `(1.2, 1.2)`；
- 两个模型都从 `w = 0`、`b = 0` 开始，初始损失完全由数据决定，两边读同一份 `.npy`。

不下载任何数据，也不提交 `.npy`（`.gitignore` 排除 `artifacts/`）。

## 运行

```powershell
python showcase/c1-linear-logistic/make_data.py --out showcase/c1-linear-logistic/artifacts/small
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/c1-linear-logistic/train.cml --arg model=linear `
    --arg data=showcase/c1-linear-logistic/artifacts/small/linear `
    --arg steps=200 --arg batch=32 --arg lr=0.1 --arg log_every=20 --arg threads=1 `
    std::macro std::nvm

python showcase/c1-linear-logistic/baseline/train_torch.py `
    --data showcase/c1-linear-logistic/artifacts/small/linear --model linear `
    --steps 200 --batch 32 --lr 0.1 --log_every 20 --threads 1
```

`compare.py` 把上面两步串起来，并额外跑 `logistic`：

```powershell
python showcase/c1-linear-logistic/compare.py --profile small
python showcase/c1-linear-logistic/compare.py --profile standard   # 写 RESULTS.md 的那一档
```

## 结果（standard，1000 步，单线程）

| 问题 | 初始损失差 | 记录步最坏差 | 最终验证损失差 | 最终指标 |
|---|---|---|---|---|
| 线性回归 | 5.8e-08 | 1.1e-06 | 3.4e-07 | MAE 0.043818 vs 0.043818 |
| 逻辑回归 | 0 | 1.2e-02（容差 2e-02） | 6.5e-04 | 准确率 0.9922 vs 0.9922 |

训练时间：线性 0.0054 s vs 0.119 s，逻辑 0.0082 s vs 0.128 s；峰值内存 8.0/11.0 MiB vs
273.2/273.9 MiB；编译 0.034 s。完整表格（含吞吐、代码行数）在 `RESULTS.md`。

**不要把这个吞吐差外推**：模型只有 3 个参数、数据是 512×2，PyTorch 的时间主要是每步的
框架调度与 autograd 开销，Camel 的时间主要是解释器循环；这组数字说明的是“小模型上框架
开销占主导”，不是“Camel 的张量内核更快”。C2 的 MLP（784→128→10）才反映计算量上来之后
的对比，那里 Camel 慢 17–21%。

## 容差为什么是这些数

- 初始损失只差在点积的累加顺序与偏置加法，`1e-05` 相对；
- 每一步的损失经过优化器放大：逻辑回归在 `lr = 0.05` 下，单步 1e-7 量级的差在几十步内
  可放大到 1e-2，因此记录步用 `2e-02` 相对（实测最坏 1.2e-02）；
- 最终损失用 `1e-02` 相对，最终指标（MAE / 准确率）用 `1e-02` 绝对；
- 两边不是逐位相等，也不可能：设计原则 7 只对同一后端的同一算法承诺逐位相等。

`compare.py` 在任一界限被越过时以非零码退出，所以计划项失败就是真的数值不一致。

## 并排看

| 事情 | Camel | PyTorch |
|---|---|---|
| 模型 | `type Model = { w, b: Tensor }` | `nn.Linear(2, 1)` |
| 损失 | 手写公式，`mean` 在全量上归约 | `F.mse_loss` / `F.binary_cross_entropy_with_logits` |
| 梯度 | `with_grad(loss)<m>(x, y, kind)` | `value.backward()` |
| 更新 | `sgd(m, gradient, lr)`，返回同型参数树 | `optimizer.step()` |
| 循环 | `loop m = initial, step = 0 while ... do sync { ... }` | `for step in range(steps)` |
| 打印 | 反引号插值 `` `c1 step=${step} loss=${value:.9f}` `` | f-string |

### 求导的写法

设计原则 9.9（H21 第 3 项已撤回，见 H25）：权重是 `with` 参数，
`with_grad(loss)<m>(x, y)` 对第一个 `with` 参数求导，返回与 `m` 同型的梯度；其余
`with` 参数是常量，普通参数是数据。本示例把 `with_grad(loss)` 绑定在循环外，循环内
带权重调用。

## 遇到的问题

- 没有新增阻塞。C1 只用已经具备的张量、`grad`、`sgd`、`do` 循环；C2 报告里的
  `save_tree`、`take`、交叉熵目标等缺口这里都用不到。
- `sgd` 直接接受参数树，和 `adam` 一样，不需要逐叶子更新；这是本示例最舒服的一处。
- 逻辑回归的损失用 `relu(z) - z*y + log(1 + exp(-|z|))` 写成稳定形式；`exp(log(...))`
  的误差比 PyTorch 的 `log1p` 路径大，是记录步容差要比 C2 宽一倍的原因之一。这不是
  语言问题，是两个实现选择了不同的数值路径。

## 测试

`test/plans/perf/showcase_c1.plan.toml`（`tier = "milestone"`）跑 `--profile small`：512 行数据、
200 步、batch 32，全流程约 5 秒（含两次 PyTorch 导入）。任一数值界限被越过时计划失败。

## 不提交的东西

`artifacts/`、`*.npy`、`__pycache__/` 由 `.gitignore` 排除；`RESULTS.md` 提交，因为它是
脚本生成的、可复核的实测记录。
