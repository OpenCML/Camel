# 同一个 `t[idx]` 落到两个算子：解析的裂缝，与秩 ≥2 数值数组下标新引入的拒绝（实测，2026-10-25，h446 lane）

（前提：`docs/handover/reviews/questions-2026-10-24-evening.md` §loop-domain-through-the-tensor-iterator
（`for` 对数组由核心按第一维取元素）与同一份文件 §tensor-iter-path-left-on-develop。本 lane 只给读数与推断，
不替作者定"模块算子优先"。）

## 一、实测：`t[idx]` 的算子选择取决于索引的元素类型是否静态已知

本 lane 的 `fix(builtin)`（`33d006191`）把这条路对齐到"核心答秩-1 元素取、模块答其它秩/开放秩"，
依据是两者对**结果形状**的规则不同：核心的 `:op/idx_take`（`(arr: T[], indices: int[]) => T[]`，
`__idx_take_arr__`，`src/builtin/operators/struct.cpp:284-300`）按**槽**取（`arr->size()` 个槽，
答案类型就是输入类型 `norm[0]`），模块的 `tensor:idx_take`（`modules/tensor/ops/layout_ops.cpp` 的
`takeInfer`）从 indices 取形状、并按第一维取**行**（`k::take`：`input->dim(0)`、`rowBytes`）。
实测（`std::gir`）：`int[]` 打出 `int[]`（秩 1），`Tensor<f64>` 打出 `float[..]`（开放秩）。

读数（主机 `88zjwei`，本 lane 树 `out/latest`）：

| 程序 | 算子 | 读数 |
|---|---|---|
| `w: Tensor<f64>` + `labels: Tensor<i64>`，`sum(m.w[m.labels])` | 修复前是核心的 | `autodiff: no derivative rule for operator ':op/idx_take' of type 'float[..]'`；只给它补规则后，前向仍是核心的，拉回报 `take_grad expects a gradient shaped like the take result` |
| 同样的 `Tensor`（元素类型未述）+ `Tensor` 索引 | 模块的 | 直接绿（`modules/autodiff/gather_discrete_index_zero`） |

## 二、实测：新引入的拒绝（秩 ≥2 的数值数组用 `int[]` 下标、且没 import tensor）

`/tmp/probe_rank2.cml`（`func pick(xs: float[2, 3], idx: int[]): float[] { return xs[idx] }`，无 import）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (float[2, 3], int[]); operator '[]' is not defined for these operand types.
  --> /tmp/probe_rank2.cml:2:60
help: ... Available overloads:
          <:op/idx_arr>: (arr: T[], index: int) => T
          <:op/idx_take>: (arr: T[], indices: int[]) => T[]
          <:op/idx_str>: (str: string, index: int) => string
          <:coll/map_idx>: (m: Map<K, V>, key: K) => V?
```

**此前是静默的错误结果**：修窄之前核心的 `:op/idx_take` 会接这个调用，按槽取，也就是把秩-2 数组当
一维槽数组取（本 lane 没有为这个旧行为留下逐字读数——秩-2 常量数组今天**没有源形式**
（`rank-two-constant-array-has-no-source-form.md`），所以这个探针当时无法构造出值；只有间接读数：
`gather_grad_f64` 在"前向仍是核心的"那一步报 `take_grad expects a gradient shaped like the take result`，
即前向取出来的东西与 `w` 的 `[2, 3]` 不同形）。按设计原则，"拒绝"好于"静默错值"，但这是一处
**新引入的拒绝**，需要作者确认。

一次实验的读数（`Plan . -Tier fast`，把核心算子对**所有**数值数组让位，31 条变红；日志
`notes/h446-r3-layer-after.log`，行由计划器截断到约 250 字符）：

| 用例 | 逐字报文 |
|---|---|
| `stdlib.array_hof.{nvm,fvm,jit,prl,inline_opt,macro_opt}` | `out/latest/stdlib/array.cml:292:14: [Error]: No matching function found for argument types: <> (int[], int[]); operator '[]' is not defined for these operand types. [in the instance 'sort...` |
| `stdlib.array_large.*`（同 6 条） | 同上，位置 `stdlib/array.cml:292:14` |
| `attempt.library.*`（8 条，含 `.inline_opt.*`） | `out/latest/library/../stdlib/array.cml:292:14` 同报文 |
| `examples.algorithms.merge_sort.{nvm,fvm,jit,prl}` | 同上 |
| `subscript.forms.{nvm,fvm,jit,prl}`、`syntax.subscript_forms` | `test/cases/parse/syntax/subscript_forms.cml:18:52: [Error]: No matching function ... (int[], int[])`，候选表里只剩 `<:op/idx_arr>`、`<:op/idx_take>` |
| `poison:linear.subscript:subscript.forms.nvm` | 同上（一致性套件） |

31 条的完整名单见该日志的 `[FAIL]` 行；要点是它们都是**秩-1**数组（`int[]` 用 `int[]` 下标）且
**没有 import tensor**，所以模块算子不在候选表里——"对所有数值数组让位"会把这些程序的 `[]` 直接打断。

## 三、本 lane 的判断与推断（不是裁定）

1. 今天的收窄（`33d006191`）只把**秩 ≥2 或开放秩的数值数组**让给模块：秩-1 的 `int[]`/`float[]`/记录数组
   仍是核心的元素取（31 条因此全绿），`Tensor<f64>`（`float[..]`）、`Tensor<f64, [2, 3]>` 走模块行取。
2. 裂缝仍在：**同一个 `t[idx]` 因索引元素类型是否静态已知而落到两个算子**（`Tensor` 索引 → 模块，
   `Tensor<i64>` → 核心），两者对结果形状的规则不同。今天靠"按秩分工"把两者错开，是过渡期的补丁。
3. 推断（供作者参考）：终态大概要靠 **P4 别名**让 `Tensor` 与 `f32[..]` 成为同一个类型（
   `docs/handover/questions/tensor-alias-transition.md`），那时"两种拼写两个算子"的前提消失，
   一个 `[]` 只有一个规则；今天"秩 ≥2 未 import tensor 就无候选"的拒绝也应当随之消失或改写成一个
   具名诊断（现在落成 `SemanticDiag::NoMatchingFunction`，候选表里两条都 decline，看不出"这需要一个
   张量模块"这一层意思）。
4. 因此请作者裁：是否现在就立"模块算子优先于核心泛型解析"（本 lane 未做），还是接受按秩分工直到 P4。
   本 lane 不改这条；`33d006191` 的实测前后（63 → 61 红、无未预期新红）见 lane 报告。

## 四、读数口径（补注）

本页的读数取自本 lane 的树（`out/latest`）：本页最后改动 `ce1e1b90b`，与翻转线 tip 上的 blob 相同。
第一节的收窄与第二节的拒绝都跟着 `33d006191`，而它不是 `origin/develop` 的祖先
（`git merge-base --is-ancestor 33d006191 origin/develop` 退出 1）。