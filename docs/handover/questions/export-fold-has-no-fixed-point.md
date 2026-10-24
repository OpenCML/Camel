# `export_fold`：去上限之后这条递归**没有**不动点（h465 集成 lane，2026-10-25）

> 本文只报选题与读数，不定语义。作者页面（`docs/handover/README.cn.md`、`reviews/**`、
> `docs/design-principles*`、`docs/design/**`、`docs/language/**`）一个字未动。

裁定来源：`docs/handover/reviews/questions-2026-10-24-evening.md`
§`recursion-specialization-fixed-point-followups`（随 `handover/h474-in-place-planning` 到位）：
读法 E、第 2 条"键不同（多态递归）就把不同的位置里 extent 与维度名放宽为 `?`"、第 3 条"计数上限
删掉，守卫只能是一条内部断言——达到它就报'特化没有到达不动点'"。第 3 条已落地
（`bb4e01be8`：删 `kMaxTypeSpecializationsPerRecursiveFunction`，换 `kTypeSpecializationFixedPointGuard
= 4096` 的 `throw`）。本文是落地之后的一条新读数：**第 2 条的放宽在某一条路径上没有生效**。

## 一、归属读数

| 树 | `export_fold` |
| --- | --- |
| 翻转前（develop 线，6108/6087/fail 2） | 绿 |
| 翻转批 `228a2ba6a`（73 红那批） | **绿** |
| 去上限 `bb4e01be8` 之后 | **红** |

也就是说它不是翻转造成的，是"去上限"造成的：8 那个数在这条路径上确实是截断，截断去掉之后它到不了
不动点。

## 二、读数

- plan 口径：`timeout_ms` 抬到 60000 也在 60 s 内不返回（60.04 s 被超时杀掉，`expected exit 0,
  got null`）。我已把它还原成 10000，**测试一行未改**。
- 主机直跑（`/usr/bin/time camel test/cases/modules/onnx/export_fold.cml`）：

  ```
  error[RuntimeDiag::RuntimeError]: ... onnx.export_model: the type specialization of 'unroll'
  did not reach a fixed point: 4096 instances, each with a key the one before it did not have.
    --> test/cases/modules/onnx/export_fold.cml:30:5
  WALL 256.99 s
  ```

  基座同一条 <10 s 且绿。
- 对照（说明守卫不是普遍代价）：同一批里 `specialize_growing_shape.fold_specialize.bounded` 的读数
  是不动点 **1 个实例**（`GRAPH_STATS {"graphs":99,...,"specializations":1}`）；`shape_unroll.simplify.rgir`
  在新读数下也是自调用复用，绿。问题只在 `unroll` 这条路径上。

## 三、问题

1. 裁定第 2 条说"键不同就把 extent 与维度名放宽为 `?`"，那么在 `unroll` 这条路径上放宽**为什么没有
   停下来**：是它的键里还有 extent/维度名之外的成分（例如元素类型或别的实参），还是这条路径根本没
   走到放宽（例如键的差别在放宽管不到的位置）？——需要作者或 h468/h463 lane 定性，本 lane 不动
   `draft_inline.cpp`。
2. 守卫的 `throw` 是**对的行为**（不静默停在截断形状上，裁定第 3 条要的就是这个），但它把一个翻前
   绿的用例变成红、代价 257 s。终态二选一：
   - (a) 第 2 条的放宽规则覆盖这条路径（守卫留作安全网）；
   - (b) 这条递归确实没有不动点，导出侧应在花掉 257 s 之前用它自己的诊断拒绝（今天只有守卫在
     4096 个实例之后事后报错）。