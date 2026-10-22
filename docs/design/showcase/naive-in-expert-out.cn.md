# 杀手级示例："朴素写法进，专家写法出"

状态：已定（决策者 2026-10-22，负责人交办）。属于官方示例计划（同目录 README）的旗舰，也是规则系统的头号证据。

## 1. 要证明什么

用户按教科书写一个 Transformer 训练步——不懂数值稳定的写法，不知道该调哪个融合算子。Camel 的规则与按目标抽取**自动**把它变成专家会手写的样子，并且：

1. **每一处改写都可解释**：`explain` 给出改写链，用户看得到"为什么这样改"；
2. **每一条规则都由用户可读、可改、可扩展**：规则是 Camel 代码，在标准库里，按数值许可门控，入库前经 `check`；
3. **数值上更安全**：朴素写法在 bf16 下溢出成 NaN 的地方，Camel 的结果没有 NaN、误差更小；
4. **速度上不输专家写法**。

一句话的卖点：**写得像教科书，跑得像专家，而且每一步都说得清。**

## 2. 程序（朴素写法，示意）

```camel
func attention(x: f32[t, d], w: Attn): f32[t, d] {
    let q = x @ w.q
    let k = x @ w.k
    let v = x @ w.v
    let s = (q @ k->transpose) / sqrt(float(d))
    let p = exp(s) / exp(s)->sum<axis: -1>          // 朴素 softmax：大 logit 时溢出
    return (p @ v) @ w.o
}

func loss(logits: f32[t, c], y: int[t]): f32 {
    let p = exp(logits) / exp(logits)->sum<axis: -1>
    return -(log(p)[range(0, t), y])->mean            // log(softmax)：数值不稳定
}
```

再加 RMSNorm、MLP、Adam 与 EMA，组成一个 nanoGPT 规模的训练步。源码只此一份，不写任何"优化提示"。

## 3. 规则（标准库，Camel 写）

| 规则集 | 内容 | 依赖 |
|---|---|---|
| `rules::accuracy`（`refine_accuracy` 许可） | `exp(s)/sum(exp(s))` → 减去最大值的稳定 softmax；`log(softmax(x))` → `log_softmax(x)`（logsumexp）；`log(1 + x)` → `log1p(x)`；`log(1 + exp(x))` → 稳定的 softplus | 规则语言第二版 8.10 |
| `rules::fusion`（多根模式） | `(x @ wq, x @ wk, x @ wv)` → 一次 `x @ concat(wq, wk, wv)` 再切分；缩放折进权重；softmax 与其前后的逐元素运算成为一个融合组 | v2 第 3、4 步 |
| `rules::algebra` | 已有的代数化简，清理自动微分生成的反向图 | v2 第 1、2 步 |

抽取：GPU 上用 Tiles 的代价模型，按目标支持抽取（v2 8.9）。

## 4. 对照与度量

四种实现跑同一任务（相同初始化、相同数据、相同步数）：

| 实现 | 说明 |
|---|---|
| A. PyTorch 朴素 | 与 Camel 源码逐行对应的写法 |
| B. PyTorch 专家 | `F.log_softmax`、`F.scaled_dot_product_attention`、融合的 QKV 线性层——社区推荐写法 |
| C. `torch.compile(A)` | Inductor 对朴素写法能做到什么 |
| D. Camel（朴素源码 + 规则流水线） | 本示例 |

度量：

1. **数值**：fp32 与 bf16 下，大 logit 输入时是否出现 NaN/inf；与 fp64 参考的误差（按元素的 ulp 或相对误差分布）；训练曲线是否发散。
2. **速度**：每步时间、峰值内存（3090；CPU 另报）。遵守归档仓库 AGENTS.md 的测量纪律（交错、钉核、配对 bootstrap）。
3. **透明**：`explain` 对每处改写的输出，原样附在示例的 README 里。
4. **代码**：Camel 源码与 B 的行数与可读性对照。

## 5. 先做杀死搜索

主张"自动"之前，先查对手已经自动做到了什么，并实际运行：

- **Inductor 的模式匹配**：它有把朴素注意力替换为 SDPA 的模式（`fuse_attention` 一类）与 `log_softmax` 的识别吗？对 C 实测。
- **XLA 的代数化简**、**JAX** 的 `jax.nn` 稳定实现、**TVM/Relay** 的模式。
- **Herbie** 对 softmax、log-sum-exp 的改写。

若 C 已经自动得到 B 的形状，主张就落在第 1 节的 1、2 两点（可解释、用户可读可扩展、许可门控），而不是"别人做不到"。这一节的结论写进示例 README。

## 6. 交付

- `examples/models/naive-in-expert-out/`：Camel 程序、PyTorch 的 A/B/C、运行与度量脚本、README（结果表、`explain` 输出、杀死搜索的结论）。
- 规则集 `rules::accuracy`、`rules::fusion` 进标准库，经 `check`。
- 测量的原始数据与环境元数据另存归档仓库（由论文负责方取用）。
