# C2：MLP 训练 MNIST

官方示例第一批（H13）的第一个示例。同一个任务写两遍：一遍是 Camel（`train.cml`），
一遍是社区惯用写法的 PyTorch（`baseline/train_torch.py`）。两边读同一份数据、同一份
初始权重，训练同样的 784 → 128 → 10 的 MLP（ReLU、Adam、softmax 交叉熵），在同样的
验证集上评估，并用同一套容差比较结果。所有数字由 `compare.py` 生成，见
[RESULTS.md](RESULTS.md)。

## 目录

```
showcase/c2-mlp-mnist/
  train.cml                Camel 实现（展示品：参数树、with_grad、take 取样、loop 收集曲线、save_tree）
  baseline/train_torch.py  PyTorch 对照（nn.Linear、F.cross_entropy、torch.optim.Adam、state_dict）
  make_data.py             数据与初始权重的生成脚本（MNIST 或确定性的合成替身）
  compare.py               运行两边、比较、度量、写 RESULTS.md；比较失败时以非 0 退出
  RESULTS.md               最近一次 `--profile standard` 的结果（由 compare.py 生成，勿手改）
  README.cn.md             本文件
  README.md                英文版
  artifacts/               生成的数据、检查点（已 gitignore，不入库）
```

## 数据

`make_data.py` 写出一份两边共用的数据与初始权重。默认落在
`showcase/c2-mlp-mnist/artifacts/<profile>/`（gitignored；`--data <dir>` 可换位置），缺失或
轮数不够时 `compare.py` 会在运行前重新生成。只想生成数据、不跑比较：
`python showcase/c2-mlp-mnist/make_data.py --out <dir>`。

- `train_images.npy` / `val_images.npy`：`(N, 784)` 的 float32，像素值在 `[0, 1]`；
- `train_labels.npy` / `val_labels.npy`：int64 类别下标，`F.cross_entropy` 与
  `nn.softmax_cross_entropy` 都直接接受这种写法；
- `train_order.npy`：一个 epoch 接一个 epoch 的置换，平铺存放。两边的批采样器都走这份
  顺序，所以同一个 step 在两边看到的是同一批行；
- `init/w1.npy`、`init/b1.npy`、`init/w2.npy`、`init/b2.npy`：He 正态初始化的权重与零偏置；
- `dataset.id`：数据内容的身份（训练图像与标签的 SHA-256 前 16 位加规模）；Camel 端把它
  写进检查点，读回时 `save_tree`/`load_tree` 会核对，换一份数据就读不回来。批顺序不在
  其中，所以改 epoch 数不会让旧检查点失效；
- `manifest.json`：来源、规模、种子、每个文件的 SHA-256。

`train_order.npy` 至少要覆盖 `ceil(steps / (train / batch))` 个 epoch；`compare.py` 会把
这个数传给 `make_data.py`（后者默认 10）。手写 `train.cml` 时若步数超过顺序覆盖的范围，
读取批的 `slice` 会失败。

**MNIST 不会从网络下载。** 仓库缓存里有 `mnist.npz` 时用真实 MNIST（`--mnist` 指路径，
或 `--source auto` 让脚本在仓库根的 `.cache/mnist/mnist.npz` 找）；找不到时，脚本生成
一份**确定性的合成替身**并在 `manifest.json` 里写明来源是 `synthetic`：每个类在 28×28
网格上有自己的一组亮矩形（粗略的笔画），每个样本整体平移几个像素并加高斯噪声，按固定
种子打乱。它只是让展示机制（数据、训练、评估、检查点、比较）在没有数据集时也能跑通；
C2 要证明的是语言与示例机制，不是数据集本身。CI 的小规模配置固定用合成替身，所以每个
机器上的结果一致。

## 运行

比较（推荐；会自动准备数据、跑两边并写表）。要求 `PATH` 上的 `python` 装有
`numpy`、`torch`、`psutil`，并且 `PATH` 上有 Camel 二进制：

```powershell
# 小规模：合成数据，200 步，约 5 秒（含 PyTorch 导入）
python showcase\c2-mlp-mnist\compare.py --profile small

# 报告里的标准配置：仓库缓存有 MNIST 时用它，否则合成替身
python showcase\c2-mlp-mnist\compare.py --profile standard `
    --mnist D:\Projects\Camel\.cache\mnist\mnist.npz
```

`--profile`：`small`（1000/200，200 步，batch 50）、`standard`（10000/2000，1000 步，
batch 100）、`full`（60000/10000，3000 步，batch 128）。其余参数（`--steps`、`--batch`、
`--lr`、`--log-every`、`--threads`、`--seed`、`--source`）可覆盖配置。

单独跑 Camel（先 `$env:CAMEL_HOME = "out\latest"` 并把 `out\latest\bin` 放进 `PATH`）：

```powershell
camel showcase\c2-mlp-mnist\train.cml `
    --arg data=showcase/c2-mlp-mnist/artifacts/small --arg steps=200 --arg batch=50 `
    --arg lr=0.001 --arg log_every=20 --arg threads=1 `
    std::macro std::nvm
```

**第二次运行不需要清理**：`save_tree` 整体替换检查点目录（见下面“遇到的问题”）。

单独跑 PyTorch 对照：

```powershell
python showcase\c2-mlp-mnist\baseline\train_torch.py `
    --data showcase/c2-mlp-mnist/artifacts/small --steps 200 --batch 50 --lr 0.001 --log-every 20 --threads 1
```

两个程序打印同一套机器可读的行（`c2 config`、`c2 init`、`c2 step=<i> loss=<v>`、
`c2 trained`、`c2 checkpoint`），`compare.py` 只解析这些行。

## 结果与容差

`RESULTS.md` 里由脚本生成三张表：

1. **Agreement**：初始验证损失、每个记录步的训练损失（取缩放差最大的一步）、最终验证损失、
   最终验证准确率、两边各自的检查点往返漂移；每行给出差值与容差，任何一项超界
   `compare.py` 以非 0 退出。小规模计划用同一套断言，所以示例不会悄悄坏掉。
2. **Performance**：训练时间（只量训练循环，数据加载/评估/检查点不计）、吞吐（`steps ×
   batch / 时间`）、整个进程的峰值工作集（psutil 采样，含子进程）、Camel 的编译时间
   （`std::macro std::stats std::null` 三次的中位数）、以及含启动的整进程墙钟时间。
3. **Code size**：按两边共用的 `// == section ==` / `# == section ==` 分段统计 SLOC
   （空行、整行注释、Python docstring、C 块注释不计；带行尾注释的代码行计一次）。

**容差的依据（设计原则第 7 节）**：逐位相同只承诺给“同一后端上的规范算法”。这两个程序算
同样的 f32 公式，但矩阵乘的累加顺序、Adam 偏置修正的写法不同，而且差异会随优化步数累积，
所以这是一次**有界误差**的比较：初始损失相对 `1e-5`（同一份权重与数据，一次前向）、每个
记录步的训练损失 `1e-2`、最终验证损失相对 `1e-2`、最终验证准确率绝对 `1e-2`（一个
百分点）。记录步的训练损失可能接近零，这时纯相对界量到的是舍入噪声而不是分歧，所以这一行
用带单位下限的差 `|a - b| / max(1, |a|, |b|)`，表里报的也是这个缩放后的值。标准配置下最坏
的是第 900 步，缩放差 `1.46e-3`，界是 `1e-2`，见 `RESULTS.md`；`small` 配置（合成替身）
的差在 `2.4e-4` 量级。

## 并排对照的要点

- **参数就是类型**：Camel 的 `type Mlp = { w1, b1, w2, b2 }` 同时是初始权重、梯度和优化器
  状态的类型；`with_grad(loss)<m>(x, y)` 直接给出同形状的梯度树，`adam(m, g, state, lr)`
  直接吃这棵树，程序里没有一处按名字数叶子。PyTorch 要 `nn.Module` + `optimizer` 对象 +
  `state_dict` 三套东西来分别表达同一件事。
- **训练是表达式**：整个训练是一次 `loop`，状态（模型、优化器状态、步数、损失曲线）显式
  列出，不需要把 step 函数、日志和状态更新拆成几个函数。循环体是纯的——曲线收进
  `float[]`，不在循环里打印——所以计时的区域正好是训练本身，循环也不需要 `sync`。
  PyTorch 是惯用的 `for` 循环加 `zero_grad`/`backward`/`step`，在循环里打印。
- **副作用是显式的**：收集到的曲线由 `report` 事后打印；它是一个 `sync` 函数，`foreach`
  按步序执行。PyTorch 的 `print` 本来就按顺序；Camel 里唯一写的地方被标了出来。
- **数据管线**：两边读同一批 `.npy`，也走同一份 `train_order.npy`（每个 epoch 一个置换）。
  Camel 用 `take(train_x, slice(order, 0, offset, offset + batch))` 取一小批，标签同样取；
  PyTorch 用同一份 order 做下标。`take` 产生拷贝（任意行序不是视图），Camel 为 gather
  付费，PyTorch 的高级下标也一样。两边都不在训练中打乱：顺序是数据的一部分，所以同一个
  step 在两边看到同一批行。
- **标签就是原生形式**：数据里每行一个 `int64` 类别下标，`F.cross_entropy` 直接吃，
  Camel 的 `softmax_cross_entropy` 也直接吃。`load_npy` 读出的张量没有静态元素类型，所以
  Camel 端写 `cast(labels, 'int64')` 来选中整数标签那条重载（见“遇到的问题”）。
- **短的地方**：检查点（`save_tree` 一棵树 vs `torch.save` 一个 `state_dict`）、评估（一次
  `argmax`/`sum` vs `no_grad` + `.item()`）、初始化（四个 `load_npy` vs 逐层 `copy_`）Camel
  都更短；`loss` 和 `model` 两段 PyTorch 更短，因为 `nn.Linear` 把偏置和形状藏起来了。总行数
  见 `RESULTS.md`。
- **代价**：记录下来的 standard 运行里 Camel 的吞吐是 PyTorch 的 0.83 倍，峰值内存为其
  0.24 倍（74.7 vs 315.8 MiB，单线程，见 `RESULTS.md`）。同日三次测得 0.83、1.30、0.81 倍
  （中间一次 PyTorch 被别的负载拖慢），所以墙钟比值随机器负载变化，稳定的是数值一致性。
  编译时间约 0.03 秒，
  PyTorch 是 eager、没有这一项。性能不是这个示例要证明的东西，但数字照实报。

## 遇到的问题

写这个示例时真正别扭的地方；每条都附了问题，按交接任务书第 4 节的格式写进
`docs/handover/reports/H13.md` 的“待决问题”。

1. **下标与切片语法尚未实现。** 设计原则 5.4 与 H21 描述了 `t[i]`、`t[a:b]`、`t[idx]`；
   语言参考（`06_expressions.md`）仍写着切片写法保留、报 `NoMatchingFunction`。于是最自然
   的 `train_x[order[offset : offset + batch]]` 只能写成
   `take(train_x, slice(order, 0, offset, offset + batch))`。能跑，但读者要把设计里当作
   下标的东西翻译回函数调用。
   **问**：H21 的下标语法在队列里；落地之前官方示例用函数形式可以接受吗，落地后是否应当
   改回 `t[...]`？
2. **从文件读出的张量没有静态元素类型，选不中按标签分派的那条重载。**
   `nn.softmax_cross_entropy` 按目标的类型重载：浮点张量表示 one-hot 行，整数张量表示类别
   下标。`load_npy` 返回的是裸 `Tensor`，整数重载不会被选中，运行期报 “expects rank-2
   target”。示例在每次使用处写 `cast(labels, 'int64')`。技术说明
   （`docs/technical/43_rows_labels_and_keys.md` 第 5 节）把它列为未决问题；这是语言层的
   绕行，不是任务本身的性质。
   **问**：应当让 `load_npy`（或一个加载变体）返回元素类型已知的张量，还是让这一对重载在
   解析时看运行期 dtype？
3. **批顺序无法通过键式随机共享。** `random::permutation(key(seed), n)` 已经存在且是纯函数，
   但 PyTorch 对照无法复现键式置换；如果两边各自打乱，就会在不同的批上训练，逐步比较
   失去意义。示例因此把每个 epoch 的置换写进 `train_order.npy`，两边都走它；Camel 用
   `take`，不用 `permutation`。比较因此是精确的，但打乱归数据管线所有，不在训练程序里。
   **问**：有非 Camel 对照的示例，数据侧采样器是否就是预期形态？还是这类示例只比较聚合
   指标，让两边各用自己的生成器打乱？
4. **收集损失曲线能写，但读起来重。** `loop ... losses = [] as float[]` 加
   `losses->append<value>` 是已定的写法（设计原则 3.10），也能用；事后打印记录步需要
   `range(...)->filter<...>->foreach<sync ...>`，为一个 Python 里 `for` + `print` 的动作
   写六行和一个 `sync` 闭包。不是阻塞问题，记下来因为它正是示例要暴露的仪式感。
5. **`take` 拷贝行。** 打乱后的采样器需要 gather；连续的批本可以是 `slice` 视图。拷贝是每步
   的成本，不是峰值的来源：**更正（H40b）** 本示例原先写“标准配置的内存比用连续切片时高”，
   H40 查明那一轮 262 MiB 的峰值来自回收的节奏（数据块释放后取基线的顺序），与 `take` 无关；
   修正后 `take` 与连续切片 `slice` 的峰值相同（H40 第 3 节），standard 运行的峰值内存是
   74.7 MiB（`RESULTS.md`）。这是 `take` 的既定语义（5.4），不是缺陷，随其他数字一并报出。

## 测试

第一次检查提出的问题已由 H20/H21 回答，示例现在用的是答案：`save_tree` 整体替换目录
（Python 侧不再清理）、`take` 取行、`softmax_cross_entropy` 接受整数标签，也不再需要
`one_hot`。H21 第 3 项已撤回（见 H25）：`with <m: Mlp>` 与 `with_grad(loss)<m>(x, y)` 是
最终形态。本示例把 `with_grad(loss)` 绑定在循环外，循环内
`let (value, gradient) = loss_with_grad<m>(x, y)`。

小规模配置已进计划 `test/plans/perf/showcase_c2.plan.toml`（`tier = "slow"`）：它用合成替身
跑 200 步、跑两边、检查上面那组容差，几秒完成（记录的那次 3.3 秒）。H10 的 fast/slow 分层
已经落地，默认的 `npm run test` 跳过 slow 层，`npm run test:all`（或 `--tier all`）会跑到它。

## 不入库的东西

`artifacts/`（数据、检查点）、`*.npy`、`*.pt` 都在 `showcase/c2-mlp-mnist/.gitignore` 里；
仓库根的 `tmp/` 也已 gitignore。`RESULTS.md` 只有几十行文本，入库，方便对照。
