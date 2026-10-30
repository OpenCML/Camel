# GPU 支持面：`tiles::census` 的缺口账本

这是 G4 的"缺口为 0 才算完成"（`docs/handover/reports/gpu-g4-g5-plan.md` G4 第 1 条）的唯一账本。数由遍
`tiles::census` 读出：程序可达图里的面内算子（`tensor` 的数组与 `nn` 的参数树）按
`modules/tiles/src/graph_regions.cpp#operations` 那张表分成"有 device 实现"与"缺口"两组，逐条打印名字。

## 怎么取数

两条命令，`std::macro` 写在前面，派生图的算子才算得进来（少了它，`grad`/`with_grad` 还没展开）：

- `camel benchmarks/transformer/model.cml std::macro tiles::census`，要 `CAMEL_PACKAGES=<repo>/benchmarks/common`
  （`model.cml` 从那里导入 `camel_bench`）
- `camel showcase/c2-mlp-mnist/train.cml std::macro tiles::census`

这个遍只读程序与表，不碰设备：`CUDA_VISIBLE_DEVICES=-1` 下读数相同。所以**下面的数与本机有没有空闲卡无关**。

## `transformer_step`（`benchmarks/transformer/model.cml`）：22 = 5 + 17

有 device 实现（5）：`tensor:add`、`tensor:matmul`、`tensor:mean`、`tensor:multiply`、`tensor:sum_to`。

缺口（17），按类分：

- 视图与形状（4）：`tensor:permute`、`tensor:permute_inverse`、`tensor:reshape`、`tensor:transpose`
- 规范化（6）：`tensor:softmax`、`tensor:softmax_grad`、`tensor:layer_norm`、`tensor:layer_norm_grad`、`tensor:gelu`、`tensor:gelu_grad`
- 反向（2）：`tensor:matmul_grad_lhs`、`tensor:matmul_grad_rhs`
- 形状与索引查询（2）：`tensor:shape`、`tensor:numel`
- 逐元素补齐（2）：`tensor:broadcast_like`、`tensor:zeros_like`
- 优化器（1）：`nn:sgd`

不加 `std::macro` 时是 12 = 4 + 8：有实现的是 add/matmul/mean/multiply，缺口 8 条正是
`nn:sgd`、`tensor:gelu`、`tensor:layer_norm`、`tensor:permute`、`tensor:reshape`、`tensor:shape`、
`tensor:softmax`、`tensor:transpose`——计划里那句"表外至少 8 个（三个视图名、`softmax`、`gelu`、
`layer_norm`、`cross_entropy`、`cast`）"就是这一行；展开之后的 17 是同一批算子加上它们的 vjp。

## C2（`showcase/c2-mlp-mnist/train.cml`）：21 = 8 + 13

有 device 实现（8）：`tensor:abs`、`tensor:add`、`tensor:matmul`、`tensor:multiply`、`tensor:relu`、
`tensor:subtract`、`tensor:sum`、`tensor:sum_to`。

缺口（13），按类分：

- 损失（2）：`nn:softmax_cross_entropy_labels`、`nn:softmax_cross_entropy_labels_grad`
- 优化器（2）：`nn:adam`、`nn:adam_state`
- 反向（2）：`tensor:matmul_grad_lhs`、`tensor:matmul_grad_rhs`
- 索引（2）：`tensor:take`、`tensor:argmax_axis`
- 比较（2）：`tensor:gt`、`tensor:strict_eq`
- 形状与索引查询（2）：`tensor:shape`、`tensor:numel`
- 类型（1）：`tensor:cast_like`

## 缺的是哪一半：名字已列，性质未查

17 条与 13 条都是**已注册**的算子（它们出现在能编、能跑、在主机上算得出数的图里），所以缺的是设备的
那一半，不是注册。但每一条究竟只是"表里少一个名字 + 少一个 device 程序"，还是"连语义声明或下标映射都
还没写"，**这一笔没有逐条查**。查法在 `modules/tiles/src/graph_regions.cpp#eligibility`：它把拒绝分成
`unsupported_operator` 与 `semantic_declaration`、`unsupported_index_contract` 两类，下一笔按条落时
先用那条路把 30 条各自归组，再决定补表、补语义还是补下标映射。

## 怎么读这张账本

- 两张表的缺口都清空，G4 才允许记完成；这份文件只记读数，一个算子都不在这里补。
- 表本身今天 20 个名字，全在 `tensor:`；`nn:` 的算子一个都没有 device 实现，所以 C2 的损失与优化器整段在缺口里。
- 账本是快照，不是断言：表长一个名字、程序多一个算子，数就变；重跑上面两条命令即可。