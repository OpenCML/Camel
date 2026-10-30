# `run_training` 那一族的已知失败，在等什么

一条：`test/plans/feat/modules/autodiff.plan.toml` 的 `benchmark_gradients_vs_torch`（`tier = "slow"`，`--tier all` / `npm run test:all` 才跑；fast 层不跑它），`backends = ["nvm"]`、`known_fail = ["nvm"]`、`known_fail_note` 见文件里那一条。

| 用例（原样） | 判的是哪一条 | 失败形态（原文） | 一句结论 |
|---|---|---|---|
| `benchmark_gradients_vs_torch.nvm` | `benchmarks/train.py --check`：九个模型的训练梯度与 PyTorch float32 自动微分一致，两种配置 `std::macro std::nvm` 与 `std::macro std::opt std::nvm` | `benchmarks/train.py` 抛 `RuntimeError: ... mlp_big/model.cml std::macro std::opt std::nvm failed (-11)`，`-11` 即 SIGSEGV；栈是 `camel::core::rtdata::Variant::updateRefs+31` ← `GenerationalAllocatorWithGC::scanRelocatedObjects` ← `minorGCUnlocked` ← `safepoint` ← `NodeVMSchedPass::call` | 去函数化交给运行时的那个 enum 值，在 minor GC 的重定位扫描里活不过它的类型：`updateRefs` 头几条指令就崩，还没走到字段槽的循环 |

**它在等什么**：等去函数化写出的值，其 enum 类型比值活得久——GC 扫到它时读得到类型与变体表。**认领：`lane-defun5`**（诊断是在这条车道的树上取的）。

**为什么它是 pass 的缺陷，不是程序的**：同一个模型在 `std::macro std::nvm`（不进去函数化）下把训练负载跑完，同一套 pass 下的推理负载 `exit 0`，而**小的 `mlp` 与 `mlp_big` 崩得一模一样**（`mlp` 训练+pass `139`、`mlp_big` 训练+pass `139`；两个模型不挂 pass 都是 `0`）——差别是这条 pass，不是模型的规模。栈里重复的 `NodeVMSchedPass::call` 只有 6 层、不随模型长，所以也不是栈深度。

**为什么必须是 `known_fail` 而不是跳过**：`known_fail` 是"登记了失败形态、但不许悄悄变绿"的机制——runner 见到标记了 `known_fail` 却通过的用例会直接判红（`marked known_fail on '%s' but passes: remove the mark`），所以修好它的那一笔必须同时摘掉标记。跳过则相反：跳过不登记形态，也不会有任何东西在它变好时叫你。这一条尤其要这样：它此前是**绿**的，而那是 `session.commit()` 被校验拒、pass 在 catch 里放弃 `run_training` 这一族换来的（`defun-dispatch-notes.md` 顶部），不是它降对了。