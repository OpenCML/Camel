# `std::cpp` 要不要有 tensor 类型

**问题**：`std::cpp` 写出的程序**没有 tensor 值**——`src/passes/trans/cpp` 里 `Tensor` 一次都没出现（`git grep -c "Tensor" -- src/passes/trans/cpp` = 0）。于是所有造 tensor 的算子都只能拒绝，而它们拒绝时撞上的往往不是 tensor，是**形状参数**。要不要给这个后端一个 tensor 类型？

**三个数（同一把尺子，`test/cases` 3201 个 `.cml`）**：

| 格子 | 程序 | 句 | 类型 |
|---|---|---|---|
| `tensor:ones` | 39 | 1 | 1 |
| `tensor:zeros` | 32 | 1 | 1 |
| `tensor:load_npy_f32` | 28 | 1 | 1 |
| `tensor:reshape` | 270 | 2 | 4 |
| **合计** | **369** | | |

**一句复现**：`camel test/cases/trans/cpp/refuse_tensor.cml std::cpp` → `… uses 'tensor:ones' to build a tensor, and std::cpp writes no tensor value: the pass has no tensor type, so a shape cannot be handed to it.`（`std::fvm` 跑同一个程序正常；最小程序四行即可复现，见 `cases/trans/cpp/refuse_tensor.cml`）

**为什么现在这句是对的、也是不动的理由**：`int[]` 是译得了的，缺的是 tensor 值本身；把 `ones/zeros` 的拒绝改成点名 tensor（本轮 `c271cd595`）之后，369 个程序已经**记在同一件事上**，普查不再把它们当 369 件活。但"计数对"不等于"能建"。

**三个可选，各带代价**：
- **A. 加一个最小的 tensor 类型**（形状 + 一条 element 跑线，只有构造/取形状/取元素三个操作，其余仍拒绝）：代价是 `cpp_types` 里多一个类型的声明与相等，`rt` 头里多一小段；**每个 tensor 算子仍要逐条写规则**，`reshape` 270 个程序靠它一条不够。
- **B. 只把 tensor 值降成"数 + 形状"的二元组**，规则逐条写：代价是不引入类型，但**每个算子都要手写规则**（369 个程序后面的算子数不止 4 个），且生成的 C++ 与解释器的 tensor 语义要靠每条规则自己保证——"同一输入同一输出"没有类型兜底。
- **C. 现在不动**，把 369 个程序记成"后端没有 tensor 类型"（本轮已经这么记了）：代价是这 369 个程序在 `std::cpp` 上永远不绿，但它们不再冒充缺口，也不替它们排工。

**问作者的话**：选 A、B 还是 C？若选 A，最小集合是不是"构造 + 取形状 + 取元素"三件；若选 B，"数 + 形状"的表示要不要写进 `docs/design/cpp-standalone`。

**堵着谁**：`std::cpp` 普查里 `tensor:` 这一族 369 个程序；`h-fmt` 那条 `reshape`（2 句 / 4 类型）也要这张裁定才知道是"没有类型"还是"没有规则"。