# 同一个 `T[..]`，两种表示：`tensor:*` 的结果该走哪条

**问题（作者裁）**：`tensor:ones` 的签名是 `export func ones::<T>(shape: int[]): T[..]`（`stdlib/signatures/tensor.cml:26`）。按 2026-10-31 05:30 的分界（"看算子的**结果类型**"），`T[..]` 是**带形状的数组**，`std::cpp` 已有原生表示（develop 的 `literal_rank`、`reshape_flat`、`slice_rank` 这批用例正在往这个方向加），它该走原生；按同一天 05:50 的裁定（张量是**外部对象**过桥，`rt::Foreign`，语义只在运行时 tensor 模块里实现一份），它该走 `Foreign`。

**同一个类型，两种表示**。裁据是哪一条：**"算子来自哪个模块"**（`tensor:*` 一律 `Foreign`，其它模块的带形状数组走原生），还是**"结果类型的形状是否具体"**（`T[..]` 这种类型参数化的走 `Foreign`，`f32[2;2]` 这种具体的走原生）？

**划界表（按算子的结果类型，来自 `std::cpp` 普查的 82 个算子）**：

| 算子（普查里的格子） | 结果类型 | 跟哪一条 | 备注 |
|---|---|---|---|
| `tensor:ones` / `tensor:zeros` | `T[..]`（类型参数化） | **`rt::Foreign`**（05:50） | 作者点名过四行程序 |
| `tensor:load_npy_f32` | 张量 | `rt::Foreign` | 同上 |
| `tensor:reshape` | `T[..]` | **两条都说自己是** | `h-fmt` 那条已收尾，表示法与这里必须一致 |
| `rand:uniform_array` / `rand:normal_array` | `T[..]` | 未定 | 与 `ones` 同形：形状是参数 |
| `autodiff:grad` / `autodiff:with_grad` | 函数值 | 原生（`rt::Fn`） | 缺的是求导本身 |
| `json:decode_as` | `Map<string, int>!` | 原生（`rt::Map`） | 缺的是 decode 规则 |
| `:op/mem_blocks` | 四个计数的结构体 | 原生 | 已裁定：目标不具备（无块空间） |
| `tiles:upload` | `Device<T>` | **不确定** | 若 `Foreign` 装任意运行时根，它归这条；若只给张量开，另立 |

**为什么这一页不能自己裁**：`tensor:reshape` 已经有人按"带形状的数组"做过一版（`h-fmt`），若这里把 `tensor:*` 一律判给 `Foreign`，**同一个类型在两个后端上会有两种表示**，正是"同一输入同一输出"要防的；反过来若一律判给原生，05:50 那句"语义只在运行时 tensor 模块里实现一份"就落空。**两条裁定在同一个类型上相遇，只有作者能分。**

**与本题无关的一行事实**：`std::cpp` 里 `Tensor` 出现 **0 次**（`git grep -c "Tensor" -- src/passes/trans/cpp`），所以今天还没有任何表示；这一页定的是"第一个表示该是哪一个"。