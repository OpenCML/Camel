# Figure 1（第二版）：构图不内联，内联之后 cse 才合、algebra 才开火

决策者 `D-1127-269` 换的例子。与第一版（`2026-11-27-figure1-cse-algebra.md`，`f = (x+1)*3 + (x+1)*4`，两个 `x+1` 是**同一段源码**写出来的两颗同形节点）相比，这一版把三步的活拆得更干净：**不内联时 `cse` 与 `algebra` 都无事可做**，因为 `inc(x)` 是一颗调用节点、`(x + 1)` 是另一颗，两颗形式不同谁都不等于谁。

```
[provenance] tip 0136257d1580f61e1cd4fb93b7dd2571c9b2148e | binary built 2026-11-27T23:41:00+08:00 | tier fast | jobs 2
[host] 88zjwei | camel-remote.ps1 | 本轮不是安静宿主（合并位全快层在跑）
```

程序 `fig2.cml`（本页全部读数用它）：

```caml
func inc(x: int) => int { return x + 1 }
func f(x: int) => int { return inc(x) * 3 + (x + 1) * 4 }
func main() => int sync { println(f(2)); return 0 }
```

`camel fig2.cml std::fvm` → `21`，`EXIT=0`。（2 → inc(2)=3 → 3*3 + 3*4 = 9 + 12 = 21）

## ① 流水线前：`inc(x)` 是一颗调用节点，与 `x + 1` 那颗不是同一个节点

`camel fig2.cml std::gir std::nop`（`EXIT=0`），`f` 那一张图：

```
N03 x(PORT) | N04 [label="inc", type="FUNC"] | N05 3 | N06 :op/mul_l | N07 1 | N08 :op/add_l | N09 4 | N0A :op/mul_l | N0B :op/add_l
N03 -> N04 (0|0)     把 x 交给 inc
N04 -> N06           inc 的结果乘 3
N03 -> N08, N07 -> N08   x + 1（另一颗 :op/add_l）
N08 -> N0A, N05 -> N06, N06 -> N0B, N0A -> N0B
GRAPH_STATS graphs 4 / nodes 19 / direct_calls 2
```

⇒ **`inc(x)` 是 `FUNC` 调用节点 `N04`，`x + 1` 是 `:op/add_l` 节点 `N08`**：形式不同、编号不同。**构图不内联** —— 这是这一页的第一个事实。

## ② 不内联时：`cse` 与 `algebra` 都无事可做（与 ① 逐字节相同）

```
camel fig2.cml opt::cse std::gir std::nop                → EXIT=0，cmp vs ① IDENTICAL
camel fig2.cml opt::cse opt::algebra std::gir std::nop   → EXIT=0，cmp vs ① IDENTICAL
GRAPH_STATS 三次都是 nodes 19
```

⇒ 这一步是"**没有活可干**"，不是"干了但看不出来"：图逐字节不动。

## ③ 内联之后：C++ 那条能合，Camel 的 `opt::cse` 不合 —— 本页的要害

```
camel fig2.cml std::cxx::inline std::gir std::nop                       → EXIT=0；nodes 28（direct_calls 2→0，inc 已展开）
    展开后 f 里同时有 N0C [label=":op/add_l"]（来自 inc 体，origin=8373）与 N07 [label=":op/add_l"]（源码里的 x+1，origin=8384）
camel fig2.cml std::cxx::inline opt::cse std::gir std::nop              → EXIT=0；cmp vs 上一行 IDENTICAL；nodes 仍 28
camel fig2.cml std::cxx::inline opt::cse opt::algebra std::gir std::nop  → EXIT=0；cmp 仍 IDENTICAL；nodes 仍 28
camel fig2.cml std::cxx::inline std::cxx::cse std::gir std::nop         → EXIT=0；nodes 24（−4）⇒ C++ 的 cse 合了
camel fig2.cml std::cxx::inline std::cxx::cse std::cxx::algebra …       → f 收成 N03 x / N04 1 / N05 :op/add_l / N06 7 / N07 :op/mul_l
                                                                        = x → (+1) → (*7) ✔
```

`cmp`：**Camel 那条与"内联后未优化"逐字节相同**；**C++ 那条与 Camel 那条 DIFFER**。

### 隔离实验：问题不在"内联体与源码体不同形"

`fig3.cml`：`f(x) => inc(x) * 3 + inc(x) * 4`（两处都由 inline 造出）

```
run: 21 / EXIT=0
baseline 18 nodes → inline 28 → +opt::cse 28（不动） → +std::cxx::cse 24（合并）
```

⇒ **凡是 inline 造出来的节点，`opt::cse` 都不合并**，与"是不是同一个函数体"无关。这是缺陷，不是"无事可做"：同一份图交给 `std::cxx::cse` 会合。

## 结论

1. **"构图不内联"成立**：`inc(x)` 是调用节点，与 `x + 1` 是两颗不同的节点。
2. **内联之前 `cse`/`algebra` 都无事可做**（逐字节相同）。
3. **内联之后，C++ 那条三步走通（`x → (+1) → (*7)`），Camel 这条第二步就停了**：`opt::cse` 对内联造出的节点一个都不合 ⇒ `opt::algebra` 也因此无火可开。
4. 因此 Figure 1 要"三步各有可见的活"，**必须先修 `opt::cse` 对内联节点的判据**（已立加急缺陷卡，见下）；此外 Figure 1 的最终流水线用 Camel 写的 `opt::inline`（决策者 `D-1127-270`，计划 `internal/plans/2026-11-28-opt-inline.cn.md`），落地后本页要再量一次并与 `std::cxx::inline` 那版逐字节比。

## 附：与本页同时看到的一条（不是本页结论）

`d.cml`（`passes::optimize` 的论文程序）在这台二进制上仍然 **139**（`Segmentation fault`，stdout/stderr 全空）。复现与根因定位在 `task-353` 那条线上，与本页无关。