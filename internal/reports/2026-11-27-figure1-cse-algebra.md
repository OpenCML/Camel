# Figure 1 的读数：`cse` 把两处同形合成一颗，`algebra` 才匹配到同一个 `x`

`D-1127-265` 改口径后要的那一个例子。全部走命令行流水线，不碰"遍当值"那条崩溃路径（`task-353`）。

## 环境

```
[provenance] tip 0136257d1580f61e1cd4fb93b7dd2571c9b2148e | binary built 2026-11-27T23:41:00+08:00 | tier fast | jobs 2
0136257d15 = Merge fsh-dev: algebra, the algebraic simplification, is a pass of the opt module
src/stdlib/opt.cml:346  export func algebra::<F>(f: F) => F! when is_function::<F>()  （= rules::simplify）
```

程序（仓库外 `/tmp/paper-1127-255/fig1.cml`）：

```caml
func f(x: int) => int { return (x + 1) * 3 + (x + 1) * 4 }
func main() => int sync { println(f(2)); return 0 }
```

`camel fig1.cml std::fvm` → 打印 `21`，EXIT=0（程序本身正确）。

## 三张图

### ① 前：`camel fig1.cml std::gir std::nop`

```
N00 [label="x", type="PORT", value_type="int"]
N01 [label="1", type="DATA"];   N02 [label=":op/add_l", type="OPER"]     ← 第一个 x+1
N03 [label="3", type="DATA"];   N04 [label=":op/mul_l", type="OPER"]
N05 [label="1", type="DATA"];   N06 [label=":op/add_l", type="OPER"]     ← 第二个 x+1（另一颗节点）
N07 [label="4", type="DATA"];   N08 [label=":op/mul_l", type="OPER"]
N09 [label=":op/add_l", type="OPER"]
```

`f` 的图 10 个节点；`x` 分别喂 N02 与 N06 —— **两处书写就是两颗节点**。

### ② `camel fig1.cml opt::cse std::gir std::nop`

```
N00 x | N01 1 | N02 :op/add_l | N03 3 | N04 :op/mul_l | N05 4 | N06 :op/mul_l | N07 :op/add_l
N00->N02, N01->N02, N02->N04, N03->N04, N02->N06(共用), N05->N06, N04->N07, N06->N07
```

8 个节点；两处 `x+1` 合成一颗 N02，被 N04 与 N06 共用。

### ③ `camel fig1.cml opt::cse opt::algebra std::gir std::nop`

```
N00 [label="x", type="PORT"]
N01 [label="1", type="DATA"]
N02 [label=":op/add_l", type="OPER"]              ← x+1
N03 [label="7", type="DATA", value_type="int"]    ← 3+4 折成常量 7（遍里新造，无 span/origin）
N04 [label=":op/mul_l", type="OPER"]              ← (x+1)*7
```

4 个节点：`x → (+1) → (*7)`。

### ③′ 对照：`camel fig1.cml opt::cse std::cxx::algebra std::gir std::nop`

与 ③ **逐字节相同**：`cmp fig1-algebra.dot fig1-cxx.dot` → IDENTICAL（exit 0）。

⇒ "律只写一处"在这个语料上成立，证据是**图逐字节相同**，不是计数相等。

## 可见的活（节点数，`f` 这一簇含 ARGS/EXIT 行）

| 阶段 | 行数 | 变化 |
|---|---|---|
| 前 | 42 | — |
| `opt::fold`（单独跑） | 42 | **一字未动**（与"前"`cmp` IDENTICAL） |
| `opt::cse` | 38 | 两处 `x+1` 合成一颗（10 → 8 节点） |
| `opt::cse opt::algebra` | 31 | `a*3 + a*4` → `a*7`（8 → 4 节点） |
| `opt::fold opt::cse opt::algebra` | 31 | 与上一行**逐字节相同**（`cmp` IDENTICAL） |
| `opt::cse std::cxx::algebra` | 31 | 与上面两条逐字节相同 |

三条反面证据：

```
A) camel fig1.cml opt::algebra std::gir std::nop（不带 cse）→ 10 个节点，与"前"逐字节相同
B) camel fig1.cml opt::fold std::gir std::nop             → 与"前"逐字节相同（fold 单独跑，图一字未动）
C) camel fig1.cml opt::fold opt::cse opt::algebra …       → 与 opt::cse opt::algebra 逐字节相同
```

**所以"每一步都有可见的活"这句话要写准，不能一律这么说：**

- `cse`：**有活**（10 → 8，两处 `x+1` 合成一颗 N02，被 N04 与 N06 共用）；
- `algebra`：**有活，但前提是 `cse` 先合并**（8 → 4）；不带 `cse` 时 10 颗不动 —— 律匹配不到同一个 `x`；
- `fold`：**在这条语料上无活**；`(1+2)` 这种字面量在**构建期**就已折成 `DATA 3`，`fold` 遍到不了它。

⇒ 论文的写法：**"`fold` 的活发生在构建期（字面量到遍这一层时已经是图里的常量）；到遍这一层，`cse` 与 `algebra` 才有可见的变化。"** 既不夸大也不含糊。

## 相关读数（同一时期的对照用例）

`lane/bang-suffix @ 3a0fceef56`（`task-351`）：`cxx_isolation` 15/15 全绿；

```
egraph 文档两侧 sha256 相同：single_x 1121 B c75ff3d1… ；two_writes 1335 B a0a2f4fa…
display 文档两侧 sha256 相同：single_x 3947 B bbf4f4c8…
two_writes 赛前控制：只跑 algebra ⇒ 17 节点 3 add / 2 mul、常量 3 与 4 都在；
                    cse+algebra ⇒ 12 节点、1 add / 1 mul
expectedFlags：预期 0,0,0,2,2（猜）→ 实测 2,2,2 ⇒ opt::algebra 与 C++ 一样每张图都提交
```

⇒ **`fold` 那对不能跨侧比、`algebra` 这对能比，原因是两个遍的提交行为不同，不是文档不同。**