# C3：字符级 Transformer，用库层 `grad` 训练

官方示例里第一个**真正需要反向传播**的模型：两层、多头注意力、`softmax`、`layer_norm`、
残差连接、嵌入查表、因果掩码，字符级。求导走的是**库层**的 `vjp`/`grad`
（`stdlib/vjp.cml` 里用 Camel 写出来的 `transpose(split(reinterp<jvp>(f)))`），
不是正在退役的引擎侧 `with_grad`/`buildGradientGraph`。

这个示例的产出**不是一条收敛曲线，而是墙**：库层 `grad` 今天能微分什么、在哪个构造上停下、
停下时诊断怎么说、要补什么才能继续。每条墙在 [`WALLS.cn.md`](WALLS.cn.md) 里一行，附最小程序。
模型本身按作者要求写满（不为了跑通而缩），所以它在第一个库层 `vjp` 调用处停下是**读数**，
不是缺陷。

## 目录

```
showcase/c3-char-transformer/
  train.cml                 模型与训练循环（库层 vjp 求参数树的梯度，nn.adam 更新）
  corpus.txt                语料，96 行 / 4278 字节 / 28 字符字母表，入库
  make_corpus.py            语料的确定性生成器（重跑得到同一份字节）
  probe.py                  跑 probes/ 下每个最小程序，逐字打印库层答了还是拒了
  probes/                   十二个最小程序，一个构造一个；见 WALLS.cn.md
  block_forward.cml         最小注意力块（六参数）的前向，打印一个数
  block_library_grad.cml    同一个块，走库层 vjp
  block_engine_grad.cml     同一个块，走引擎 with_grad（对照，不是本示例的路径）
  baseline/block.py         同一个块的 numpy 版：解析雅可比 + 有限差分自查
  compare.py                跑上面四边、比较、写 RESULTS.md；任何一项超界以非 0 退出
  RESULTS.md                最近一次 compare.py 的结果（脚本生成，勿手改）
  WALLS.cn.md               墙清单：每条的分类、诊断、最小程序、要补什么
  README.cn.md              本文件
  README.md                 英文版
```

## 模型

`train.cml` 按作者 2026-11-02 14:00 的原文写：

- **嵌入查表**：`take(m.emb, window)`，`emb` 是 `[V, D]`，一个窗口取出每位置的 D 个数；
- **位置**：`+ m.pos`，每位置一行；
- **两层 block**，每层：
  - `layer_norm` → **多头注意力**：`q = x wq + bq`，`reshape` 成 `[T, H, Dh]`、`permute` 成
    `[H, T, Dh]`，`scores = q @ transpose(k) / sqrt(Dh)`，加**因果掩码**（下三角 0、上三角
    -1000），`softmax(scores, 2)`，`@ v`，再 `permute`/`reshape` 合头，输出投影 → **残差**；
  - `layer_norm` → 两层 MLP（`relu`）→ **残差**；
- **head**：`layer_norm` → `wout` → 每个位置的 logits；
- **损失**：`nn.softmax_cross_entropy`，目标是窗口右移一位的字符；
- **更新**：一次 `nn.adam`。

参数树是一个 `struct` 的 37 个张量（两层写开而不嵌套，因为结构体的切空间还没有实现——
见 WALLS 第 9 条）。损失把模型当**第一个参数**：`window_loss(m, window, targets)`，
于是 `vjp(window_loss)(m, ...)` 的 pullback 回答模型类型的余切，
`adam(m, pullback(1.0).0, opt, lr)` 直接吃它，程序里没有一处按名字数叶子。

## 数据

语料是**入库**的 `corpus.txt`：96 行、4278 字节，字母表 `' abcdefghijklmnopqrstuvwxyz.'`。
`make_corpus.py` 是它的生成器（八个模板乘六组词，固定种子的线性同余生成器），重跑得到同一份
字节，所以语料不靠信任、靠重算。没下载、没有版权问题：示例要证的是梯度，不是文本。

## 运行

`block_forward.cml`、`block_engine_grad.cml` 不需要参数：

```powershell
camel showcase\c3-char-transformer\block_forward.cml
camel showcase\c3-char-transformer\block_engine_grad.cml
```

`train.cml` 要语料路径，`mode=forward` 只算前向（打印初始损失），`mode=train` 进训练循环
（会调库层 `vjp`）：

```powershell
camel showcase\c3-char-transformer\train.cml --arg corpus=showcase/c3-char-transformer/corpus.txt --arg mode=forward --arg steps=0
camel showcase\c3-char-transformer\train.cml --arg corpus=showcase/c3-char-transformer/corpus.txt --arg mode=train --arg steps=20
```

比较（推荐；跑四边、写表、任何一项超界以非 0 退出）：

```powershell
python showcase\c3-char-transformer\compare.py
```

墙清单的复现（跑十二个最小程序，逐条打印库层答了还是拒了）：

```powershell
python showcase\c3-char-transformer\probe.py
```

## 读数怎么读

- `RESULTS.md` 的 **forward** 行：同一个块、同一组权重，Camel（f32）对 numpy（f64），容差
  `1e-6` 相对。这一行证明模型与六个参数是解析式描述的那个块。
- **gradient (engine with_grad)** 行：引擎答的六个偏导对解析雅可比，容差 `1e-4` 相对
  （f32、一次 softmax 三次矩阵乘）。这是对照：引擎能算，说明缺的是**库层的规则**，不是程序。
- **gradient (library vjp)** 行：库层 `vjp` 对这六个参数的回答。今天它答不出来，
  这一行写的是拒绝的原话；分类见 `WALLS.cn.md`。若哪天库层开始给数，这一行会拿同样的
  `1e-4` 去比，而不是继续写"拒绝"。
- `baseline/block.py` 的解析雅可比用同一文件的中心差分自查（步长 `1e-6`），
  自查误差打在同一份输出里（记录值为 `4.8e-10`），所以"手推的雅可比"不是一句声明。

小规模配置已进计划 `test/plans/perf/showcase_c3.plan.toml`（`tier = "milestone"`）：
它跑 `probe.py`、`train.cml` 的前向、训练循环的拒绝、以及 `compare.py` 的对表，几秒完成。
milestone 层不进默认 `npm run test` 的 fast 层；`--tier milestone` 或直接点名这个文件会跑它。

## 不入库的东西

`block.json`、`probes.json`、`*.npy`、`artifacts/`、`__pycache__/` 在
`showcase/c3-char-transformer/.gitignore` 里。`RESULTS.md` 与 `WALLS.cn.md` 入库，
它们是这个示例的产出。