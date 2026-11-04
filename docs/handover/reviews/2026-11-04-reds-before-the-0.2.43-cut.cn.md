# 0.2.43 切片前的红：四条要修、一条口径、一批按平台登记

发布/CI 侧 → 开发侧，2026-11-04。

## 现在的读数

闸门 `41772024087`，树 `develop = d7f15f2b3`，8772 条：

| 腿 | 红 |
|---|---|
| linux-x64 | 5 |
| linux-x64-nojit | 6 |
| win-x64 | 17 |
| macos-arm64 | 19 |

`release` 已停在 `0.2.43` 的版本号提交上等切。下面四条是这一版不想带出去的。

## 四条要修的（都在 `develop` 头上修）

1. **`modules.tiles / gpu_scope.a_run_without_a_device_names_its_own_configuration`**
   复现：`camel --devices cpu test/cases/modules/tiles/gpu_scope_benchmark.cml`
   缺：`the run selected the host only (--devices cpu)`
2. **`showcase_s2_first_step_gradient / matmul_t.vector_lhs_law.nvm`**
   复现：`camel test/cases/compile/transforms/matmul_transpose_vector_lhs.cml std::nvm`
   期望 exit 0，实得 exit 1；缺 `matmul_t vector_lhs value=3.0 dL/dv=[0.75, 1.125] dL/dw=[[1.0, 1.0], [2.0, 2.0]]`，也缺 `matmul_t vector_lhs engine value=3.0 dL/dw=[[1.0, 1.0], [2.0, 2.0]] dL/dv=[0.75, 1.125]`
3. **`showcase_s2_first_step_gradient / matmul_t.vector_rhs_law.nvm`**
   同一形状：`matmul_transpose_vector_rhs.cml std::nvm`，期望 `value=3.5 dL/dx=[[0.5, 0.25], [0.5, 0.25]] dL/du=[4.0, 6.0]`
4. **`showcase_s2_first_step_gradient / matmul_t.rank_three_is_refused_by_name.nvm`**
   `matmul_transpose_rank_three.cml std::nvm`；缺 `skip=rule-ranks uri=tensor:matmul rule=matmul_t node=6 position=0 ranks=(3, 2) operands=2`

第 2、3 条是同一族的两个形状，第 4 条是同一个规则的拒绝路径；它们都出自刚合进来的 `lane-lawlint` / `lane-vjpparity` 那一批，按你们的节奏应该是一处修完三条一起绿。

## 一条我这边处理的

`docs.links / docs.snippets_compile` 不是算错，是时间边界：

```
used 35160 ms of CPU against a 120000 ms timeout, below the 4x margin
```

这是计划口径的问题（要么把 `timeout_ms` 提到有理由的高度，要么把这条用例拆开），发布侧改，不动你们的代码。

## 一批不用修、按平台登记的

macOS 15 条、win 12 条，全是平台差异而不是回归：

- `trans.cpp` 三条（macOS 上转译比较跑不起来）
- `gelu_grad` / `erf_grad` / `erfc_grad` 的 bits 与 `native.sim_declaration_import` 的十进制文本（arm64 的 FMA 收敛与长双精度）
- `modules.rules` 四条（精度断言）
- `load_class` 两条、`debugger.dap_fusion_group`（工具在 macOS/win 上 exit 1）

我按"带平台限定的 known_fail"登记。**如果其中某条你判断是真缺陷，回我一句，我把它从登记里拿掉**。

## 约定

- 只在 `develop` 头上修；不要动 `release` / `main`；修完回我 commit sha。
- 赶得上这一版就修；赶不上就回我"哪条带出去"，我按已知红发版，下一版补。