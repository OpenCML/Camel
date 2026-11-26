# resnet 全批运行的数据块空间：判类（task-77）

**结论一句话：不是"本来就该这么大"，是反向路径把块留住了 —— 同一次运行的**前向**峰值 ≤ 128 MB，
一次训练步的峰值却有 3.8 GB，差 30 倍；被拒的那块 9.0 MB 在峰值时同时活着约 420 份。**

一次运行、一台机器、一个上限梯子，两个工作负载对比得出，不涉及时序，不引用任何耗时读数。

## 1. 读数

机型：工作站 `KOGLER-WORKSTAT`，28 核，`std::macro` + `std::nvm`，`--block-limit` 逐步抬高，
每步看运行是否越过块空间。**最小的"能跑完"的上限就是该工作负载的峰值**（块空间一旦不够立即失败，
失败信息里自带"已用多少 / 这次申请多少"）。同一份 `benchmarks/resnet/model.cml`、同一份权重与输入
（`benchmarks/artifacts/resnet/`，`x[16,3,32,32] f32`，11,169,162 个参数）。

```
前向（CAMEL_BENCH_WORKLOAD 空，warmup=1 rep=1）
  limit=64   refused: asked 4.0 MB,  54.8 MB in use
  limit=96   refused: asked 4.0 MB,  74.8 MB in use
  limit=128  completed                                   <-- 前向峰值 ∈ (96, 128] MB

训练一步（CAMEL_BENCH_WORKLOAD=train）
  limit=1024  refused: asked 9.0 MB,   985.9 MB in use
  limit=2048  refused: asked 577 KB,  2041.9 MB in use
  limit=3072  refused: asked 9.0 MB,  3050.7 MB in use
  limit=3584  refused: asked 9.0 MB,  3554.8 MB in use
  limit=3776  refused: asked 9.0 MB,  3758.1 MB in use
  limit=3808  refused: asked 9.0 MB,  3767.1 MB in use
  limit=3840  completed                                  <-- 训练一步峰值 ∈ (3808, 3840] MB
```

**两步都不影响这个峰值**：`CAMEL_BENCH_WARMUP=0 REP=1`（只有一步）在 3584 仍然被拒、在 3840 仍然跑完
⇒ 这 3.8 GB 是**一步之内**同时活着的量，不是"每步留一点、多步累积"。

## 2. 判类算式

```
权重本体          11,169,162 × 4 B                 =  42.6 MB   (f32，manifest 实测)
权重梯度                                          =  42.6 MB
动量/优化器状态                                    ≈  42.6 MB
前向激活 + 权重（实测上界，limit=128 能跑完）        ≤ 128   MB
激活的梯度（与前向激活同阶）                        ≈  34   MB
──────────────────────────────────────────────────────────
一步训练"该有"的活集                              ≈ 250 MB（给 2 倍余量 500 MB）

实测一步训练峰值                                  ≈ 3.8 GB
⇒ 实测 / 该有 ≈ 7.6 倍（含 2 倍余量则 15 倍以上）
```

**另一个方向的算式（块数）**：被拒的块始终是 **9.0 MB**，峰值时"已用 3767.1 MB"
⇒ 峰值时同时活着 **≈ 419 份 9.0 MB 类块**。这个模型一步训练里同尺寸的张量个数是十几个量级
（8 个 basic block × 2 个卷积 + 残差），419 份是它的二十倍以上。

## 3. 为什么不是"本来就该这么大"

```
· 同一个模型、同一份输入，**只做前向**时峰值 ≤ 128 MB —— 模型自己的张量就那么大。
· 反向应当再加一份与前向同阶的东西（激活的梯度 + 权重梯度 ≈ 90 MB），不应当是前向的 30 倍。
· 每抬一次上限，失败点就往后挪同样多（"已用 = 上限 − 23…41 MB"），**活集追着上限长**，
  直到 3.8 GB 才够 —— 这是"有多少住多少"的形状，不是"需要固定一份"的形状。
· 峰值与步数无关（一步与两步同峰）⇒ 不是跨步泄漏，而是**一步之内反向的缓冲全活着**。
```

## 4. 量具：为什么没有 `created` / `live`

**现有量具拿不到这两个数**，这不是没量，是量不到：
```
· mm::blockBytesPeak() / blockLiveCount() / blockCreations() 三项都有，但只在两处被打印：
  ① std::gc::summary —— 是图遍（GraphIRPass），**必须在调度遍之前**跑；std::nvm 执行完图就空了，
     写在它后面的遍不会运行（camel 会给出 PassesNeverRun 的警告），所以它只能打印执行前的 0；
  ② 退出时的 [camel] stats 行（std::profile gc_stats=1）—— 有 gc / spaces 两行，**没有块那一行**。
· ⇒ 峰值改用"最小能跑完的上限"来量（本页第 1 节），这是块空间自己的失败判据，精度是一个段大小（几 MB）；
  块数改用 峰值 ÷ 被拒块大小 得到同类块的并发份数。
· 要拿到精确的 created/live，只需在退出报告里加一行（mm/profiler.cpp 的 blocks 三项已经在结构里，
  只是没有接到退出报告上）。**按 task-77 的约束"别为它加新量具"，本次没有加**；
  要不要加，交决策者定，加了三行代码就能把这三个数变成常驻读数。
```

## 5. 复现

```powershell
# 工件（首次）：生成与 referee 相同的权重与输入
python benchmarks/common/weights.py --models resnet

# 量：前向与一步训练各跑一个上限梯子（工作站本地，只取内存三数）
python test/cases/probe/resnet_block_space.py

# 单点复核：任何一档都可以手跑
$env:CAMEL_HOME = '<worktree>\out\latest'
$env:CAMEL_PACKAGES = '<worktree>\benchmarks\common'
$env:CAMEL_BENCH_WORKLOAD = 'train'
$env:CAMEL_BENCH_WARMUP = '1'; $env:CAMEL_BENCH_REPS = '1'; $env:CAMEL_BENCH_THREADS = '28'
$env:CAMEL_BENCH_WEIGHTS = '<worktree>\benchmarks\artifacts\resnet\weights'
$env:CAMEL_BENCH_INPUT   = '<worktree>\benchmarks\artifacts\resnet\input.npy'
& "$env:CAMEL_HOME\bin\camel.exe" --block-limit 3840 benchmarks\resnet\model.cml std::macro std::nvm   # 跑完
& "$env:CAMEL_HOME\bin\camel.exe" --block-limit 3808 benchmarks\resnet\model.cml std::macro std::nvm   # 被拒
```

`test/plans/probe/resnet_block_space.plan.toml` 把同一个探针包成一份计划，可走构建机；
它**不是**对错判据，只是一次内存读数，所以 `tier = fast` 但它只量不判红。

## 6. 下一步（不属于本车道，交决策者）

```
① 方向是"反向的缓冲不要全活到 step 结束"：让每个节点的梯度缓冲在它最后一次使用后就被回收/复用。
   本页只判了"有块被留住"，没有定位到具体节点；定位它需要按节点统计活块（即第 4 节那一行）。
② `lane/autodiff-grads` 那条把 `--block-limit 8192` 写进计划的改动与本文不冲突：
   8192 是"让这批运行能跑完"的护栏，本文回答的是"为什么它需要这么多"（答：不需要这么多）。
③ 任何回收策略的改动必须先拿到"按节点数活块"的读数，否则改完无法判断是回收了还是只是把峰值挪了个位置。
```

## 7. provenance

```
机型：工作站 KOGLER-WORKSTAT（28 核），非构建机；本次只取内存读数，未用于任何耗时结论
线程：CAMEL_BENCH_THREADS=28（与 referee 的默认一致：os.cpu_count()）
复现时的工作站负载：[load] busy_cores avg=2.07 max=2.64（读数为内存，与 CPU 负载无关）
树：lane/backend-speed，含探针两笔提交
```
## 8. 接上三行读数、并定位到"谁把块留到 step 结束"（task-98）

### 8.1 三行读数已接上（`std::profile gc_stats=1` 后面）

```
[camel] blocks: live=183 bytes=261578944 peak=3959326656 created=6412 freed=6229
[camel] blocks-at-peak: blocks=3161 of 6412 created
[camel] blocks-at-peak: size=9216KB blocks=258 bytes=2434793472
[camel] blocks-at-peak: size=2304KB blocks=258 bytes=608698368
[camel] blocks-at-peak: size=4608KB blocks=86  bytes=405798912
[camel] blocks-at-peak: size=512KB  blocks=354 bytes=202506240
[camel] blocks-at-peak: size=1024KB blocks=98  bytes=114032640
[camel] blocks-at-peak: size=4096KB blocks=19  bytes=79691776
[camel] blocks-at-peak: size=0KB    blocks=1993 bytes=69568448
[camel] blocks-at-peak: size=256KB  blocks=86  bytes=25362432
```
- `peak=3,959,326,656 B = 3775.6 MB`，落在 §1 用梯子夹出的 `(3808, 3840]` 区间内（差的是段粒度）⇒ **两种量法互相印证**。
- `created − freed = 6412 − 6229 = 183 = live`，与 `block.h` 写明的不变量一致。
- **峰值时 6412 个块里有 3161 个同时活着（49%）**，而运行结束时只剩 183 个 / 249 MB（≈ 那个"该有"的 250 MB）。
  ⇒ 这 3.8 GB 是**步内瞬时的**，不是泄漏到进程结束；结束时的 249 MB 正好是权重 + 梯度 + 动量的量级。

### 8.2 峰值时活着的块是什么（按大小类，与权重一一对上）

权重的尺寸（`manifest.json`，f32）与峰值的大小类逐一对上，且**每一类都是同一个倍数 86**：

```
大小类(峰值时)      份数   每份   总字节         对应张量（f32）
  9.00 MB            258   ×86   2,434,793,472  b41_c2_w / b42_c1_w / b42_c2_w  3 × 86
  2.25 MB            258   ×86     608,698,368  b31_c2_w / b32_c1_w / b32_c2_w  3 × 86
  4.50 MB             86   ×86     405,798,912  b41_c1_w                         1 × 86
  0.56 MB            354  ≈4×86    202,506,240  128×128×3×3 的四个             ≈4 × 86
  1.12 MB             98   1×86    114,032,640  b31_c1_w                         1 × 86
  4.00 MB             19   —        79,691,776  16×64×32×32 f32 激活（不是 86 倍）
  <256 KB           1993   —        69,568,448  形状/下标/小标量
⇒ **每一个权重的梯度在峰值时都存在 86 份**（2 步 × 43 个权重/偏置；43 与 `tensor:load_npy_f32` 的 43 次调用一致）。
```

### 8.3 谁在造这些块（`std::profile report=true` 的按算子表，同一次运行）

```
cpu  tensor:add            3489 次调用   2997 个块载荷   3.49 GiB    ← 梯度累加
cpu  tensor:zeros_like     3444 次调用   2952 个块载荷   3.41 GiB    ← 梯度缓冲
cpu  nn:conv2d_kernel_grad   40 次调用     40 个块载荷   85.14 MiB
cpu  nn:conv2d               60 次调用     60 个块载荷  112.50 MiB
cpu  nn:conv2d_input_grad    40 次调用     40 个块载荷   81.38 MiB
整次运行：6411 个块载荷，7.55 GiB
```
**`tensor:add` + `tensor:zeros_like` 两个算子占了 5949/6411 = 93% 的块载荷、6.90/7.55 GiB = 91% 的载荷字节**；
卷积自己（前向 + 两个反向）只占 0.28 GiB。

### 8.4 结论：持有者是"梯度累加的每一次 `+` 都新造一份"

```
· 造块的：`tensor:add`（梯度累加）与 `tensor:zeros_like`（累加的初值），不是卷积内核；
· 留住它的：`+` 是值语义 —— 每次累加**新造一个数组**，旧的那份不会因为"后面没人读了"而释放，
  一直留到整步结束（峰值时 3161/6412 个块活着，结束时只剩 183 个）；
· 量级：同一个权重的梯度在峰值时有 86 份（§8.2），而"该有"的是 1 份 + 累加时的临时；
  这 86 份乘上权重梯度的大小，就是 2.27 GB + 0.61 GB + 0.41 GB ≈ 3.3 GB 的峰值来源。
```

### 8.5 最小修法建议（**未实施**，按 task-98 只定位不修）

```
① 方向一（不动执行器）：让梯度累加在"目标只有一个写者"时原地累加，不新造数组 ——
   即 reverse 规则里的 `dW = dW + contrib` 改成写入既有缓冲区。改动在 autodiff 的规则与 tensor:add 的原地路径，
   覆盖面：`tensor:add` 的 2997 个块载荷几乎全部消失。
② 方向二（不动 autodiff）：执行器按最后一次使用释放一个节点的值 —— 执行计划里每个节点的消费者是编译期已知的，
   一个节点在其最后一个消费者执行完后即可释放。这能一次解决所有"活到 step 结束"的值，不只梯度。
   代价是执行器要维护每个槽的剩余消费者计数（一次与节点表同规模的表）。
③ 两条互不排斥；①的先决条件是"写出是否唯一"能在规则层判定，②的先决条件是应用在按序执行的后端上。
```

### 8.6 量具说明（新增的两处，均为诊断，默认关闭）

```
· `std::profile gc_stats=1` 的退出报告新增两行：`[camel] blocks:`（四项计数，已有值）与
  `[camel] blocks-at-peak:`（仅 CAMEL_MM_BLOCK_TRACE=1 时才非空）。
· 按大小类统计只在 CAMEL_MM_BLOCK_TRACE=1 时计数：每次分配/释放各一次放宽原子加，峰值时刻拷一份快照。
  快照在其它线程仍在分配时取得，各类可能各差一个块 —— 足以指出哪些大小类撑起了峰值，不足以做精确账。
· 关闭时这两处都不执行（`traceBlocks()` 一次静态初始化后的布尔读）。
```

## 9. "原地累加能消掉多少"的两个比例（`task-99` ②）

### 9.1 比例一：2997 个 `tensor:add` 块载荷里多少来自反向的累加链（有分子分母的命令）

同一份 `model.cml`、同一台工作站、同一 `--block-limit`，只改工作负载：

```
# 只前向（CAMEL_BENCH_WORKLOAD 空）
  camel --block-limit 512 benchmarks/resnet/model.cml std::macro "std::profile" report=true std::nvm
  → cpu  tensor:add   18 次调用   18 个块载荷   30.0 MiB

# 一步训练（CAMEL_BENCH_WORKLOAD=train）
  camel --block-limit 8192 ... 同上
  → cpu  tensor:add 3489 次调用 2997 个块载荷 3.49 GiB
```
```
分母：3489 次 add 调用（2997 个块载荷，3.49 GiB）
分子：训练 − 只前向 = 3471 次（3471/3489 = 99.5%）、3.46 GiB（99.2%）
⇒ **add 的块载荷几乎全部（99%）来自反向的梯度累加**，不是模型里手写的 `+`（手写的是 18 次/前向）。
```

### 9.2 比例二：这些累加的"目标只有一个写者"与"同布局"，一半是构造性的、一半还差一次读数

```
· 构造性（读代码可得，无需读数）：反向的累加全部由 `addTangentSum` 造 ——
  src/stdlib/autodiff/pullback.cpp:2129-2159（`add(acc, contribution)`），
  调用点 pullback.cpp:2228（反向循环的每一轮 `carried = carried + round`）与 :2141（聚合元素逐个累加）。
  ⇒ 累加链上 `acc_old` 的唯一消费者就是这次 add（线程式累加），**"单写者"在构造上成立**，
    且两个操作数都是同一 primal 的切向量（gradient_ops.cpp:549-552 的 `TangentSpace.addUri`），
    同一 dtype / 同一 shape，`k::binary` 的产物是连续数组（elementwise_ops.cpp:43-49）
  ⇒ **"同布局"在构造上成立**。
· 还差的那一半：`acc_old` 是否可能是一个**视图**（切片/转置的别名）。`tensor:add` 的产物是连续数组，
  但累加链的起点可能是 `row_add_at` 之类写进某个张量的行，此时"原地写进 acc_old"需要知道它背后没有别的活引用。
  这一项**现有读数里没有**：`--copies` / `StorageReuse`（exec_plan.h、storage_reuse.h）只对 **update 节点**
  （append／路径赋值／sort 重绑定）做"能否复用存储"的判定并打印，`tensor:add` 这类**算子节点**没有这个判定，
  也没有消费者数的转储（`git grep consumers` 在 src 下只有 CLI 的一个开关名）。
```

### 9.3 一句话结论（给方向一定量）

```
若把反向的累加改成原地写：**能消掉 ≈3471 个块载荷 / 3.46 GiB（add 的 99%）**，
即 §8 里 3.3 GB 峰值的主要来源；前提是"目标无别名"，而这正是缺的那一次读数。
⇒ 最小的一步不是先改 autodiff，而是把 `StorageReuse` 的判定**扩展到算子节点**：
  它复用现有判定机制（写者数已在计划里算过），既是那把"能不能原地写"的读数，也是方向一的实现入口。
```

### 9.4 本节的可复算命令

```
cd D:\Projects\Camel-wt\perf
$env:CAMEL_HOME='D:\Projects\Camel-wt\perf\out\latest'
$env:CAMEL_PACKAGES='D:\Projects\Camel-wt\perf\benchmarks\common'
$env:CAMEL_BENCH_WEIGHTS='...\benchmarks\artifacts\resnet\weights'
$env:CAMEL_BENCH_INPUT='...\benchmarks\artifacts\resnet\input.npy'
$env:CAMEL_BENCH_THREADS='28'; $env:CAMEL_BENCH_WARMUP='1'; $env:CAMEL_BENCH_REPS='1'
# 训练：$env:CAMEL_BENCH_WORKLOAD='train'  前向：$env:CAMEL_BENCH_WORKLOAD=''
& out\latest\bin\camel.exe --block-limit 8192 benchmarks\resnet\model.cml std::macro "std::profile" report=true std::nvm
```

### 9.5 补上"运行时口径"：`inspect --reuse` 的 observed 那一半

9.2 缺的那项别名读数，现在有了一个**只读**的出口（`camel inspect --reuse`，分支 `lane/reuse-reading`），
它把两件一直被写、却从没被读过的计数摊开（`profile::planUpdateMarks` / `profile::arrayCountAnswer`，
原先只有写方 `exec_plan.cpp:1043`、`array.cpp:1100`，没有任何读者）。尾部一行：

```
REUSE observed plans <n> reuse-input <n> by-array-count <n> copy <n> counts in-place <n> copied <n>
```

口径必须这么读，这是**设计上的**两条口径，不是同一件事的两次测量：

```
· plans / reuse-input / by-array-count / copy  —— **编译期**：建了多少个计划，
  以及这些计划在更新节点上打的三种 `StorageReuse` 标记（Input / ByArrayCount / Copy）各有几个节点；
· counts in-place / copied                    —— **运行时**：一次原地形式的更新去问数组
  "这些字节我一个人用吗"（`Array::canWriteInPlace`）时，数组回答"能"和"不能"各多少次。
⇒ 编译期的 `alias=unknown`（本工具的行内第三列）与运行时这两个计数是**两件事**，
   两者不同不是缺陷；行内 `alias` 永远不猜，正是因为它该由运行时那次提问来回答。
```

**交叉核对（本次实测，resnet 训练步，`-Jobs 1` 一条 run）**

```
camel --block-limit 8192 --reuse benchmarks/resnet/model.cml std::macro "std::profile" report=true std::nvm
→ REUSE observed plans 27 reuse-input 0 by-array-count 0 copy 0 counts in-place 0 copied 0

同一程序 inspect --copies（35 行）：
→ 只有 2 行 update，两行都是 declared（训练步的 nn:sgd），in-place / by-count / copied 各 0 行：
   COPIES model.cml:106:16 sgd declared
   COPIES model.cml:113:19 sgd declared
   COPIES train_step updates 1 in-place 0 by-count 0 declared 1 copied 0
```

⇒ 两个工具的零是**同一个事实**：这条程序的更新只有 `declared`（算子自己声明复用，没人去问数组），
所以计划没打标记、也没有一次 `canWriteInPlace` 提问。
反证：若这是"落到默认值恰好是 0"的缺陷，`--copies` 会打出 in-place / by-count 行而 observed 仍为 0；
实测两侧一致，故**不是缺陷**。⇒ 见到这种零的判据顺序：**先问"这个程序有没有更新计划"，再问是不是缺陷。**

⇒ 本节**只补了运行时那两组计数**。9.2 里问的"这个 `tensor:add` 的目标此刻有几个持有者"
仍然要"把 `StorageReuse` 的判定扩到算子节点"的运行时，那是另一格（9.3 的第 244 行说的那一步），
本节的 observed 不声称补上了别名。

⇒ 一个词两个集合，已分开命名（避免把这行读成与 `--copies` 自相矛盾）：

```
`--copies` 的 `by-count`  = 存储留给数组的每个更新（**包含计划什么都没决定的原地形式** None），是后端的执行形态；
`--reuse observed` 的 `by-array-count` = 计划**真打了** `StorageReuse::ByArrayCount` 标记的节点，是更窄的那个集合。
```
