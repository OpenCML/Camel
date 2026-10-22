# 评审：杀手级示例的第一版（决策者 2026-10-22）

度量框架、PyTorch 三种对照、fp64 参考与精度扫描做得扎实，通过。**Camel 程序是旗舰示例的门面，按标准库写法准则返工**（它展示的是"朴素"的**算法**，不是朴素的 Camel 写法）：

1. **配置走环境变量**（`get_env('NAIVE_BENCH')`、`NAIVE_INIT_DIR`、`NAIVE_FINAL_DIR`）：违反"设置不放在环境变量里"。改为 `main` 的参数（`--arg bench=true`）。`set_num_threads(1)` 是运行配置，交给命令行 `-t 1`，不写在程序里。
2. **形状不可见**：全用 `Tensor`。旗舰示例要展示原生多维数组：`x: f32[b, t, d]`、`wq: f32[d, d]`，维度名在签名里一眼可见（H17 已合入的部分能写多少写多少，写不了的记为缺口）。
3. **两个块写死成 `block0`、`block1`**：模型是 `blocks: Block[]`，前向是 `loop h = x for b in m.blocks do block_forward(b, h)`。
4. **`/ 4.0`** 是隐藏的 `sqrt(d_head)`：写出来。
5. **`cast(oh, 'float32')` 用字符串指定类型**：用类型（`f32(oh)` 或 `oh as f32[…]`）；若做不到是语言缺口。
6. **参数个数的长算式塞在 `println` 的插值里**：提到一个有名字的函数（或由 `tree_reduce` 算出），输出只打印值。
7. 朴素的**算法**保留（`exp/sum` 的 softmax、`log(p)` 的损失），这是示例的意义所在。

**README 列出的缺口，转为任务**：
- **张量元素类型只有 f32/i64/bool，f64 在创建时被悄悄降成 f32**：这是数值语义的缺陷，不只是缺口——静默降精度违反数值契约（第 7 节）。补齐 `f64`、`bf16`、`f16` 的元素类型（CPU 与 GPU；`bf16`/`f16` 的计算按 `change_precision` 许可），创建与转换不得静默改变类型。立为 **H238，优先**。
- **`one_hot` 没有 VJP、需要 `stop_gradient`**：设计原则 9.9 规定离散值的导数为零，`one_hot` 的输入是整数，它的回传应自动为零，不需要用户写 `stop_gradient`。修正，并检查其他以离散值为输入的算子（`argmax`、比较、下标的位置参数）。
- QKV 融合、`rules::accuracy`、`explain`：已在 H232 的依赖里，到位后接上。
