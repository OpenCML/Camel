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