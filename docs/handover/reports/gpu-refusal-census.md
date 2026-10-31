# `#gpu` 拒绝普查：22 条，该修 22 / 该拒 0

状态：**普查稿（只判不修）**。本页不改任何一行实现，只把设备路径上"作用域为什么被拒"的算子
一条条摆出来、按作者 06:10 那句判成两堆。数字每次都重数（第 5 节的命令），本页是快照。

## 1. 判据（照抄作者 06:10 的裁定）

> **"判断一条拒绝该不该写成'目标不具备'，只问一句：语言规定这个目标必须有吗？核心类型和核心
> 运算，每个后端都必须有；只有对运行时本身的内省，可以随目标不同而缺席。"**
> （`docs/handover/reviews/questions-2026-10-31-a-version-port-that-must-carry-a-value.md:141`）

两句话给出两侧：

- **该修**：核心类型或核心运算，而目标上**这种值已经存在**；
- **该拒**：只可能是"对运行时本身的内省"——它对 `#gpu` 这条边界意味着"问设备自己"，
  不意味着"算一个张量"。

这一页判的是**算子面**：一个 `#gpu` 作用域因为哪个算子而不成立。判的不是"理由的措辞"，
措辞那一层是另一件事（`graph_regions.cpp#gpuRefusal` 的六种句子），这里不混进来。

## 2. 这一堆为什么会聚在一起

26 条这个数曾经在账上（`docs/handover/reports/gpu-g4-g5-gap-list.md:126`）。它聚在一起的原因
只有一条：**它们都是"设备上还没有实现"的算子**——不在 `operations()` 里，于是 `eligibility`
答 `unsupported_operator`，`gpuRefusal` 把它说成 `the module provides none`，整个 `#gpu`
作用域被拒，节点退回主机。

**而今天这一堆是 22 条，不是 26。** 差在四条布局算子上：`broadcast_like`、`permute`、
`reshape`、`transpose` 已经进了 `operations()`（`graph_regions.cpp:100-106`，注释里写着它们
不算、程序就是核心自己的索引实例给出的地址），所以它们不再出现在缺口里。这不是"四条被修好了
所以少四条"之外的任何东西——**26 → 22 的算式是 17 + 13 − 4，而 17 这个数今天是 13**：

| 程序 | 今天的读数 |
| --- | --- |
| `benchmarks/transformer/model.cml` | `TILES_CENSUS ops=22 device=9 gap=13`（账上是 `ops=22 device=5 gap=17`） |
| `showcase/c2-mlp-mnist/train.cml` | `TILES_CENSUS ops=21 device=8 gap=13` |

两个程序各 13 条缺口，共用 4 条（`tensor:matmul_grad_lhs`、`tensor:matmul_grad_rhs`、
`tensor:numel`、`tensor:shape`），所以今天的一堆是 **13 + 13 − 4 = 22**。
`device` 从 5 涨到 9 就是那四条布局算子：它们换了地方，从缺口那一栏换到了有实现那一栏。

## 3. 两堆的数

| 堆 | 条数 |
| --- | ---: |
| **该修** | **22** |
| **该拒** | **0** |

**该拒 0 不是一个空话。** 作者那句只给"目标不具备"留了一个合法的入口：对运行时本身的内省。
这条边界上没有这样的算子——`device()` 是有的（它把卡的描述交出来），`std::mem::blocks()` 那
一类在设备路径上不出现。所以这一堆 22 条全部落在"该修"：它们算的值**目标上都有**
（`f32` 张量、`i64` 位置），缺的是写法，不是能力。

## 4. 该修那一堆的构成（按族）

| 族 | 条数 | 算子 | 一句话 |
| --- | ---: | --- | --- |
| **数学：激活与归一化** | **6** | `gelu`、`gelu_grad`、`layer_norm`、`layer_norm_grad`、`softmax`、`softmax_grad` | 逐元素与整行归约的组合，`f32` 上已经有 `exp`/`log`/`sqrt`/`sum_axis`/`mean_axis`/`divide`，缺的是这几条把已有算子串起来的写法 |
| 训练（`nn` 模块自己的 kernel 表） | 5 | `nn:sgd`、`nn:adam`、`nn:adam_state`、`nn:softmax_cross_entropy_labels`、`nn:softmax_cross_entropy_labels_grad` | 判据按排除法判"该修"（只有内省可以缺席），但**修法不同**：不是 `tiles` 的 lowering，是 `nn` 模块那张 kernel 表要不要给设备一个入口 |
| 索引与形状读取 | 4 | `tensor:take`、`tensor:argmax_axis`、`tensor:numel`、`tensor:shape` | 读的是值自己的形状与元素个数（不是运行时内省），取值走的是已有索引契约 |
| 微分（核心算子的反向） | 3 | `tensor:matmul_grad_lhs`、`tensor:matmul_grad_rhs`、`tensor:permute_inverse` | 派生图上的算子：前向 `matmul` 在设备上有，反向没有 |
| 比较 | 2 | `tensor:gt`、`tensor:strict_eq` | 核心运算里的比较，主机半边已有，设备缺 |
| 创建与转换 | 2 | `tensor:zeros_like`、`tensor:cast_like` | 同一个元素类型上的创建与复制 |

**最大的族：数学（激活与归一化）6 条。** 它不是"一个算子没写"，是一个组合缺写法：
六条里有四条（`gelu_grad`、`layer_norm_grad`、`softmax_grad` 与 `layer_norm`）共用同一组
归约模式，写一条的代价和写六条的代价不是线性的。

**没有未判的族。** 22 条逐条都落在判据能判的一侧，这一页没有"原样打印、不折进任何一堆"的
条目。`nn` 那一族是本页唯一一处判得勉强的（判据只给了"核心"与"内省"两个名字，模块自己的
kernel 表两个都不叫），所以它的"修法不同"单独写在表里，不让它混进 `tiles` 的 lowering 账。

## 5. 怎么重数（数字带着它的算式）

```bash
# 两个程序各跑一次 census（std::macro 必须先于 tiles::census）
CAMEL_PACKAGES=benchmarks/common camel benchmarks/transformer/model.cml std::macro tiles::census
camel showcase/c2-mlp-mnist/train.cml std::macro tiles::census
```

读数就是上面第 2 节那两行 `TILES_CENSUS`；`TILES_CENSUS_GAP <name>` 一行一个缺口名；两个
清单取并集就是这一堆。本页的两个读数取自 `lane-gpubf16@0274b4f8b` 之后的一棵树，与账上的
`device=5 gap=17` 不同，原因见第 2 节。

**一条判据（进页）**：**拒绝普查的第一产出不是数字，是"这一堆为什么会聚在一起"。**

## 6. 这一页堵着谁

- **`tiles` 的下一笔**：最大的族是数学（6 条），它们共用归约模式，一条的写法能带出四条；
- **`nn` 的 kernel 表**：5 条判了该修、但不在 `tiles` 的账上，要另一条车道认领；
- **账本第 126 行那句"26 这个数"**：今天读数是 22，算式与差值写在第 2 节，账本那行按本页改。