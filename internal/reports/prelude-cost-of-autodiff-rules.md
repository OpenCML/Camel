# 前奏成本：`import autodiff` 现在多编译 `graph` 与 `rewrite`

**这条是给决策者对账清单第 8 条（前奏声明预编译缓存）的输入，不是新任务。**

## 事实

`stdlib/autodiff.cml` 现在声明了程序变换的三个标签（`#jvp(rule)`、`#transpose(rule)`、`#vjp(rule)`）和标量算术一族的 10 条 `export rule`。规则机制（`rule` 声明）由标准库的 `graph` 与 `rewrite` 两个模块提供，因此**声明 `autodiff` 时这两个模块也要编译**：任何 `import autodiff` 的程序，前奏都多出这两个模块的六类阶段。

实测（本分支 `out/latest`，`test/cases/modules/tensor/fusion_group_none.cml`，`std::profile top=0`）：

```
  resolve graph                              7.9 µs
  cmo_load graph                           12.51 ms
  resolve rewrite                           11.0 µs
  cmo_load rewrite                         641.5 µs
  parse graph                              668.3 µs
  gct graph                                584.8 µs
  resolve types                             11.5 µs
  (counting)                                10.7 µs
  declare graph                            391.1 µs
  parse rewrite                            392.9 µs
  gct rewrite                              399.2 µs
  resolve graph                             15.7 µs
  resolve types                              7.9 µs
  (counting)                                10.8 µs
  declare rewrite                          286.2 µs
  (counting)                                42.7 µs
```

合计 **≈16.0 ms**，其中 `cmo_load graph` 一项 12.5 ms（加载 `graph` 的已编译模块对象）。

## 对第 8 条的要求

前奏缓存不能只缓存"程序自己 import 的模块"，还要覆盖**声明模块通过 `rule`/标签间接拉进来的模块**——否则一个 `import autodiff` 的程序的缓存条目仍然会去编译 `graph` 与 `rewrite`，这条账不会因为缓存而消失。反过来，如果前奏缓存把 `graph`、`rewrite` 的编译结果一起收进缓存条目，这 16 ms 就不用单独再优化。

## 证据

`test/cases/modules/tensor/fusion_group_none.profile.golden.txt` 于 2026-10-29 为这件事重录了一次，diff 只有那 16 行阶段行；算子行、计数、字节数、hint、循环步数与程序打印的值都没有动，JSON golden 未改（重录理由也写在 `test/cases/modules/tensor/fusion_group_profile.py` 的头部注释里）。