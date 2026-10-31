# 「等第 5 步（外部对象）」的程序按结果类型分桶（2026-10-31，`lane-census2`）

**尺子**：`test/tools/cppexec/tensor_buckets.py`（只测量，不改翻译）。对 `test/cases/**/*.cml` 逐个跑 `camel <case> std::cpp`，读出拒绝里点名的算子，按"这个算子交回的是哪种值"分桶。

**读数（树 `a43e4f62d`，1244 个源文件，build-info `a43e4f62dd (clean)`）**：

| 桶 | 数 |
|---|---|
| 被"等第 5 步"的算子拒绝的程序（总计） | **140** |
| ① 结果是原生带形状数组（`T[..]` 有具体形状） | **28** |
| ② 结果是 `tensor` 模块对象 | **110** |
| ③ 两种说法都像 | **2** |

**按算子**：`tensor:ones` 42、`tensor:zeros` 37、`tensor:load_npy_f32` 31、`rand:uniform_array` 24、`rand:normal_array` 4、`tensor:reshape` 2。

**③ 的名单（全部，两条）**：
- `test/cases/modules/tiles/matmul_edges.cml` — `tensor:reshape`
- `test/cases/modules/tiles/max_special.cml` — `tensor:reshape`

**为什么总数是 140 而不是 369**：369 是 `lane-mcts` 那棵树上的数（`reshape` 270 + `ones` 39 + `zeros` 32 + `load_npy` 28）。**`tensor:reshape` 已在 develop 上落地**，那 270 个程序不再以 `tensor:reshape` 身份被拒，这一格只剩 2 个；其余各格也随树移动（42/37/31 对 39/32/28）。**同一把尺子量不同的树，总数会变——数目要带树。**

**③ 为什么只有两条**：`tensor:reshape` 是唯一同时符合"来自 `tensor` 模块"与"结果是带形状的数组"的算子；`rand:*_array` 的结果也带形状，但语义不在 tensor 模块里（stdlib 组合出来的），所以落在①。