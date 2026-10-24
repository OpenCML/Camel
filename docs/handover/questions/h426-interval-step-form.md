# 带步长的 `range(a, b, step)` 在语言里不存在：4.2 的“一件事一种写法”只落地了一半

Author: 车道 `handover/h426-interval`（`D:\Projects\Camel-wt\h426-interval`），2026-10-24。Status: open — 需要负责人裁定：带步长的整数序列**由谁、以什么写法**承担（补上 `range(a, b, step)`，还是把 4.2 那句改掉）。测量在 Linux 主机（`88zjwei`）上、`origin/develop`（`69f183330`）与本车道改动同一构建窗口（`261024_121510_69f183330_dirty`）内完成，读数走 `camel-remote.ps1` 与计划运行器。

## 数字先

- 裁定（`docs/handover/reviews/questions-2026-10-23.md:392`，`### h248-generator-and-composition`）逐字要求：`a..b` “取代两参数的 `range(a, b)`（删除，一件事一种写法），**带步长写 `range(a, b, step)`**”。
- 实测（`CAMEL_ERROR_FORMAT=short`，同一命令）：`range(0, 10, 2)` → `SemanticDiag::NoMatchingFunction`，`code=0x0300000F`，逐字 “No matching function found for argument types: `<> (int, int, int)` … Available overloads: `<:struct/range>: (start: int, end: int) => int[]`”。
- 即：**带步长那一半在语言里根本没有实现**。本切片的任务只要求删两参数形式并让 `for i in 0..n` 融合，所以我**没有**新增三参数重载，`range(0, 10, 2)` 仍是 `NoMatchingFunction`。
- 名字相近但**不是**这件事的：`stdlib/signatures/tensor.cml:53` 的 `export func range(start: int, stop?: int, step?: int): Tensor<int64>`，`tensor::range(0, 10, 2)` 给 `tensor([0, 2, 4, 6, 8], dtype=int64)`，是张量模块自己的函数（C++ 在 `modules/tensor/ops/creation_ops.cpp`），不是 `int[]` 的区间。它不归两参数删除管，也未被我改动。

## 我落地了什么（本切片）

删两参数 `range(a, b)`（新诊断 `SemanticDiag::RangeTwoArgumentsRemoved`，`0x0001E3`，指向写下的名字），迁移全部可改调用点到 `a..b`（142 个 `.cml`、224 行；另有 3 个**生成器**里拼 Camel 源码的 7 行：`test/tools/collfuzz/collfuzz.py`、`test/cases/modules/std/array/sort_reference.py`、`test/tools/perfbaseline/scale_programs.py`），`for i in 0..n` 由编译器融合、不物化（图上没有 `:struct/range`、没有 `int[]`；2,000,000 轮在 8 MB 堆上限下运行，而同一个区间绑定到名字时 16 MB 的分配被 `RuntimeDiag::MemoryExhausted` 拒绝）。`range(a, b, step)` 保持原样。

## 为什么需要裁定

两条路都能让“带步长”与 4.2 一致，代价不同：

- **(a) 补上语言写法**：在 `:struct/range` 旁边加 `range(start: int, end: int, step: int) => int[]`（或把步长写成区间自己的语法）。要定的语义至少有：`step == 0`（空？拒绝？）、`step < 0`（`b <= a` 的方向反过来的半开区间？与今天 `b <= a` 给空数组的规则如何并存）、`b` 与 `a` 的同余边界、以及**是否同样融合**（`for i in 0..n step 2` 要不要也进 loop 的两个槽位——那要改 `LoopIterableForm` 的载荷与 `exec_plan` 的槽位布局）。这些 4.2 都没写，我不擅自定。
- **(b) 改文档**：把 4.2 那句“带步长写 `range(a, b, step)`”换成别的写法（例如由数组高阶算子或 `tensor::range` 承担），即承认“整数区间的步长版本”这一件事不在语言里。这需要作者动 `docs/design-principles.cn.md:259`（我不改作者页面）。

我的建议：先按 (b) 收口（本切片已保证“一件事一种写法”：两参数已删，`a..b` 是唯一的整数区间写法），把带步长当一个**新**的未决条目交给作者，而不是让代码继续与 4.2 不一致——现在这句话读起来像“已经有了”，实测却是 `NoMatchingFunction`。

## 同族、我测过但**没有**动的地方

1. **作者页面仍有被删写法的旧句子**（`docs/language/**` 属作者，按规约我不动）：`docs/language/06_expressions.md:537` 逐字 “**数组**：元素本身，`range(a, b)` 就是一个数组。”；`:523` 的循环语法行与本节的序列清单（数组/字符串/映射/张量）没有“直接写下的区间”这第五种，而融合之后它确实是一种（`for i in 0..n` 不建数组，`for i in r`（`r = 0..n`）建）。`docs/design-principles.cn.md:259` 那句“带步长写 `range(a, b, step)`”同上。
2. **同名函数与内建同名时，内建赢**（本切片实测，与裁定那句括号里的说法不完全一样）：`import { range } from tensor` 之后，**两参数**的 `range(0, 5)` 仍然解析到内建 `:struct/range`，于是被新诊断拒绝——这不是推理，是 `registry.docs_complete` 在张量模块自己的 `@example range(0, 5)` 上打出来的（该示例的包装源码确实按名字导入了 `range`）。只有**一参数**（`range(6)`）和**三参数**（`range(0, 10, 2)`）的无限定调用、以及限定名 `tensor::range(0, 5)`，才落到张量模块的 `export func range`。因此我按这条实测规则把 12 个张量文件里的**两参数**调用也迁到 `a..b`（8 个文件 9 行），只留下单参数的；张量模块自己的函数一行未改。裁定那句“一个模块自己的区间类函数按它声明的方式调用”在**限定名与其它元数**上成立，在“无限定两参数”上不成立——是否要让模块导入的同名函数压过内建（或让内建只在不产生歧义时赢），留给作者。
3. **单参数 `range(6)`** 在 4 个测试文件里是张量模块的（都按名字导入，如 `test/cases/modules/optim/training.cml:17`），我未迁移：它们不是两参数的整数区间。
4. **`showcase/*/README*.md` 里的 `range(...)`** 位于表格的 **Python/PyTorch 列**（如 `showcase/c1-linear-logistic/README.cn.md:92` 的 `for step in range(steps)`），不是 Camel 写法，未动；`benchmarks/**/*.py`、`test/tools/**/*.py` 的 `range(` 同理。

## 我没有做

没实现 `range(a, b, step)`（含“融合的带步长区间”）；没动 `docs/language/**`、`docs/design-principles*.md`、`docs/design/**`、`docs/handover/reviews/**`、`docs/handover/README.cn.md`；没给张量模块的 `range` 改名或加别名；没把 `a..b` 推广到非整数（裁定只说整数）。