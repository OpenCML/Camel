# C2：MLP 训练 MNIST

官方示例第一批（H13）的第一个示例。同一个任务写两遍：一遍是 Camel（`train.cml`），
一遍是社区惯用写法的 PyTorch（`baseline/train_torch.py`）。两边读同一份数据、同一份
初始权重，训练同样的 784 → 128 → 10 的 MLP（ReLU、Adam、softmax 交叉熵），在同样的
验证集上评估，并用同一套容差比较结果。所有数字由 `compare.py` 生成，见
[RESULTS.md](RESULTS.md)。

## 目录

```
showcase/c2-mlp-mnist/
  train.cml                Camel 实现（展示品：参数树、value_and_grad、loop、save_tree）
  baseline/train_torch.py  PyTorch 对照（nn.Linear、F.cross_entropy、torch.optim.Adam、state_dict）
  make_data.py             数据与初始权重的生成脚本（MNIST 或确定性的合成替身）
  compare.py               运行两边、比较、度量、写 RESULTS.md；比较失败时以非 0 退出
  RESULTS.md               最近一次 `--profile standard` 的结果（由 compare.py 生成，勿手改）
  README.cn.md             本文件
  README.md                英文版
  artifacts/               生成的数据、检查点（已 gitignore，不入库）
```

## 数据

`make_data.py` 写出一份两边共用的数据与初始权重：

- `train_images.npy` / `val_images.npy`：`(N, 784)` 的 float32，像素值在 `[0, 1]`；
- `train_targets.npy` / `val_targets.npy`：one-hot 目标（`nn.softmax_cross_entropy` 要的写法）；
- `train_labels.npy` / `val_labels.npy`：类别下标（PyTorch 的 `F.cross_entropy` 要的写法）；
- `init/w1.npy`、`init/b1.npy`、`init/w2.npy`、`init/b2.npy`：He 正态初始化的权重与零偏置；
- `dataset.id`：数据内容的身份（训练数据、目标的 SHA-256 前 16 位加规模）；Camel 端把它写进
  检查点，读回时 `save_tree`/`load_tree` 会核对，换一份数据就读不回来；
- `manifest.json`：来源、规模、种子、每个文件的 SHA-256。

**MNIST 不会从网络下载。** 仓库缓存里有 `mnist.npz` 时用真实 MNIST（`--mnist` 指路径，
或 `--source auto` 让脚本在仓库根的 `.cache/mnist/mnist.npz` 找）；找不到时，脚本生成
一份**确定性的合成替身**并在 `manifest.json` 里写明来源是 `synthetic`：每个类在 28×28
网格上有自己的一组亮矩形（粗略的笔画），每个样本整体平移几个像素并加高斯噪声，按固定
种子打乱。它只是让展示机制（数据、训练、评估、检查点、比较）在没有数据集时也能跑通；
C2 要证明的是语言与示例机制，不是数据集本身。CI 的小规模配置固定用合成替身，所以每个
机器上的结果一致。

## 运行

比较（推荐；会自动准备数据、清掉旧检查点、跑两边并写表）：

```powershell
# 小规模：合成数据，200 步，约 5 秒（含 PyTorch 导入）
& D:\Projects\Camel\benchmarks\.venv\Scripts\python.exe showcase\c2-mlp-mnist\compare.py --profile small

# 报告里的标准配置：仓库缓存有 MNIST 时用它，否则合成替身
& D:\Projects\Camel\benchmarks\.venv\Scripts\python.exe showcase\c2-mlp-mnist\compare.py --profile standard `
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

**重复运行前要删掉 `data` 下的 `checkpoint` 目录**：`save_tree` 拒绝覆盖已存在的目录（见
下面“遇到的问题”）。`compare.py` 每次都会先删。

单独跑 PyTorch 对照：

```powershell
& D:\Projects\Camel\benchmarks\.venv\Scripts\python.exe showcase\c2-mlp-mnist\baseline\train_torch.py `
    --data showcase/c2-mlp-mnist/artifacts/small --steps 200 --batch 50 --lr 0.001 --log-every 20 --threads 1
```

两个程序打印同一套机器可读的行（`c2 config`、`c2 init`、`c2 step=<i> loss=<v>`、
`c2 trained`、`c2 checkpoint`），`compare.py` 只解析这些行。

## 结果与容差

`RESULTS.md` 里由脚本生成三张表：

1. **Agreement**：初始验证损失、每个记录步的训练损失（取相对差最大的一步）、最终验证损失、
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
记录步的训练损失相对 `1e-2`、最终验证损失相对 `1e-2`、最终验证准确率绝对 `1e-2`（一个
百分点）。检查点一行不是两边对比，而是各自必须原样读回（漂移为 0）。标准配置下实际最坏的
一步约为容差的 0.55 倍，见 `RESULTS.md`；`small` 配置（合成替身）的差在 `1e-5` 量级。

## 并排对照的要点

- **参数就是类型**：Camel 的 `type Mlp = { w1, b1, w2, b2 }` 同时是初始权重、梯度和优化器
  状态的类型；`value_and_grad(loss)(m, x, y)` 直接给出同形状的梯度树，`adam(m, g, state, lr)`
  直接吃这棵树，程序里没有一处按名字数叶子。PyTorch 要 `nn.Module` + `optimizer` 对象 +
  `state_dict` 三套东西来分别表达同一件事。
- **训练是表达式**：整个训练是一次 `loop`，状态（模型、优化器状态、步数）显式列出，不需要
  把 step 函数、日志和状态更新拆成几个函数。PyTorch 是惯用的 `for` 循环加 `zero_grad`/
  `backward`/`step` 三件套；两边读起来都清楚，这里 Camel 少一层“优化器对象”。
- **副作用是显式的**：循环体写成 `sync { ... }`，日志按书写顺序执行；PyTorch 的 `print` 本来
  就按顺序。Camel 这一层显式声明在“图上的循环默认可并行/可跳过”的语义下是必要的，但写训练
  循环时是额外的噪声。
- **数据管线**：两边都直接用 `numpy` 落盘的 `.npy`。Camel 用 `slice(t, 0, start, start+batch)`
  取连续的小批；数据脚本先打乱，所以每个 epoch 的顺序固定。PyTorch 侧同样切片，写起来更短。
- **短的地方**：检查点（`save_tree` 一棵树 vs `torch.save` 一个 `state_dict`）、评估（一次
  `argmax`/`sum` vs `no_grad` + `.item()`）、初始化（四个 `load_npy` vs 逐层 `copy_`）Camel
  都更短；`loss` 和 `model` 两段 PyTorch 更短，因为 `nn.Linear` 把偏置和形状藏起来了。
- **代价**：这份实现里 Camel 的吞吐约为 PyTorch 的 0.8 倍（标准配置，单线程，见
  `RESULTS.md`），峰值内存约为其 0.35 倍；编译时间约 0.07 秒，PyTorch 是 eager、没有这一项。
  性能不是这个示例要证明的东西，但数字照实报。

## 遇到的问题

写这个示例时真正别扭的地方。每条都附了问题，按交接任务书第 4 节的格式写进
`docs/handover/reports/H13.md` 的“待决问题”。

1. **`save_tree` 不能覆盖检查点，而 Camel 没有办法删目录。** `save_tree` 的签名注释说
   “The directory is replaced as a whole or not at all”，实现却在目录已存在时直接报
   `checkpoint already exists`（`modules/tree/src/checkpoint_io.cpp`）。一个训练程序第二次
   运行就写不出检查点；`fs` 模块有 `exists`、`mkdir`，甚至有覆盖写文件的 `replace`，却没有
   删除目录/文件的操作，所以程序自己无法恢复。`compare.py` 从 Python 侧删目录绕过，但纯
   Camel 用户没有等价办法。
   **问**：是注释对（应当整体替换、失败时保留旧目录），还是实现对（拒绝覆盖）？如果是后者，
   是否要给 `save_tree` 一个 `replace` 变体（像 `fs.replace` 那样），或给 `fs` 补一个删除
   操作？
   **已解决（H20）**：按注释实现——`save_tree` 整体替换，旧目录在成功前保留、失败时放回；
   `fs::remove`、`fs::remove_dir` 返回 `T!`。见 `docs/handover/reports/H20.md` 的 `## H20-4`。
2. **张量没有按行取子集/重排的算子，数据管线只能在语言外打乱。** 取小批只有
   `slice(t, axis, start, end)`，`t[i]` 取的是单个标量；没有 `gather`/`index_select`/按索引
   数组取行。于是“每个 epoch 重新打乱”在 Camel 里写不出来，示例只能让 `make_data.py` 先
   打乱一次、训练时取连续切片。对一个训练示例来说这是明显的缺口。
   **问**：近期是否计划加按索引取行（或数组版 `map` + 张量索引）的算子？在它出现之前，
   官方示例是否接受“数据脚本预打乱”的做法？
3. **交叉熵只接受 one-hot 目标，而数据管线通常只有类别下标。** `nn.softmax_cross_entropy`
   要 `(N, 10)` 的 one-hot 张量；MNIST 的原生标注是 `(N,)` 的整数。为了同一份数据两边都能用，
   `make_data.py` 同时写了 one-hot 和下标两份（一份给 Camel，一份给 PyTorch），数据目录因此
   多了一份冗余。Camel 里从下标造 one-hot 要用 `range(0,10) == label` 再 `reshape`/`concat`
   逐行拼，既慢又长（旧的 `test/cases/modules/nn/mnist.cml` 就是这么写的）。
   **问**：是否计划让交叉熵接受类别下标（像 `F.cross_entropy`），或提供 `one_hot` 算子？这
   属于 `nn` 的 API 还是语言层的数据表示问题？
4. **记录损失曲线要付出额外的循环状态或显式打印。** `loop` 的值只有最终状态，想把每个记录
   步的损失带回循环外，要么把 `float[]` 加进状态元组，要么在循环体里 `println` 后由外部
   解析（本示例选了后者，`compare.py` 解析 stdout）。这不是错误，但对“训练曲线”这种常见
   需求，语言里没有一个顺手的“收集副产物”的写法；设计原则第 15 节也没有覆盖。
   **问**：这是预期用法（日志就是副作用，曲线由外部工具读），还是应当有更直接的表达？

## 测试

上面四条已由检查意见立项：取行/标签/键式随机数与 `save_tree` 整体替换在 H20，下标切片、
lambda 推断与“对第一个参数求导”在 H21。本示例保留现有写法（`with <m: Mlp>`、
`slice`、one-hot 目标、Python 侧删检查点目录），等 H20-4 与 H21-3 落地后按
`value_and_grad(loss)(m, x, y)`、`take`/`permutation`、整数标签整体改写；报告 H13 记了
这个顺序。检查意见要求的反引号插值与 `let (value, gradient) = ...` 解构已经改好。

小规模配置已进计划 `test/plans/perf/showcase_c2.plan.toml`（`tier = "slow"`）：它用合成替身
跑 200 步、跑两边、检查上面那组容差，约 6 秒。H10 的 fast/slow 分层已经落地，默认的
`npm run test` 跳过 slow 层，`npm run test:all`（或 `--tier all`）会跑到它。

## 不入库的东西

`artifacts/`（数据、检查点）、`*.npy`、`*.pt` 都在 `showcase/c2-mlp-mnist/.gitignore` 里；
仓库根的 `tmp/` 也已 gitignore。`RESULTS.md` 只有几十行文本，入库，方便对照。