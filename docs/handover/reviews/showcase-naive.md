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

## 第二轮（决策者 2026-10-22）

通过：环境变量与 `set_num_threads` 去掉了，`sqrt(d_head)` 写出来了，参数个数有了名字，`one_hot` 等离散值的导数自动为零、不再需要 `stop_gradient`（设计原则 9.9）。还有两处：

1. **形状写成了数字**：`type TokenStates = Tensor<f32, [8, 16, 64]>` 把 `batch`、`sequence`、`d_model` 的值抄进了类型——改一个超参数要改十几处类型，读者也看不出 8 是批大小。旗舰示例要的是 `f32[b, t, d]` 这样带维度名的形状。若原生多维数组的写法（H17 第 2、4 项）还没到，就用维度名的 `Tensor<f32, [b, t, d]>`（5.4 维度名在形参里自动绑定）；两者都写不出时，作为语言缺口在报告里写清，不用数字代替。
2. **`blocks: (Block, Block)` 用元组**：层数是配置，应当是 `Block[]`，前向用 `loop h = x for b in m.blocks do …`。若数组的元素是带张量的结构体时有限制，写清是什么限制并作为缺口处理。`import { sqrt as scalar_sqrt } from math` 说明 `tensor` 与 `math` 各导出一个 `sqrt`、导入时相撞（10.2）。数组与张量统一（H17）之后，逐元素的数学函数只应有一份：`math::sqrt` 对标量与数值数组都成立（数组上配 `#impl` 内核），`tensor` 不再另导出同名函数；`exp`、`log` 等同理。
