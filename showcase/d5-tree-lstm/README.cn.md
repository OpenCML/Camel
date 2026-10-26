# D5 — Tree-LSTM 情感分类

四片叶子的句法树：叶子是词，内部结点用 Tree-LSTM 组合两个孩子，根结点的隐状态过线性层
得到“正面/负面”。这个示例专门展示 **`enum` 树上的递归**：`forward` 递归调用自己，树是
`enum` 值，而 `with_grad` 仍然给出整棵参数树的梯度。

标签不是四个词极性的简单求和，而是 **左子树与右子树的极性异或**——同样四个词，分组不同
标签就不同，所以“词袋”模型做不对，必须真的按树组合。

## 目录

```
showcase/d5-tree-lstm/
├── train.cml                 Camel 程序（递归 Tree-LSTM）
├── baseline/train_torch.py   PyTorch 对照（同样的递归）
├── make_data.py              合成树、词向量初始化、one-hot 标签
├── compare.py                跑两边、比数值、量时间/内存、写 RESULTS.md
├── RESULTS.md                standard 配置的实测结果（脚本生成）
├── README.cn.md
└── README.md
```

## 数据

- 每句固定 4 片叶子，词表 8；词 id < 4 为负向，≥ 4 为正向；
- 标签 = `(左子树极性 > 0) XOR (右子树极性 > 0)`；
- 256 句训练、64 句验证，固定种子；
- 每片叶子写成 one-hot `[4, 8]`，两边直接 `onehot @ emb` 取词向量；**没有用**
  `take`/行索引（还未实现），所以数据按 one-hot 存，取词是矩阵乘。

模型参数 762 个，8 维隐藏。

## 运行

```powershell
python showcase/d5-tree-lstm/make_data.py --out showcase/d5-tree-lstm/artifacts/small
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/d5-tree-lstm/train.cml --arg data=showcase/d5-tree-lstm/artifacts/small `
    --arg steps=200 --arg lr=0.01 --arg log_every=20 --arg threads=1 std::macro std::nvm
python showcase/d5-tree-lstm/compare.py --profile small
python showcase/d5-tree-lstm/compare.py --profile standard   # 写 RESULTS.md
```

## 结果（standard，1000 步，每步一句）

| 指标 | Camel | PyTorch | 差 |
|---|---|---|---|
| 初始验证准确率 | 0.609375 | 0.609375 | 0 |
| 记录步最坏损失（第 800 步） | 0.000076483 | 0.000076529 | 6.0e-04 相对 |
| 最终验证准确率 | 1.000000 | 1.000000 | 0 |

训练时间 0.264 s vs 2.759 s；峰值内存 19.5 MiB vs 274.6 MiB；编译 0.032 s。完整表格在
`RESULTS.md`。

**性能说明**：每步只有一棵 4 叶树、762 个参数；PyTorch 的时间主要是 Python 递归与每步
autograd 的开销，Camel 的递归在 C++ 图里执行。0.096 的比值说明的是小模型/深递归上的
框架开销，不是内核速度。C2 的 MLP 才是计算量占主导的对照。

## 容差

- 初始准确率是离散计数，理论上两边必须完全一致（实测差 0），界限仍写 1e-02 绝对；
- 记录步损失 1e-02 相对（实测最坏 6e-4）；
- 最终准确率 1e-02 绝对（实测都 1.0）；
- 不逐位相等：设计原则 7。

## 并排看

| 事情 | Camel | PyTorch |
|---|---|---|
| 树 | `type Tree = enum { Leaf(Tensor), Node(Tree, Tree) }` | 嵌套 tuple，叶子是 `Tensor` |
| 递归 | `forward<p>(left)` / `forward<p>(right)` | `self.forward(left)` / `right` |
| 组合 | 五个门的手写公式 | 同一个公式 |
| 损失 | `softmax_cross_entropy(logits, onehot)` | `F.cross_entropy(logits, ids)` |
| 循环 | `loop p, opt, step ... do sync { ... }` | `for step in range(...)` |

### 为什么每步只放一句

树是变长的，批处理要么补齐成固定形状，要么用行索引/`take` 把多棵树拼起来；后者是
行索引与下标切片才有的。这里每步处理一棵树，把“递归 + 自动微分”写清楚，吞吐不是目标。
下标切片 `t[idx]` 落地后，可以按批把多棵树拼成一个数组，训练循环的形状不变。

### 求导的写法

设计原则 9.9：`with <p: Params>` 是最终形态，
`with_grad(loss)<p>(tree, target)` 对整棵参数树求导，返回同型的梯度树；树的输入是数据。
本示例把 `with_grad(loss)` 绑定在循环外。

## 遇到的问题

- **递归本身可用**：`enum` 树上递归调用带 `with` 参数的函数、再对参数求导，工作正常；
  这是 D5 想验证的核心，没有阻塞。
- **没有 `take`/行索引**：用 one-hot 乘词向量表绕开按行取词；`take` 落地之后可以
  直接存词 id。见 README 的数据一节。
- **变长树没有批处理**：见上，下标切片落地后可以补；当前每步一句，报告里如实写了吞吐。
- 内部结点的输入取两个孩子隐状态之和（child-sum），没有为内部结点单独加参数；这是
  Tree-LSTM 的常见简化，不影响“递归 + 门控”的展示。

## 测试

`test/plans/perf/showcase_d5.plan.toml`（`tier = "milestone"`）跑 `--profile small`：256 句、
200 步，整条计划约 6 秒（主要是两次 PyTorch 导入）。数值越界即失败。

## 不提交的东西

`artifacts/`、`*.npy`、`__pycache__/` 被 `.gitignore` 排除；`RESULTS.md` 提交。
