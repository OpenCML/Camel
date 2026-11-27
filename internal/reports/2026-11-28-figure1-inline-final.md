# Figure 1（终点版）：inline → cse → algebra，三步各有可见的活，两个入口给出同一张图

**读数所挂的树与二进制**

```
tip      1ff90d06982f772a44b7265899279ae7b38e9ab9
binary   built 2026-11-28T00:43:27+08:00
tree     /home/zjwei/camel-work/promote        tier fast   jobs 2     BUILD_SECONDS 15.0
```

**被量的程序**（`/tmp/paper-1127-255/fig2.cml`，仓库外）

```camel
func inc(x: int) => int { return x + 1 }

func f(x: int) => int { return inc(x) * 3 + (x + 1) * 4 }

func main() => int sync { println(f(2)); return 0 }
```

`camel fig2.cml std::fvm` → 打印 `21`，退出码 `0`。

---

## 一、四步的读数（`GRAPH_STATS` 原文里的 `nodes` / `direct_calls`）

| 流水线 | nodes | direct_calls |
|---|---|---|
| 无 | **19** | 2 |
| `std::cxx::inline` | **28** | **0** |
| `std::cxx::inline opt::cse` | **24** | 0 |
| `std::cxx::inline opt::cse opt::algebra` | **18** | 0 |
| `std::cxx::inline std::cxx::cse` | 24 | 0 |
| `std::cxx::inline std::cxx::cse std::cxx::algebra` | 18 | 0 |

每一步的活：

- **inline**：把 `inc(x)` 这颗 `FUNC` 调用节点展开成它体里的 `x + 1`（19 → 28 节点，直接调用 2 → 0）。
  不展开，后面两步没有东西可做。
- **cse**：把展开出来的那颗 `x + 1` 与源码里写的 `x + 1` 合成一颗（28 → 24，−4 个节点）。
- **algebra**：在同一颗节点上把 `a*3 + a*4` 并成 `a*7`（24 → 18），`f` 收成

```
N03 [label="x", type="PORT"]
N04 [label="1", type="DATA"]
N05 [label=":op/add_l", type="OPER"]        ← x + 1
N06 [label="7", type="DATA", value_type="int"]   ← 3 + 4
N07 [label=":op/mul_l", type="OPER"]        ← (x+1) * 7
```

即 `x → (+1) → (*7)`。

## 二、两个入口给出同一张图（逐字节相同）

```
$ camel fig2.cml std::cxx::inline opt::cse opt::algebra std::gir std::nop            > camel.dot
$ camel fig2.cml std::cxx::inline std::cxx::cse std::cxx::algebra std::gir std::nop  > cxx.dot
$ cmp camel.dot cxx.dot && echo IDENTICAL
IDENTICAL
```

**库里的名字（`opt::cse` / `opt::algebra`）与核心的名字（`std::cxx::cse` / `std::cxx::algebra`）
在同一条流水线上给出**逐字节相同**的图：律只写一处，两个入口是同一件事。**

## 三、两次书写合成一颗：最短证据

```camel
func inc(x: int) => int { return x + 1 }
func f(x: int) => int { return inc(x) * 3 + inc(x) * 4 }   // 两处都来自 inc（fig3.cml）
```

```
std::stats                        18 nodes
std::cxx::inline                  28 nodes
std::cxx::inline opt::cse         24 nodes      ← 两次书写合成一颗
```

这排除了"内联体与源码体不同形所以合不了"的解释：两处形态完全一样，`opt::cse` 照样合。

## 四、库里的 `opt::inline` 也参与这张图（`D-1128-28`）

上面一至三节量的是 `std::cxx::inline` 那一侧。库里用 Camel 写的 `opt::inline` 落地后，
**同一条流水线换成它，四个数字逐个相同、最后一张图逐字节相同**。

读数所挂的树与二进制（`opt::inline` 合进 integration **之前**的枝尖）：

```
tip       4fc2dcd735d8d7ba7dd0ad409faba0d858104c02   (lane/opt-inline)
binary    built 2026-11-28T04:22:21+08:00
build-info revision 4fc2dcd735…  dirty false
```

被量的程序与第一节相同（`fig1.cml`），`camel fig1.cml std::fvm` → 打印 `21`、`RC=0`。

| 流水线 | nodes | direct_calls |
|---|---|---|
| 无 | **19** | 2 |
| `opt::inline` | **28** | **0** |
| `opt::inline opt::cse` | **24** | 0 |
| `opt::inline opt::cse opt::algebra` | **18** | 0 |
| `std::cxx::inline opt::cse opt::algebra` | 18 | 0 |

四个数字与第一、二节**逐个相同**（`19 / 28 / 24 / 18`，直接调用 `2/0/0/0`）。

两个入口给出同一张图：

```
$ camel fig1.cml opt::inline opt::cse opt::algebra std::gir std::nop            > opt-inline.dot
$ camel fig1.cml std::cxx::inline opt::cse opt::algebra std::gir std::nop       > cxx-inline.dot
$ cmp opt-inline.dot cxx-inline.dot && echo IDENTICAL
IDENTICAL          （两份 3543 字节逐字节相同）
```

第三张图的末尾仍是 `x → (+1) → (*7)`，`main` 里是 `(2+1)*7 = 21`。

**三段各自的活**（实测依据，同一份 `std::gir` 原文）：
`opt::inline` 把 `inc(x)` 展开成 `x + 1`（`add_l` 计 7 处：`f` 2 + `main` 2 + `inc` 自身 1 …）；
`opt::cse` 把两份 `(x+1)` 合成一处共享（`add_l` 7 → 5）；
`opt::algebra` 把 `*3 + *4` 折成 `*7`（`mul_l` 4 → 2）。
**`inc` 自己的图在三段里始终是 3 个节点** —— `inline` 只复制体、不删 callee，删是 `dce` 的事。

> 待补：`opt::inline` 合进 integration 后，在尖端复跑本页四条管线 + `cmp` + 四组 `stats`，
> 贴新 tip 与 `binary built`，并确认四个数字不变。

## 五、为什么之前不成立（一行留档）

23:5x 在同一棵树的更早尖端（`0136257d15` / `c88ac04a47`）上，`opt::cse` 对内联造出的节点一个都不合
（`std::cxx::inline opt::cse` 仍是 28 nodes）。`8c6704cb22`（"the sweep keys equal operations by the control
region, so an inlined one merges"，`opt.cml` 改成按控制 region 链取键）之后不再复现 —— 本页第一、二节的读数
就是在那一笔之后的尖端上取的。