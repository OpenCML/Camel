# Issue: transformer 原生 train workload 零 CLI pass 裸跑 segfault

> 记录日期:2026-10-02(MLSys 2027 评估期间发现,暂不修——见 round3-dev-plan T2/T9)。
> 状态:**已修复**(T9,commit e59b2573)。根因与机制见下。

## 复现

```powershell
$env:CAMEL_HOME = "<repo>\out\latest"
$env:CAMEL_PACKAGES = "<repo>\benchmarks\common"
$env:CAMEL_BENCH_WEIGHTS = "<repo>\benchmarks\artifacts\transformer\weights"
$env:CAMEL_BENCH_INPUT = "<repo>\benchmarks\artifacts\transformer\input.npy"
$env:CAMEL_BENCH_WORKLOAD = "train"
$env:CAMEL_BENCH_WARMUP = "1"; $env:CAMEL_BENCH_REPS = "1"
out\latest\bin\camel.exe benchmarks\transformer\model.cml
```

零 CLI pass(std::nvm 回退路径)下进程以 access violation(0xC0000005)崩溃;
其他模型(mlp/gru/lenet/resnet/transformer4/transformer_big)同路径不炸。

## 根因(已定位)

与 nvm 无关,是 **GC 根覆盖缺口**:零 pass 时 `with_grad` 在运行期构建梯度图,
产物是"运行时创建的图"(gradient graph + 各 pullback 子图)。这些图不是 graph-carrier
根——没有任何 tracer 引用它们。梯度执行途中的 GC(large-object 预算触发,在图边界
safepoint 做 major 收集)会把 pullback 子图静态区里的常量(如 `permute_inverse` 的
排列数组)当作不可达对象回收;后续执行读到悬空引用即崩。mlp 等模型不炸只是
分配量小、没触发收集的巧合;`CAMEL_GC_STRESS_SAFEPOINT=1` 下小模型同样必崩。

`std::macro` 能绕过,是因为梯度图在编译期构建并随程序闭包一并注册为 carrier 根。

## 修复(commit e59b2573)

- `Function::updateRefs` 追踪其 runtime graph:Function 值活多久,图就活多久;
- `GCGraph::updateRefs` 追加追踪闭包边(dependencies/subGraphs/staticGraphRefs),
  使整个图闭包的静态常量随顶层 Function 同生命周期;
- `clearMarks` 改为遍历整个 graphSpace 去标记,避免 Function 追踪到的图因陈旧
  mark 位在下一轮收集中被跳过而漏扫静态区。

修复后零 pass 梯度与 `std::macro` 路径**逐位一致**(16/16 张量 byte-identical);
回归锁定用例 `test/cases/modules/autodiff/pb_statics_gc.cml`(GC stress 下
nvm/fvm 两路,修复前 segfault、修复后通过)。

## 绕过(修复前)

显式给 `std::macro`(及之后任意 pass)即正常:

```powershell
out\latest\bin\camel.exe benchmarks\transformer\model.cml std::macro std::nvm
```

E1/E2/E5 全部 transformer 测量均用 `std::macro` 前缀执行,数字不受影响。

## 已知残留(记录,不在本次修复范围)

零 pass 路径下每个训练步都会重新构建梯度图,graphSpace 中的图结构本身随之
持续累积(与 GC 无关的既有泄漏;`std::macro` 路径不受影响——梯度图只构建一次)。
