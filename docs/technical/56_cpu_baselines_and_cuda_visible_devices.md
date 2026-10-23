# 56. CPU 基线里的 CUDA 上下文：`Adam.step()` 建上下文，`SGD.step()` 不建

showcase 的 PyTorch 对照程序全是 CPU 程序：四个 `baseline/train_torch.py` 里没有一处 `.cuda()`，
`--threads 1` 只设 `OMP/MKL/OPENBLAS_NUM_THREADS`。2026-10-23 的慢层里，c6 与 d5 却在第一行比较数字
之前就死了：

```
torch.AcceleratorError: CUDA error: CUDA-capable device(s) is/are busy or unavailable
  .../torch/optim/adam.py:222  in step:  self._accelerator_graph_capture_health_check()
  .../torch/optim/optimizer.py:485  capturing = torch.accelerator.current_stream().is_capturing()
Returning 999 (CUDA_ERROR_UNKNOWN) from cuDevicePrimaryCtxRetain
```

这是本机 `~/camel-eval/venv-bench` 里的 `torch 2.14.1+cu130`（2026-10-02 安装）。`Optimizer.step` 的
包装层对每个优化器都套了 profiler 与钩子，但**设备探针只由 Adam/AdamW 调用**——`torch/optim/optimizer.py`
里这个方法自己的注释就是 `# Currently needed by Adam and AdamW`：

```python
def _accelerator_graph_capture_health_check(self) -> None:
    ...
    accelerator = torch.accelerator.current_accelerator(check_available=True)
    if accelerator and accelerator.type in {"cuda", "xpu"}:
        capturing = torch.accelerator.current_stream().is_capturing()
```

`current_accelerator(check_available=True)` 枚举设备并 retain 主上下文，`current_stream()` 再问当前流——
两层都不需要调用者提过 `.cuda()`。主机上 8 张卡有 7 张被别的租户各占约 20 GiB，
`cuDevicePrimaryCtxRetain` 于是返回 `CUDA_ERROR_UNKNOWN`。SGD 不调用它，所以从来不红。

## 56.1 最小复现与两种优化器的差别

同一台机、同一时段（2026-10-23，load 106-130）：

| 命令 | 结果 |
|---|---|
| `python3 -c "import torch; torch.optim.Adam(torch.nn.Linear(2,2).parameters(), lr=.1).step()"` | `AcceleratorError`（打印为空） |
| 同上，前面加 `CUDA_VISIBLE_DEVICES=` | ok |
| `python3 -c "import torch; torch.optim.SGD(torch.nn.Linear(2,2).parameters(), lr=.1).step()"` | ok（有没有 `CUDA_VISIBLE_DEVICES` 都 ok） |

所以红的集合正好是按优化器分的：c2、c6、d5 的基线用 `torch.optim.Adam`，c1 用 `torch.optim.SGD`。
c1 因此从来没被暴露（它在同一天、同一条字面路径、同一个新目录下都 PASS），c2 先死在另一个错误上
（`ckpt` 不再导出 `tree_map`/`tree_reduce`，进程 0.6 s 就退出，根本没跑到 torch）。

## 56.2 与数据目录无关（2×2）

`compare.py --profile small` 的 `--data` 只决定数据写到哪；`prepare_data` 每次都重跑 `make_data.py`。
把两条轴交叉跑一遍（c6，2026-10-23，load 106-130）：

| | 字面 `tmp/c6-showcase-small` | 新目录 |
|---|---|---|
| 不设 `CUDA_VISIBLE_DEVICES` | exit 1，`AcceleratorError` | exit 1，`AcceleratorError` |
| `CUDA_VISIBLE_DEVICES=""` | exit 0，`c6 compare: PASS` | exit 0，`c6 compare: PASS` |

目录这一列不改变任何一格：决定的是设备探针，不是路径。d5 同样的两条腿也是同一个结论。

## 56.3 处置

`showcase/common/showcase_compare.py` 与 `showcase/c2-mlp-mnist/compare.py` 各自的 `python_env()` 现在
设 `CUDA_VISIBLE_DEVICES=""`：这个比较是 CPU 对 CPU，别人的上下文占着卡不是它的题目。四个 showcase
计划的 PyTorch 侧都经过其中一个 `python_env()`，所以覆盖是完整的（只有这四个计划用 torch，见
`test/plans/perf/step_budget.plan.toml` 只用 Camel）。

**这条不能照抄到真的用 GPU 的用例上**：它把设备对基线藏起来，而不是修 ROM 的占用。一个需要 CUDA 的
计划若照抄，会在“没有设备”与“设备被别人占”之间失去区分能力。

复核方式：上面 56.1 的四条一行命令，或者 56.2 的四格；红的原始记录在
`test/results/log/20261023_042934_2744408/fail/perf.showcase_c6_vae/small/stderr.txt`（同目录的
`stdout.txt` 只有 `c6 step=0`，所以它在计划的一行摘要里看起来像容差或数据问题）。