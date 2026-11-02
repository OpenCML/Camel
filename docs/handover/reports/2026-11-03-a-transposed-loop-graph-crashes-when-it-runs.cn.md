# 转置出来的循环图一旦执行就 SIGSEGV

**读数树**：`lane-portgate @ a1f648fb4`（`origin/develop 00c59b0c6` + 从 `lane-b2` 取来的四个代码文件
`modules/transforms/{transpose.cpp,transpose.h,transpose_operator.cpp}`、`stdlib/vjp.cml`）。
**Release 构建**，`out/latest/bin/camel <文件> std::nvm`。三份程序都在本节里可直接复制。

## 一、三个最小程序与它们各自的返回码

### G：转置一个直线函数，随后跑别的函数的正向

```caml
import { transpose } from transforms
import rulesets.transpose

func thrice(x: float): float {
    return loop s = x for i in 0..3 do s * 2.0
}

func quad(x: float): float {
    return loop s = x for i in 0..4 do s * 2.0
}

func twice(x: float): float {
    return 2.0 * x
}

func main(): int sync {
    let b = transpose(twice)
    println(b(1.0))
    println(quad(1.0))
    return 0
}
```

```
(2.0)
16.0
rc=0
```

### H：转置一个循环函数并调用它

```caml
import { transpose } from transforms
import rulesets.transpose

func thrice(x: float): float {
    return loop s = x for i in 0..3 do s * 2.0
}

func twice(x: float): float {
    return 2.0 * x
}

func main(): int sync {
    let t = transpose(thrice)
    println(t(1.0))
    return 0
}
```

```
rc=139   (SIGSEGV，一个数都没打印)
```

### C：两个循环函数、两个转置、一次正向

```caml
import { transpose } from transforms
import rulesets.transpose

func thrice(x: float): float {
    return loop s = x for i in 0..3 do s * 2.0
}

func quad(x: float): float {
    return loop s = x for i in 0..4 do s * 2.0
}

func main(): int sync {
    let three_back = transpose(thrice)
    println(thrice(1.0))
    println(three_back(1.0))
    println(three_back(2.0))
    println(quad(1.0))
    let quad_back = transpose(quad)
    println(quad_back(1.0))
    return 0
}
```

```
8.0
rc=139   (SIGSEGV，崩在 three_back(1.0)，第二个 transpose 的 enter 从未出现)
```

## 二、结论三条

**1. 建图无问题。** 给 `buildTransposeGraph` 打了 enter/exit 探针（`fn`、`base`、`baseSize`、`member`、
`made`、`groupProgram`、`groupSize`、`holds`），G、H、C 三个程序里每一次 `TRANSPOSE-DBG enter` 都有
配对的 `exit`，`holds=yes`，`groupSize` 正常增长；图内 `skip=` 一次都没有；操作行写的是
`TRANSPOSE op signature=… => (cotangent) => (cotangents..) resolved=(f: (x: float) => float pure) => (float) => (float,) transposed=(float) => (float,) graph=(float) => (float,) same=yes`
——**解析器与图两侧都自认为 `(float) => (float,)`，`same=yes`**。C 里**第二次 `TRANSPOSE-DBG enter` 从来没出现过**，
所以"第二次建图与第一次共享了什么"这个方向不成立。

**2. 崩在执行。** G 的转置被执行完（打 `(2.0)`），程序继续跑到 `quad(1.0)` 打 `16.0`；
H 的转置**一被调用**就 SIGSEGV，连正向数都没来得及打印。**转置直线函数完好，转置循环函数崩。**

**3. 不是"每进程只许一个转置成员"的设计残留。** G 里第一个转置建好、执行完，程序仍然正常；崩的发生与
"有几个转置"无关，只与"被执行的那个转置是不是循环图"有关。

## 三、一句话定位

**调用侧按 `(ct)` 答，图侧仍按端口收，`same=no` 这一处尚未收口 —— 这是 `a14312140` 那一笔没做完的设计收口。**

循环转置建出来的图 `nodes=11`（直线是 `nodes=3`），它把原循环的区域、边界、进位都搬进了新图；
直线图执行时端口数与调用实参数对得上，循环图对不上，于是运行期按端口读/写时踩到图外。`same=yes` 是
**类型**上的自洽，不是**运行期形状**上的自洽——两者在循环这条路上分叉了。

## 四、我方默认

**先修"转置循环图的运行期端口/状态映射"，不改成调用侧绕法**（不在 `vjp`/`transpose` 的调用处补参数，
也不把循环转置改成逐轮展开）。理由：绕法会让 `same=no` 永远存在，而下一次有人写多状态的循环还会踩同一处。

**例外**：若要修到 `jvp` 的循环提升（`pairTypeOf` 那一族）⇒ 停手，那条归 `splitlift` 的车道，两笔会撞。
## 五、修法与读数（2026-11-03 00:35）

**根因（`transpose.cpp:1985`，原文）**：`} else { result = loopNode; }` —— 单状态、无逃逸的循环，答案就是
loop 节点本身，一个裸 `float`。签名 `(..Tangent<ParamsOf<F>>)` 在任何 arity 都是元组，一个参数就是
`(float,)`。直线函数不炸，是因为它走构建末尾 `parts` 那一段，那里会自动包成元组；**循环这条抄了近路**。

**修法（8 行，照抄 `1613` 与 `1908` 已在用的写法，只改这一个分支）**

```cpp
result = tupleOf({loopNode}, {cotangentTypeOf(loopNode, Type::Float64())}).first;
```

多状态或有逃逸的循环不进这个分支（它们本来就经 `addCotangent` 收集、末尾成元组），所以只改一处；
没有动 `jvp` 的循环提升，也没有碰 `pairTypeOf`。

**三个程序重跑**

```
G 直线 transpose(twice) 后 quad(1.0)          (2.0) 16.0                     rc=0
H 循环 transpose(thrice) 后调用               (8.0) 8.0                      rc=0   （修前 SIGSEGV）
C 两个循环函数、三次转置调用                    8.0 (8.0) (16.0) 16.0 (16.0)   rc=0
```

**读数**：`feat/compile/transforms` 42 / 60（修前 41，`transpose_runs_a_loop_rounds_backwards` 换回
cotangent-only 写法后转绿）；`feat/transforms/split` 14 / 18；全量 fast `8526` 里 `8483` 过、`23` 红、
`killed by a signal` **0**。