# 卡：nvm 上 `i32` 与 `i32 + 0` 的比较退化成 `false`（`==`、`<`、`>` 全是 `false`）

**发现者**：`lane-parked-rebase`（只读探针），起点是 `lane-misc` 报的 `abs(INT_MIN) == INT_MIN` 在 nvm 上为 `false`。

## 现象（`perf` 树，`--detach origin/integration`，`BUILD_EXIT=0`，`binary built 2026-11-28T07:23:53`）

```
let m: i32 = -2147483647 - 1

println(m == m)        nvm true    fvm true
println(m == m + 0)    nvm false   fvm true     ← 同一个变量、同一个值
println(m + 0 == m)    nvm false   fvm true     ← 左右对调仍 false
println(m < m + 0)     nvm false   fvm false
println(m > m + 0)     nvm false   fvm false
println(abs(m) == abs(m)) true / true
```

- 不走 `abs` 的对照：`println((0 - m) == m)` ⇒ **nvm false / fvm true** ⇒ `abs` 无辜。
- "计算值 vs 计算值"成立：`abs(m) == m + 0` 两后端 `true`，`(0 - m) == (m + 0)` 两后端 `true`。
- `int`（64 位）宽度没有这个反常。
- 减法按值算，所以 `abs(m) - m` 在 nvm 上仍是 `0`（这解释了为什么只写"值 + 差"的用例看不见它）。

## 最可能的方向（未证实）

`0` 这类整数字面量的类型更宽（`int` 是 64 位），`m + 0` 于是是 `i64`，`m == m + 0` 就是 **`i32` 与 `i64` 比较**：
`fvm`/`jit` 走提升（`true`），`nvm` 没有提升、比较落到默认 `false`。**`<` 与 `>` 同时 false** 正是"落默认值"的形状（铁律 13：失败时落到哪、那个值会不会刚好看起来对）。
次可能：nvm 的寄存器/标签对"变量槽 vs 计算槽"不同源，比较按标签而不是按值。

## 判定标准（先证方向，再动手）

1. **宽度假设**：`let z: i32 = 0; println(m == m + z)`（`i32 + i32`）—— 若 `true`，宽度假设成立；若仍 `false`，方向改为"槽/标签"。
2. **显式类型**：`println(m == (m + 0) as i32)` 与 `let w: i64 = m as i64; println(m == w)`，看哪一侧才是问题。
3. **标签假设**：`std::jgir` 落盘到文件（`onnx::export` 同形的写法或 `--out`），读两侧槽的 `type`/标签；**不要**把整图印到 stdout（会被 runner 截断）。
4. **三后端**：`nvm` / `fvm` / `jit` 都要；四后端若便宜也加 `prl`。

## 判据

- 修好之后：`m == m + 0`、`m < m + 0`、`m > m + 0` 在 **nvm/fvm/jit 三后端一致**且**语义正确**（`==` true、`<` false、`>` false）。
- 常设用例进 `test/cases/`（含 `i32`、`i64`、显式窄化三组），并在计划里跑三后端。
- `feat/compile`、`feat/modules`、`feat/trans` 对 `2026-11-28-promotion-67-reds.tsv`（147 名、两键）**新增红 0**；`[provenance]`/`binary built`/`numstat`/`BUILD_EXIT=0` 同一次。
- **不许**为了让比较"看起来对"而在解析层套强制转换把问题藏起来 —— 若真是 nvm 缺提升，修在 nvm 的比较/提升那一层；语义（整数提升规则）以设计原则为准，原则没写的先报决策者。

## 状态

- 2026-11-28 07:3x：卡建立，派给 `lane-witness`（先证方向，再修）。
- `lane-misc` 的 `abs(INT_MIN)` 正式用例**只断言"值"与"差为 0"**，`==` 那条在定因与修复之前不进断言。

## 定位（2026-11-28 07:4x，`lane/nvm-int-compare`，未打 gate、未改代码）

按"先证方向再动手"的顺序走，卡在这一步：**执行点已收窄到两处，但还没读到"位型 + 标签"的原文**。

已排除的落点（逐个 grep 过的原文）：

- `src/stdlib/std/nvm/nodevm.cpp` 及 `nvm/*.cpp|*.h`：**没有 `Less`/`Eq`**（`git grep -n 'Less' -- src/stdlib/std/nvm` 零命中）⇒ 比较不在这层的手写 switch 里。
- `src/kernel/execute/pass_spec.cpp:349` 的 `case Op::Less:`：那是 pass 规格表里的算子登记，不是求值。
- `src/stdlib/std/builtin/executors/builtin_semantics.cpp`：整文件是**声明**（稳定键、数值语义、fault 表），`git grep -n 'i32'` 只命中文件头注释第 5 行 ⇒ **不带求值**。所以你给的"在 Less/Eq 条目打 gate"落点不成立。
- 真正的入口在这张表：`src/stdlib/std/builtin/executors/builtin.cpp:261-266` — **`op/eq_i`（i32）与 `op/eq_l`（i64）都指到同一个 `__builtin__eq__`**，`op/lt_*`、`op/gt_*` 同形。这才是"两个不同宽度的槽进同一个比较函数"的地方。
- `__builtin__eq__` 的定义**不在** `builtin.cpp` 里（该文件只有函数指针表与 `#include "builtin.h"`），下一手第一步：`git grep -rn '__builtin__eq__' -- src/stdlib/std/builtin | grep -v '{"op/'` 找定义，再在**定义那里**打位型 + 类型标签的 gate。

判据（下一手照这个打，一次就够）：

```cpp
// 在 __builtin__eq__（以及 lt/gt 同一个实体）入口，按你要读的两侧各打一行
CAMEL_LOG_INFO("gate: lhs bits=0x{:016x} type={} rhs bits=0x{:016x} type={}",
               (uint64_t)fromSlot<int64_t>(lhs), (int)lhs.typeCode(),
               (uint64_t)fromSlot<int64_t>(rhs), (int)rhs.typeCode());
```

`-1` 与 `-1 + 0` 跨两槽跑一次，看两种结论哪一种：
1. **位型就已经错**（i32 的 `0xFFFFFFFF` 与 i64 的 `0xFFFFFFFFFFFFFFFF` 白读成同一个、或按无符号比）⇒ 修在**取槽/提升**那一层；
2. **取对了比错**（两侧都读成 `-1`，比较仍返回 false）⇒ 修在**比较的返回路径**（落默认值），按铁律 13 查"失败时落到哪"。

状态：未打 gate（本格上下文用尽），未改代码，未跑三后端；**没有断言、没有读数**，卡不为它背书。