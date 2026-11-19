# 真实优化 pass 表（Camel 写的 pass，在 `2d7c5c3a1` 原样上）

**基座**：`2d7c5c3a1` Release（build-info revision `2d7c5c3a14`，源指纹 `0244fca0cbac`），宿主 `88zjwei`，本机未编译。
**pass 全是 Camel 代码**，未改一字：`opt::cse` / `opt::fold` / `opt::dce` / `opt::saturate` 在 `stdlib/opt.cml`；
`greedy::fuse` 是 `test/cases/opt/writings/greedy_fuse.cml.in` 原样复制进 scratch 作 `greedy.cml`；
`checkpoint::ckpt` 是自己写的 `docs/handover/reports/eval-round10/probes/checkpoint.cml`。`src/`、`modules/`、`stdlib/` 未动。

## 第 2 项 · 两张展示程序上的读数

### 2a `showcase/c2-mlp-mnist/train.cml`（能跑）

数据：宿主上没有 `artifacts/`，numpy 也装不上（`pip install` 无网、卡住），所以用纯 Python 写了一份同形状数据
（`probes/gen_mlp_data.py`，固定种子，train 64 / val 16 / epochs 2，写到 `/home/zjwei/camel-work/mlp-small`）。
运行：`camel --arg data=<上面> --arg steps=2 --arg batch=16 --arg lr=0.001 --arg log_every=1 --arg threads=1 <程序> <pass>`

| pass | 接受/拒绝 | 定性 | 前后输出 |
| --- | --- | --- | --- |
| `opt::cse` | **接受**（exit 0，无诊断） | — | **一致**（除 `seconds=` 计时字段） |
| `opt::fold` | **接受** | — | **一致** |
| `opt::dce` | **接受** | — | **一致** |
| `opt::saturate` | **接受** | — | **一致** |
| `greedy::fuse` | **接受**（exit 0） | 不是保义的 pass：它把一对乘法里的第二个换成第一个，图的读数是重点，输出**应当**变 | **不同**（`step=1 loss` 2.882916447 → 2.017454732），按该 pass 的定义如此，不是缺陷 |
| `checkpoint::ckpt` | **接受**（commit 未改图，模型里 `:struct/copy_arr` 计数 0） | — | **一致** |

基线输出（7 行）：`c2 init val_loss=2.626463508 val_accuracy=0.125000000` / `step=0 loss=2.593523677` /
`step=1 loss=2.882916447` / `trained … val_loss=2.490763096 val_accuracy=0.062500000` / `checkpoint …` / `leaves …`。
比较时把 `seconds=<数>` 抹掉（每次运行都会变），其余逐字节相同。

**规范文档读数**（同一基座 `2d7c5c3a1` / `2d7c5c3a14` clean，同一 `--arg`，每个一次进程）：

| 图 | 字节 | md5 |
| --- | --- | --- |
| 基线 | 161678 | `ba142fd682a19b0e67ff7efd0e8aa932` |
| `opt::cse` | 159368 | `0faf22f2f4af40d07d7c53c2a0f787f8` |
| `opt::fold` | 188237 | `21ac5b825af8b29a975a212d9dd7c64f` |
| `opt::dce` | 161678 | `ba142fd682a19b0e67ff7efd0e8aa932`（**未改图**） |
| `opt::saturate` | 162945 | `f1f0f4d12dc603352357358f4da8a299` |

`opt::dce` 的文档与基线逐字节相同 ⇒ **未改图**；`cse` / `fold` / `saturate` 三个都改了图。基线连跑两次 md5 相同。
命令：`camel --arg data=… --arg steps=2 --arg batch=16 --arg lr=0.001 --arg log_every=1 --arg threads=1 showcase/c2-mlp-mnist/train.cml <pass> std::jgir canonical=true`

### 2b `showcase/c3-char-transformer/train.cml`（跑不起来）

**程序在定版上跑不起来，与 pass 无关**，原文：

```
error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: jvp has no forward transform for this function.
   --> /home/zjwei/camel-work/eval/out/latest/stdlib/vjp.cml:121:25
 121 |     let forward = split(jvp(f))
```

`mode=train` 与 `mode=forward` 都停在同一处，而诊断指的**不是**展示程序的第 121 行，是 `vjp` 的宏体：
`--> /home/zjwei/camel-work/eval/out/latest/stdlib/vjp.cml:121:25`，原文

```
 121 |     let forward = split(jvp(f))
     |                         ^^^
```

（`stdlib/vjp.cml:121`，在 `#macro export func vjp::<F>` 里，先于任何 `mode` 分支；
`showcase/c3-char-transformer/train.cml:121` 是 `wq2: float[...],`，与 `jvp` 无关。）
加 `std::macro` 改成在宏求值里报同一件事（`MacroEvaluationFailed: … jvp has no forward transform for this function`）。
⇒ **表里这一列的「前后输出」一律「未运行」**，只报图形读数是硬伤最小的一步：

| pass | 接受/拒绝 | 规范文档 |
| --- | --- | --- |
| 基线 | — | 274056 字节，md5 `01c271ddc3e880eba5cd6203f8e5b2b4` |
| `opt::cse` | **接受** | 271186 字节，**变了** |
| `opt::fold` | **接受** | 323843 字节，**变了** |
| `opt::dce` | **接受** | 274056 字节，**未变**（该图上 dce 没有可删的） |
| `opt::saturate` | **接受** | 274285 字节，**变了** |

**没有拒绝**要定性：两个程序上四个 Camel pass 全部接受。唯一被拒的 `std::cxx::inline` 属第 1 项（O 车道），
本页只记一句：已归类为「介于 (ii) 与 (i)」，根因是 `:struct/copy_arr` 未声明访问（`accesses_declared:false`，
读法把它当成 `global` 的 write），见 `item4-inline-refusal-classification.md`。

## 第 3 项 · 带真实编辑的 commit 墙钟

`opt::cse` 在 mlp 上（24 张图）：**9 个新进程**，每个 `camel <args> train.cml opt::cse std::jgir canonical=true --time-phases=…`，
取 `phases[]` 里 `name=pass, detail=opt::cse` 的 `wall_ns`：

```
n=9  ms = 122.3 122.5 123.6 124.3 125.1 125.4 125.8 126.0 126.8
median 125.1 ms, min 122.3, max 126.8（极差 4.5 ms）
```

**负载**：跑前 `/proc/loadavg` = `21.82 21.72 23.97`，跑后 `21.82 21.72 23.97`（同机再测 `21.79 21.71 23.94`）——
**不是安静机器**，绝对数不可与静机读数相比，只能与同一负载下的其它读数相比。
（`--time-phases` 的 `pass` 相含该 pass 本身的 walk 与 commit；`std::jgir` 另计 3.3 ms。）