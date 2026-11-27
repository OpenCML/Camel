# "形状合法而数值错"这一类：收口页

来源：`task-327` 与后续四轮探测（`lane-ssa-residue`，2026-11-28 01:1x–01:4x）。所有读数标 `[provenance] tip=…`；本页只记"这一类怎么判、修了哪四条、哪条不该修"。

## 判据（这一页的唯一结论）

**参数里装不下轴身份 ⇒ 能静默错数 ⇒ 拒绝；信息不在输入里 ⇒ 拦不了 ⇒ 写文档。**

- **能检出**：形状里带着能判的信息（例如 `[2,2,2]` 上 `[2]` 同时吻合轴 0/1/2）⇒ "多轴等长"是可机械检出的歧义 ⇒ 拒绝并点名候选轴，**拒绝可以，错数不行**。
- **不能检出**：布局约定（池化的 NCHW）—— `dim(1)` 是 C 还是 H 任何检查都推不出来 ⇒ 拒绝只会误拒合法 NCHW ⇒ **不为让检查变绿改语义**，写文档。
- **不属这一类**：参数里本来就装得下轴身份（`permute`/`transpose`/`*_axis_stated`），或行主序是唯一读法（`reshape`，"用错原语"而非静默错数），或秩被钉死因而读法穷尽（`conv` 的四个入口都 `rank == 4`）。

## 台账

| 原语 | 参数里有轴身份？ | 能静默错数？ | 处置 | 读数来源 |
|---|---|---|---|---|
| `sum_to(t, at)` | 否（`at` 只有长度） | 是：`[2,2,2]`+`[2]` 得 `[16,20]`，按通道应为 `[14,22]` | **已修**：`rank>=3` 且 target 与多于一根轴相等 ⇒ 拒绝并点名候选轴 | `545e563565`、`99d8ea189c`（`BUILD_EXIT=0`，探针 `total 1/pass 0/fail 1`＝故意错期望以打印实际值） |
| `broadcast_like(t, like)` | 否（同理） | 是：`[10,20]`→`[2,2,2]` 落最后轴，按通道应沿 `C` 展开 | **已修**：同一 `broadcastStrides` | 同上两笔 |
| `batch_norm(input, mean, var, gamma, beta)` | 否（参数 `[C]`） | 是：`[2,2,2]` 按 `[N,I,C]`、`I == C`、参数 `[2]` ⇒ 8 值中 6 个不同 | **已修**：`rank>=3` 且 `[C]` 与多于一根轴相等 ⇒ 拒绝（`axes 1 or 2`） | `fe7e81e9ec`（`feat/modules/nn`+`nn_laws` `total 87/pass 86/fail 1`，唯一 fail 是探针；`vjp-parity --strict` PASS `stale=0`） |
| `layer_norm(x, gamma, beta)` | 否（尾随 extents） | 是：`[2,3,2]`+`gamma=[2]` ⇒ 12 值中 10 个不同（3 个 `-1` 巧合相同掩盖） | **已修**：`rank>=3` 且尾随起点多于一处 ⇒ 拒绝（`from 0 or from 2`） | `1458e2e380`（同 run；rank-2 与"尾随起点唯一"的 rank-3 仍逐值正确） |
| `softmax` / `log_softmax` | **是**（`axis?: int`，默认 `-1`） | 能，但属"省略默认值" | **文档提醒**，不立卡 | `signatures/tensor.cml:298`、`:313` |
| `permute` / `transpose` / `sum_axis_stated` / `mean_axis_stated` / `max_axis_stated` / `mean` | 是 | 不能 | 不可能 | `tensor.cml:262`、`:283`、`:296`、`:137`、`:281` |
| `reshape` | 否，但行主序是唯一读法 | 不能（"用错原语"，轴序重排属 `permute`） | 文档一句 | `signatures/tensor.cml:120` |
| `embedding` / `gelu` / `one_hot` | 是（`[V,D]` 秩 2 固定 / 逐元素 / 新开轴） | 不能 | 不可能 | `nn.cml:154`、`tensor.cml:102`、`tensor.cml:153` |
| `conv2d` 族 | — | 不能（四个入口都钉 `rank == 4`） | 不可能 | `conv.cpp:213 :529 :551 :575 :612` |
| `max_pool2d` / `avg_pool2d` | **没有形状参数**（布局写死在下标里：`H=dim(2)`、`W=dim(3)`） | 能（传 `[N,H,W,C]` 形状合法、数值错），但**信息不在输入里 ⇒ 拦不了** | **文档**：NCHW 已写在 docstring（`nn.cml:93` 的 `[N, C, OH, OW]`、`:106` 的 `// [N, C, H/2, W/2]`）与报错句（`conv.cpp:584` `pool2d expects NCHW input`）；**不立卡、不加检查、不动秩门槛、不动 `count_include_pad`** | `conv.cpp:582-596`、`:351-392` |
| `rms_norm` / `group_norm` / `attention` / `dropout` / `pad` / `crop` / `silu` | — | — | **不存在**（签名零命中） | — |

## 顺带弄清的两条边界

- **`conv.cpp:656` 不是 conv**：那是 `bnTrainGeometry`（batch-norm 的 N/C/inner 几何，`C = dim(1)`、`d>=2` 乘进 `inner`、`p->dim(0)==C` 才过）。`task-327` 卡里那处引用是错的。
- **`sum_to` 的静默窗口只有一个**：`[3,2,2]` 上 `[3]`（"只对得上一根轴但不是后缀"）**今天已拒**（`requireSuffixTarget` 逐位比较：`position 2 is 3 where the tensor has 2`），走不到"落最后轴"。

## 交接给下一人的两句

1. **先把秩/形状门槛数清，再谈读法** —— `conv` 是"秩钉死 4 ⇒ 读法穷尽"，池化是"秩钉死 4 但布局不在输入里"。同一个"秩钉死"导向两种完全不同的处置。
2. **诊断位置在 `.pb` 载入的程序上是 `?:?`** —— 库级拒绝只能给形状与轴；要可定位得让 `.pb` 载入器带来源信息（截稿后的事）。