# `std::profile` 的原地更新两行：计数为 0 时打不打

## Q1：一次没有任何原地更新的运行，报告里要不要出现这两行？

原地决策三层落地后，`std::profile` 的文本报告多出两行、JSON 多出一个键（`src/profile/profile.cpp`）：第一行读内存规划写进执行计划的三类标记（复用输入的存储 / 问数组的计数 / 拷贝），第二行读更新算子在运行时问数组自己计数得到的答案。

有原地更新的运行，逐字（`test/cases/linear/byref/element_assigned.cml`）：

```
  in-place updates     the plan reuses the input's storage at 1 update nodes, asks the array's count at 0, copies at 0
                       the array's own count answered the update operators: allowed the write 1 times, refused it 0 times
```

另一个用例的 JSON，逐字（`test/cases/consistency/new_values/rebinding_loop.cml`）：

```
"inplace_updates":{"plan_reuse_input":0,"plan_by_array_count":3,"plan_copy":1,"array_count_one":250,"array_count_not_one":120}
```

现在的口径是**计数全为 0 时两行不打、JSON 也不带这个键**。这是为了不动两个 golden：`test/cases/modules/tensor/fusion_group_none.profile.golden.txt` 与 `test/cases/modules/tensor/fusion_group_none.profile.golden.json`。`test/plans/feat/modules/fusion_group.plan.toml` 的 `profile.none` 用例要求"报告与基线提交（b43dd5c68）的报告逐字相同，只允许测量数字、百分比与字节数不同"，而 `fusion_group_none.cml` 没有任何原地更新。

若改成**零值常显**，同一份报告会多出（下面两行就是现在这段代码在计数为 0 时会打印的文字）：

```
  in-place updates     the plan reuses the input's storage at 0 update nodes, asks the array's count at 0, copies at 0
                       the array's own count answered the update operators: allowed the write 0 times, refused it 0 times
```

JSON 里也会多出 `"inplace_updates":{"plan_reuse_input":0,"plan_by_array_count":0,"plan_copy":0,"array_count_one":0,"array_count_not_one":0}`。代价是重生成上面那两个 golden，并把该用例注释里"golden 取自基线提交"的说明更新；`docs/reference` 一族今天没有摘录这段报告，若有也要跟着改。

建议：**保持现状（有原地更新才打）**。理由：这两行说的是"这次运行里原地更新做了什么"，没发生就没有可读的东西；报告里其它零值行（`device kernels 0`、`transfers 0 in all`、`synchronisations 0`）描述的是"这次运行的形状"，与"这件事有没有发生过"不同。若作者要的是"永远看得见这个机制存在"，那就常显，代价如上——请一并决定是否由本轮重生成那两个 golden（本轮没有动它们）。

影响：不定的话，两种口径都只有几行代码的差别，但报告要么永远缺席这一项，要么悄悄破坏两个 golden 的逐字比较。