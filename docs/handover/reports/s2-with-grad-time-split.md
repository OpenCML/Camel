# `with_grad` 的一步花在哪：C2 standard 档

2026-11-01，树上 `e49c8860b`。给论文与 M4 各一份基线：C2 的 standard 档（10000 train / 2000 val，
1000 步，batch 100，`threads=1`）跑一次，把**一步**拆成前向、反向、优化器三块。

## 读数

| 块 | 1000 步 | 每步 | 占这一步 |
| --- | ---: | ---: | ---: |
| 前向（`matmul`、`take`、`softmax_cross_entropy_labels`、`add`、`relu` 等） | 0.343 s | 0.343 ms | 34.6% |
| 反向（`matmul_grad_lhs`/`_rhs`、`softmax_cross_entropy_labels_grad`、`sum_to`、`gt`、`multiply`） | 0.485 s | 0.485 ms | 48.9% |
| 优化器（`nn:adam`） | 0.168 s | 0.168 ms | 16.9% |
| **三块合计** | **0.996 s** | **0.996 ms** | **100.4%** |
| 循环自报（`c2 trained … seconds=`） | 0.992 s | 0.992 ms | — |

**三块相加比循环自报多 0.4%（4 ms），在 5% 以内。** 三次运行的形状一致：前向 33–37%、反向 47–50%、
优化器 17–18%；最不稳的是 `take`（25–77 ms，取一批行受内存与争抢影响）和整个循环的绝对秒数
（0.932–1.102 s），占比不受影响。

墙钟一句：**反向约是一步的一半，前向约三分之一，优化器约六分之一**；反向里两块矩阵乘法
（`matmul_grad_lhs` 0.232 s + `matmul_grad_rhs` 0.207 s）占 0.44 s，是整步的 44%。

## 怎么量的

`std::profile json=<file>` 的 `operator_rows`：每个算子一条 `{uri, calls, ns}`，`step.wall_per_round_ns`
乘 `step.rounds` 是分母（与程序自己打印的 `seconds=` 差 1 ms 以内）。**训练循环一行没改**——测的就是它
自己；`train.cml` 只在跑法上多了一个 `std::profile`。

按算子分块，不按调用点分：`with_grad(loss)` 一次调用里同时算前向和反向，调用点切不开，所以块来自
“这一步在算什么”。`multiply`、`gt` 每步各一次，前向里没有对应位置，计入反向；这处判定不确定量 2%（0.04 s），不改变三块之和。

**不计入**的是循环外的算子：`load_npy_f32` 0.055 s、`ckpt:save_tree` 0.006 s、`ckpt:load_tree` 0.002 s、
初始化（`nn:adam_state`、`:mark/filter_arr`、`load_npy_i64`）0.002 s，合计 0.064 s——它们不进分母也不进
分子。前向里还含三次评估的 `matmul`/`relu`/`argmax_axis`（0.3 ms，0.03%），未再拆。

## 复现

```bash
python showcase/c2-mlp-mnist/compare.py --profile standard --data /tmp/c2 --results /tmp/c2/RESULTS.md
camel -t 1 showcase/c2-mlp-mnist/train.cml --arg data=/tmp/c2 --arg steps=1000 --arg batch=100 \
      --arg lr=0.001 --arg log_every=100 --arg threads=1 \
      std::macro 'std::profile json=/tmp/c2/profile.json' std::nvm
```

```python
import json
rows = {x['uri']: x['ns'] / 1e6 for x in json.load(open('/tmp/c2/profile.json'))['operator_rows']}
fwd = ['tensor:matmul', 'tensor:take', 'nn:softmax_cross_entropy_labels', 'tensor:add', 'tensor:relu']
bwd = ['tensor:matmul_grad_lhs', 'tensor:matmul_grad_rhs',
       'nn:softmax_cross_entropy_labels_grad', 'tensor:sum_to', 'tensor:gt', 'tensor:multiply']
print(sum(rows.get(k, 0) for k in fwd), sum(rows.get(k, 0) for k in bwd), rows.get('nn:adam', 0))
```

## M4 用这份基线

1. 换库层 `vjp`/`grad` 之后，重跑上面两条命令，比的是**同一张表**里的三块，不是整步的秒数——反向减弱、
   前向不变才是换对了。
2. 反向的上界就是今天这一步的 0.485 s（`matmul_grad_*` 0.44 s 在里面）；`std::profile` 的
   `operator_rows` 是逐算子的，M4 引入的新算子会自己出现在表里，不必改这份测量。
3. `plan.cn.md` 的归因规矩同样适用：块要给读数，不写“变快了”。
