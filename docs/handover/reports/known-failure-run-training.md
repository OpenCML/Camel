# `run_training` 那一族的已知失败，在等什么

一条：`test/plans/feat/modules/autodiff.plan.toml` 的 `benchmark_gradients_vs_torch`（`tier = "slow"`，`--tier all` / `npm run test:all` 才跑；fast 层不跑它），`backends = ["nvm"]`、`known_fail = ["nvm"]`、`known_fail_note` 见文件里那一条。

| 用例（原样） | 判的是哪一条 | 失败形态（原文） | 一句结论 |
|---|---|---|---|
| `benchmark_gradients_vs_torch.nvm` | `benchmarks/train.py --check`：九个模型的训练梯度与 PyTorch float32 自动微分一致，两种配置 `std::macro std::nvm` 与 `std::macro std::opt std::nvm` | `benchmarks/train.py` 抛 `RuntimeError: ... mlp_big/model.cml std::macro std::opt std::nvm failed (-11)`，`-11` 即 SIGSEGV；栈是 `camel::core::rtdata::Variant::updateRefs+31` ← `GenerationalAllocatorWithGC::scanRelocatedObjects` ← `minorGCUnlocked` ← `safepoint` ← `NodeVMSchedPass::call`；`ab124a00e` 之后同一处报的是 `the type it was traced with has code 1610612746, which is not an enum's` + `the type reads as: (x: f32[..]) => float` | 去函数化把值做成了 enum（`tag=0`、`fields=1`），但它落进去的槽还写着原来的函数类型 `(x: f32[..]) => float`；GC 按槽的类型去追这个 `Variant`，读到的不是 enum |

**它在等什么**：**一个槽的类型跟着值的形式改**。`ab124a00e` 之后这条不再是推测：`Variant::updateRefs` 现在会报出它拿到的那个类型，读数是 `(x: f32[..]) => float`（`code` 1610612746 = `0x6000000A` = `TypeCode::Function`），而槽里躺着一个 `Variant`（`tag=0`、`fields=1`）——**值已经变成了 enum，位置还写着原来的函数类型**。不是 typeId 越界、不是 tag 越界、也不是悬垂指针；是表示转换只做在值上、没做在位置上（和 18:30 那条同族）。**认领：`lane-defun5`**（诊断是在这条车道的树上取的）。

**它不在快层里出现，不是漏登记**：本用例属 `tier = "slow"`，`-Tier fast` 根本不跑它，所以快层闸门的 `known failures: 2` 不含它（那两条是 `effects_model_edges` 的控制边）。要看这条债务，跑 `-Tier slow` 或直接指定这个 plan 文件：`node scripts/test.js test/plans/feat/modules/autodiff.plan.toml --tier slow --jobs 1`，它报的是 `[XFAIL] ... known failures: 1`，不是 `fail`。**“已知失败但不在快层出现”正是裁定要的“不要以绿的样子出现”，不是登记漏了。**

**为什么它是 pass 的缺陷，不是程序的**：同一个模型在 `std::macro std::nvm`（不进去函数化）下把训练负载跑完，同一套 pass 下的推理负载 `exit 0`，而**小的 `mlp` 与 `mlp_big` 崩得一模一样**（`mlp` 训练+pass `139`、`mlp_big` 训练+pass `139`；两个模型不挂 pass 都是 `0`）——差别是这条 pass，不是模型的规模。栈里重复的 `NodeVMSchedPass::call` 只有 6 层、不随模型长，所以也不是栈深度。

**为什么必须是 `known_fail` 而不是跳过**：`known_fail` 是"登记了失败形态、但不许悄悄变绿"的机制——runner 见到标记了 `known_fail` 却通过的用例会直接判红（`marked known_fail on '%s' but passes: remove the mark`），所以修好它的那一笔必须同时摘掉标记。跳过则相反：跳过不登记形态，也不会有任何东西在它变好时叫你。这一条尤其要这样：它此前是**绿**的，而那是 `session.commit()` 被校验拒、pass 在 catch 里放弃 `run_training` 这一族换来的（`defun-dispatch-notes.md` 顶部），不是它降对了。