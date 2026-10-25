# `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update`：红的来路、为什么没人认领、现在的读数

只记事实与读数。这条用例是 `test/plans/gpu/tiles_transfer.plan.toml` 的第一条测试
（`kind = "behavior"`，`case = "../../cases/modules/tree_training.cml"`，
`args = ["${case}", "std::macro", "std::fvm"]`，`expect_stdout_exact` 是五行 `0.0`）。
计划 `tier = "slow"`，一共 3 条用例；**这个计划是唯一跑该用例的计划**——
`git grep tree_training` 在 `test/plans/` 下只命中 `test/plans/gpu/tiles_transfer.plan.toml` 一处。

## 一、红是什么

约两天里它是**编译期拒绝**，用例根本跑不起来：

    error[SemanticDiag::ReturnTypeMismatch] 0x03000011

`docs/handover/batch-slow-2026-10-24.md` 的慢层整层跑把这一行逐字记了下来：值的类型
`{ weight: Tensor<float64, [2]>, nested: (Tensor<float64, [1]>, Tensor<float32, [0, 2]>) }`
与声明的 `Model`（字段是 `Tensor<f32>`）不符，位置在
`test/cases/modules/tree_training.cml` 的 `initialize`。同一次跑还记了这个计划另外两条用例
的读数（`gpu.tiles/*` 的两条是本计划之外的红），本页只说这一条。

## 二、谁引入的红、什么时候

两件事叠在一起，两件都在 `origin/develop` 的历史里：

| 事实 | 提交 | 时间 |
|---|---|---|
| 夹具写成这个样子（在 `Tensor<f32>` 的位置上写裸浮点字面量 `reshape([1.0, 2.0], [2])`），并带上这条计划条目 | `23d97db76`「feat: expand type-directed parameter tree operations into ordinary graphs」 | 2026-10-18 23:50:42 |
| **元素类型的读法改了**：张量保持写下的元素类型，`reshape([1.0, 2.0], [2])` 从此是 f64 张量 | `b7872c4c9`「refactor(tensor): a tensor keeps the element type it is written with」 | 2026-10-23 02:35:03 |

`b7872c4c9` 的提交信息自己写明了这条读数（逐字）："`reshape([1.0, 2.0], [2])` and
`new([1.0, 2.0])` are now f64 tensors, because an array of float literals is an f64 array. f32 programs
spell the element type: `reshape([1.0, 2.0] as f32[], [2])` …"，以及
"Every case that built f32 tensors from float literals now says f32"。所以这条用例以前建的是 f32 张量、
与该提交之后不再成立：**红是它带来的**。

同一提交改了 210 个文件，给一批"本意就是 f32"的夹具补了 `as f32[]`——包括这条用例的兄弟
`test/cases/modules/tiles/device_tree.cml`——而 `test/cases/modules/tree_training.cml`
**不在它的改动名单里**（`git show b7872c4c9 --name-only` 对该路径零命中）。

最早被写下来的红读数是 `07e81237f`（2026-10-23 23:57:00，`b7872c4c9` 之后约 21 小时）：
`docs/handover/batch-slow-2026-10-24.md` 把它记在 `bd20f2200`（2026-10-24 13:53:19）与这个对照点上，
归为 `pre-existing: red at 07e81237f`。检索 `docs/handover/`，这条红在 `b7872c4c9` 之前
没有任何记录。

## 三、为什么没人认领

- 它的计划是 `tier = "slow"`、在 `test/plans/gpu/**` 下，而闸门的目标是 `smoke` 与
  `test/plans/feat`：这个计划从不在闸门的选程里（H161 记过同一类洞——102 条 GPU 用例从没被闸门跑过）。
- 用例只被这一个计划跑，快层永远看不到它：改坏它不会有任何快层读数出声。
- `--changed` 的规则表（`test/plan-selection.json`）里唯一点名 `test/plans/gpu/tiles_transfer.plan.toml`
  的路径是 `modules/tiles/` 与 `test/cases/modules/tiles/`；这条用例在 `test/cases/modules/` 根下，
  不在其中。也就是说单独改 `test/cases/modules/tree_training.cml` 不会让 `--changed` 点名这个计划：
  它今天被选中，靠的是同一个计划里另外两个被规则覆盖的用例
  （`test/cases/modules/tiles/device_tree.cml`、`test/cases/modules/tiles/upload_large.cml`）。
- 慢层整层的那次批量跑只给了它一行读数（第二节），没有归属文件、没有问答页；
  在 `docs/handover/` 里检索这个用例名，只有那一行。

## 四、现在已好，以什么读数证明

夹具过期这件事由 `65f0bb26d`「test: give the tensor literals of two fixtures the element type their
functions declare」（2026-10-25 11:18:18）修掉，**只改夹具**：改动文件只有
`test/cases/modules/tree_training.cml`（两个字面量补 `as f32[]`，写法与兄弟
`test/cases/modules/tiles/device_tree.cml` 相同）与 `test/cases/facts/cost_many_tensor.cml`
（不被任何计划引用）。计划的期望**一个字未动**：`git show 65f0bb26d` 没有触碰
`test/plans/gpu/tiles_transfer.plan.toml`，`expect_stdout_exact` 仍是五行 `0.0`。
`65f0bb26d` 是 `origin/develop` 的祖先（`git merge-base --is-ancestor 65f0bb26d origin/develop` 退出 0）。

本页的读数（`camel-remote.ps1 -Path D:\Projects\Camel-wt\h518-author-pages`，
`-Plan test/plans/gpu/tiles_transfer.plan.toml,test/plans/feat/modules/index_semantics.plan.toml`，
主机 88zjwei，树 tip `8dd55009b`）：

    [PASS] tree_expansion_autodiff_and_device_update               3.38 s
    [PASS] device_parameter_tree_training                          2.91 s
    [PASS] large_upload_completion                                 5.46 s

同一次运行把 `test/plans/feat/modules/index_semantics.plan.toml` 的 5 条一起跑了，合计
`[SUMMARY] total: 8, pass: 8, fail: 0`；本计划的 3 条全 PASS。

`65f0bb26d` 的提交信息里还记了修复前后的对照，逐字："the case now prints it verbatim — `0.0` five
times — and the plan is green three runs in a row (`[SUMMARY] total: 3, pass: 3, fail: 0`, the case at
3.10/3.66/2.52 s, against 139.12 ms and a compile error before)"。

## 五、防它再次无声的机制（现状）

成立的两条：

- `scripts/plan-inventory.mjs` 的 `SLOW_PLANS` 点了 `test/plans/gpu/tiles_transfer.plan.toml`，
  `scripts/release-gate.mjs` 的慢层按显式计划文件跑它；同一个清单还要求每个计划文件落在
  `FAST_TARGETS`、`SLOW_PLANS`、`FORBIDDEN` 或 debug 计划之中，落在外面的是一个闸门失败——
  "没有任何层跑的计划"不会再静默存在。
- `test/plan-selection.json` 的 `modules/tiles/` 规则把 `test/plans/gpu/tiles_transfer.plan.toml`
  点给 tiles 的改动；用例路径规则（`ownCases`）让它名下的用例也能选中它。

还差的一格（事实，未做改动）：`test/cases/modules/tree_training.cml` 自己不在任何规则的路径里
（规则表的用例路径只有 `test/cases/modules/tiles/`、`test/cases/modules/onnx/`、
`test/cases/modules/autodiff/`、`test/cases/modules/std/json/` 与 `test/cases/gpu/`），
所以只改这一个用例仍不会被 `--changed` 点名这个慢计划。

## 六、没做的

没有改任何代码、计划、期望、`tier` 或 `known_fail`；没有复活或改动任何用例；没有推任何东西。