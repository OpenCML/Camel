# 二分：`small_calls` 在 `std::jit` 上的退化落在哪一笔

被二分的格：`small_calls.cml` × `std::jit`，读数取 **`first`（一个全新进程里第 1 次执行那个 pass）**。

为什么取 `first` 而不是 `steady`：`run-timing.mjs` 的读数显示，在当前点上这一格**在同一个进程里越跑越慢**
（`1752, 2147, 2799, 2857, 2902`），审计点与阶段 2 之前都是平的（`115, 114, 112, 111, 111`）。
用 `steady` 二分会把「漂移」当成「变化」。

两端：`1fa180c4f` first = **115.1 ms**，`6fcee16d0c` first = **1751.7 ms**（同一台机器、同一轮、同样 contended）。
判定阈值 400 ms —— 离两端都远。

## 四轮，逐笔读数

`bisect.mjs` 每步都是「checkout → 构建 → 量一格」。每轮都是手动起一个新的 `lo..hi`，
因为 `b0caf544b7` 单独构建不通过（见最后一节），二分不能把它当成可判定的点。

### 第 1 轮：`1fa180c4f` .. `6fcee16d0c`（1932 笔）

| 步 | 提交 | first ms | 判定 |
|---|---|---|---|
| 1 | `d48df71f6a` | 1858.1 | 坏 |
| 2 | `bc717f7d06` | 122.7 | 好 |
| 3 | `80796d1c29` | 1910.8 | 坏 |

第 4 步的 checkout 被拦住：每次构建会改写 `src/kernel/include/camel/core/error/diagnostics/messages/` 下三个生成的头文件，
工作树于是变脏。驱动改成先 `git checkout -- .`（只丢跟踪文件的改动，不 `clean`，不动构建产物）再 checkout。

### 第 2 轮：`bc717f7d06` .. `80796d1c29`

| 步 | 提交 | first ms | 判定 |
|---|---|---|---|
| 1 | `5e8eb04d5d` | 1837.0 | 坏 |
| 2 | `8b0948e433` | 1922.5 | 坏 |
| 3 | `ce7f79bd9d` | 1971.9 | 坏 |
| 4 | `181a33cb67` | 2025.9 | 坏 |
| 5 | `1de6c6df29` | **123.2** | 好 |
| 6 | `a23a6f46d0` | 2164.7 | 坏 |
| 7 | `17a0514283` | 2001.9 | 坏 |
| 8 | `76f5cfea5b` | 2004.1 | 坏 → 收敛 |

**收敛到 `76f5cfea5b`** `2026-11-23 16:25:38 +0800 merge develop: one facts store per context, the exec plan belongs to the module that reads it, and a range's ends are bounds`。
它的一侧是好的（`1de6c6df29`，123.2 ms）。于是二分进它带进来的那条线。

### 第 3 轮：`7e344fdea1` .. `a41a7f221`（57 笔，被合进来的那条线）

| 步 | 提交 | first ms | 判定 |
|---|---|---|---|
| 1 | `1c53d16bd4` | 119.7 | 好 |
| 2 | `7172ec564c` | 120.5 | 好 |
| 3 | `78221e9f1c` | 2018.8 | 坏 |
| 4 | `1efa9e4bb6` | 119.8 | 好 |
| 5 | `a14bf9ab38` | 2050.8 | 坏 → 收敛 |

**收敛到 `a14bf9ab38`** `2026-11-23 16:01:39 +0800 merge lane/cleanup`。它的第二父 `1efa9e4bb6` 是好的，
第一父那一侧 `d492be1360` 差 9 笔。

### 第 4 轮：`b6c3aa3204` .. `d492be1360`（9 笔）

| 步 | 提交 | first ms | 判定 |
|---|---|---|---|
| 1 | `19327493e2` | 1890.3 | 坏 |
| 2 | `b0caf544b7` | —— | **构建失败**，不是好也不是坏 |

## 结论：落在一组**一起才编译得通**的提交上，不是单笔

`b0caf544b7` 单独构建不通过，报的是它的使用方还没跟上：

```
src/stdlib/std/nvm/nodevm.cpp:321: error: no member named 'nodes' in 'camel::runtime::ExecPlan'
src/stdlib/std/nvm/nodevm.cpp:679: error: no member named 'resolvedOpers' in 'camel::runtime::ExecPlan'
src/stdlib/std/fvm/compile.cpp:141: error: no member named 'countedSites' in 'camel::runtime::ExecPlan'
```

完整日志：`results/bisect/bisect-build-failure-b0caf544b7.log`。

所以「那一笔」是一组，它们一起在 `76f5cfea5b` 落地，**共同的题目是：执行计划从图上的一个字段，变成
事实存储里的一条不可变事实，于是原来写在计划里、跑的时候读的东西，现在没有地方可写。**

| 提交 | 它把什么搬走了 |
|---|---|
| `fef8fa378d` | `GCGraph::execPlan_` 这个字段删掉；读计划改成向事实存储 `execPlanOf` 问，键是图的版本；计划是 `Derived` 种类，算一次存一次 |
| `b0caf544b7` | `std.execPlan` 这个种类由 `std::exec` 注册，计划的值「是整的」（不可变） |
| `68f6844368` | FastVM 曾往那个计划里写四样东西（字节码起点与长度、JIT 入口、标志、间接调用离开的次数）外加租约和 pc→结点表；计划成了不可变事实，这些写不进去了 |
| `19327493e2` | nvm / prl 曾把「一个算子结点解析出的函数」写回 `ExecPlan::resolvedOpers`，注释写着理由：**解析可能要导入模块，答案值得留着，结点再跑一次就不用再查 URI**；现在这答案改放在后端自己的表里 |

## 为什么会慢 16 倍（这是解释，不是测量）

上面四条合起来的意思是一条：**原来「算一次、存在计划里、每次执行直接读」的东西，现在每次执行都要重新问一次存储、
或者查一次后端自己的表。** `small_calls` 是 2e6 次尾递归调用、每次两个小函数调用，于是这多出来的一次查表要付 400 万次。

`std::jit` 掉得最多（115 → 1752 ms）而 `std::fvm` 几乎没动（96.4 → 96.3 ms），与「JIT 入口 / 字节码起点
原先写在计划里、现在要另找」正好对上；`std::nvm` 与 `std::prl` 掉 25–40%，与 `resolvedOpers` 那条对上。
**这一条是假设，按提交信息与哪一格掉得最多推出来的，没有在代码里逐行验证，也没有做「改回去再量」的阳性对照** ——
交接一（只量，不改）不做那个；这是交接二要做的第一件事。

## 没做到的部分

- 只二分了这一格（`small_calls` × `std::jit`，掉得最多的那一格）。`tensor_train` 的 +100% 没有单独二分，
  但它落在同一个区间，且四个后端一起动，最可能同一组提交，**未验证**。
- `fib` × `std::nvm` 的 +54% 同理，未单独二分。
- `1fa180c4f` 与 `6fcee16d0c` 的构建选项相同（`3cb13550dff5`），所以这条区间是干净的；
  审计点不在其中（它的选项哈希是 `97805b1e103e`，见 `build-stamps.md`）。