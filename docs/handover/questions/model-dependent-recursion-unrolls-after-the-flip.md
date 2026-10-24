# 模型相关深度的递归在 P4 翻转后被展平（h449 lane 交回，2026-10-25）

本文件交回 `test/plans/feat/modules/onnx.plan.toml` 里 6 条 red 的**根因**：翻转之后，一个
「深度取决于模型输入」的递归在导出的化简里**不再停在不动点**，而是被一路展平（直到停止条件
被证明为常量，或者撞上编译期增长上限）。这 6 条不是 onnx 模块能单独修的：停机机制在
`src/runtime/draft_inline.cpp` 的调用点特化里，与其它 lane 共用。

## 一、六条 red 与实测

| 用例 | develop（`147e2d2d4` 构建） | 翻转（本 lane，`567e8f745` + 本批修改） |
| --- | --- | --- |
| `export_loop_carried_array` | exit 1，拒绝文本与 plan 期望逐字相同 | **exit 0，写出模型**；实测图 = `Mul(input,input)` → `ReduceSum` → `Less` → 3 层嵌套 `If`，最内 else 臂是 `x*2.0`，**没有 `Loop`** |
| `export_loop_values_array` | exit 1，`mergeOutcomes` 的期望文本 | exit 1，但文本换成 `':op/idx_arr': only int arrays (shapes) can be indexed by the model input`（递归被展平后，期望的那条拒绝路径根本不再出现） |
| `ort_loop_loaded_weights` | 模型里有 `Loop` | `FAIL: the model holds no Loop: [...]`，280 个节点 |
| `ort_loop_values` | 全部 `ok` | `tree, 6 steps: counter 106`（步数被展平后计数器失真） |
| `ort_linear_recursion` | 绿 | 85–96 s 后被 `MemoryExhausted`（`std::default`，256 MB）杀掉 |
| `pass_export_equals_export_model` | 45.60 s CPU / 25.69 s wall | 手工跑同一 harness（`pass_export.py equiv`，26 个程序）：**11m16s wall / 4m43s CPU，EXIT=1**，尾部是编译期增长上限的 help（"an inlining or unrolling that does not end"） |

整层：本 lane 起点 26 red；本批修到 16 red，剩下的 6 条就是上表，另有 10 条是 X 桶（见
`growable-array-vs-tensor-in-the-onnx-exporter.md` 第三节）。

## 二、机理（`src/runtime/draft_inline.cpp`，本 lane 的临时埋点已撤掉）

`collectDirectFuncSpecializations` 里那段守卫的注释写的是设计意图：

> Binding constants into a call of a recursive function unrolls one level. … A call still
> guarded by a branch whose condition is not constant would unroll forever: it gets type
> refinements only.

实测（`export_loop_carried_array`，`CAMEL_LOG_INFO` 打在守卫处）：守卫的**条件成立且失效动作
确实发生**——递归调用点 `insideArm=true binds=true`，值绑定被 `erase_if` 抹掉，留下的只有
**类型绑定**。留下的是哪一条：

```
[type1 int[1]]        # 实参 [k] 的精确类型，精化形参声明的 int[]
```

也就是说，翻转之后「数组字面量的精确类型（`int[1]`）精化声明的 `int[]`」这一条类型精化，
**每次特化之后都会被重新推出来**（形参的类型在 `reinferDraftTypes` 里重新推导），于是下一次
调用的特化 key 又不相同：日志里同一棵 `walk` 出现 `walk$tspec1`、
`walk$tspec1$tspec4$spec5`、`walk$tspec1$tspec6$spec7` 一串，而 develop 在同一个用例上只到
`walk$tspec1` 就到不动点、把调用留在图里（这正是导出侧那条「a recursion whose depth depends on
the model input … on a path next to a recursive call」拒绝文本的前提）。

请作者定的问题：

1. 守卫的设计前提「值绑定被抹掉之后只剩类型精化，而类型精化会到不动点」在翻转后是否还成立？
   `int[1]`（字面量的精确形状）对 `int[]`（声明的可增长数组）这条精化，是不是本来就不该进
   「按类型特化」的名单（它是值的属性，不是类型的属性——与
   `growable-array-vs-tensor-in-the-onnx-exporter.md` 是同一个歧义）？
2. 若该精化合法，那么守卫需要的是不是「同一递归函数的值绑定抹掉之后，类型特化也一并封顶」？
   现在 `kMaxTypeSpecializationsPerRecursiveFunction = 8`
   （`include/camel/runtime/draft_inline.h:43`）挡不住：本用例 3 层就把停止条件证明成常量了，
   `ort_linear_recursion` 则一路撞到内存上限。

## 三、本 lane 为什么没在这里动手

- 停机点在 `src/runtime/draft_inline.cpp`（核心运行时，非 onnx 模块），与其它 lane 的
  `std::opt`/`std::jit` 共用；改它等于改所有调度/优化路径的行为。
- onnx 侧的旋钮只有 `exportRewriteConfig()`（`growthBudget.nodes`、`maxOwnerNodes` 都设成
  上限）与 `kMaxExportRounds = 4096`，没有「按递归深度特化封顶」这一档；把上限调小只会把
  「展平」换成「拒绝」，而且会连带改变 26 个程序的导出结果，超出本 lane 的边界。

需要的话，本 lane 可以按作者的裁定在 onnx 侧补一条**导出专用**的封顶（例如导出化简里对
「已经因分支守卫而抹掉值绑定的递归调用」不再做类型特化），但那需要先定上面第 1、2 问。