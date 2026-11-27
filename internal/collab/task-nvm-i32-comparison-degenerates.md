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