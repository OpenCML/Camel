# Issue: transformer 原生 train workload 零 CLI pass 裸跑 segfault

> 记录日期:2026-10-02(MLSys 2027 评估期间发现,暂不修——见 round3-dev-plan T2/T9)。
> 状态:open,有绕过方案(`std::macro`)。

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

## 绕过

显式给 `std::macro`(及之后任意 pass)即正常:

```powershell
out\latest\bin\camel.exe benchmarks\transformer\model.cml std::macro std::nvm
```

E1/E2/E5 全部 transformer 测量均用 `std::macro` 前缀执行,数字不受影响。

## 线索(未查)

- 仅 transformer 的 train workload 触发;其 forward 无 `train` workload 时同路径正常,
  指向 value_and_grad + sgd 展开的 pullback 图与 nvm 回退路径的交互;
- 修复方向待定位(nvm 对某类节点的处理);排期见 round3-dev-plan T9(仅当 #25/reshape
  清理完成后有余量)。
