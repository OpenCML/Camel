# 转置出来的循环图一旦执行就 SIGSEGV

**读数树**：`a1f648fb4`（原 `lane-portgate`，该分支已删；`origin/develop 00c59b0c6` + 从 `lane-b2` 取来的四个代码文件
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

## 六、同一处崩溃的第二个触发形状：给带闭包的那半答零（2026-11-03 07:10，`cd46c06de`，原 `lane-splitprimal`，该分支已删）

上面那条（裸 `float` 当元组）修完后，这一页记的那个 SIGSEGV 不再出现。**同一个 `GUARD` 还有第二个触发
形状**，记在这里是因为它比上面那条更难看见：图建得出来、`same=yes`、`skip=` 一条不写，**只在被调用的
那一刻崩**，而它的触发条件是一个设计决定，不是一个漏写的包装。

作者裁定"未读到的参数在循环里也答零余切"（`origin/fsh-dev = ad7e716b0`）。把这个零答给
`split(jvp(h))` 的 backward 半（`h(x) = loop s = 0.0 for i in [1,2,3,4] do s + 2.0`，`h' = 0`）就崩：

```
--- transpose of h (body reads no outside value) ---
REINTERP split function=jvp_of_h forward=248 backward=249 type=(float) => (float, (float) => float pure) pure residuals=1 loop=1 tape=1 invariants=0
h value= 8.0
h lin(1.0)= 0.0
TRANSPOSE function=split_of_jvp_of_h_backward graph=250 type=(float) => (float,) nodes=10 arms=0 rhs=-
Program received signal SIGSEGV, Segmentation fault.
rc=139
```

**`nodevm.cpp:548` 的原文三行**（`NodeVMSchedPass::call`，`case GCNodeKind::Accs` /
`GCAccsKind::TupleIndex` 分支）：

```cpp
547:  Tuple *t   = currFrame->get<Tuple *>(srcIdx);                    // ← 0x0
548:  GUARD(idx < t->size(), "Tuple index out of bounds in NodeVM.");  // ← 崩在这
```

**带行号的栈**（`-Config Debug` 构建，`out/debug`）：

```
#0  Tuple::size (this=0x0) at include/camel/core/rtdata/tuple.h#Tuple
#1  NodeVMSchedPass::call (rootRuntimeGraph=0x7fffe79b3b90, rootFrame=0x7fffe75c6158) at src/passes/sched/nodevm/nodevm.cpp#NodeVMSchedPass::call
#2  NodeVMSchedPass::call (…) at src/passes/sched/nodevm/nodevm.cpp#NodeVMSchedPass::call   ← 间接调用：call(funcRuntimeGraph, funcFrame)
#3  NodeVMSchedPass::call (…) at src/passes/sched/nodevm/nodevm.cpp#NodeVMSchedPass::call   ← 同一个调用点第二次
#4  NodeVMSchedPass::apply (…) at src/passes/sched/nodevm/nodevm.cpp#NodeVMSchedPass::apply
```

`Tuple::size_` 就是 Release 里 `mov 0x8(%rax),%ecx` 那一下（`call+3468`，`rax=0x0`）：**空指针，不是野指针。**

**一句根因**：`Accs`/`TupleIndex` 按位置取成员时，`currFrame` 上那个"参数携带的元组"槽是空的
（`currFrame->get<Tuple *>` 得 `0x0`），因为两层间接调用（`nodevm.cpp:629`，
`fillFrameForIndirectCall` 之后）进去时没有把这个元组填进成员帧，槽还是 `NullSlot` —— **不是递归爆栈**
（`call` 只有三层、`#1`/`#2` 同址、`currRecursionDepth_` 的守卫 `throwNativeStackExhausted` 没响），
是**成员图少填了一个参数槽**。

**触发条件**：只有在我们给"带闭包的那半"回答常量零时才会走到。今天 `transpose.cpp` 那道守卫
（`closurePortOf.empty()`）让它保持在拒绝这一侧 —— 所以 `h` 经 `vjp` 现在是
`TRANSPOSE function=split_of_jvp_of_h_backward skip=state-unread node=2 origin=0`、exit 1、**不崩**；
而同一条零答案对不带闭包的形状（`k(x) = loop s = 0.0 for i in [1,2,3] do s + 2.0`，`k' = 0`）已经生效，
答 `(0.0)`（`pullback(2.0)` 也是 `(0.0)`，线性的零）。

**仍是推测的那一半**（照实写，没验证）：那个元组本应是调用点传入的闭包/参数元组 —— 零答案让这个元组失去了
唯一的读者，调用点于是没有再传它，而图里那个 `Accs` 节点还在按位置取它。要坐实，得看
`fillFrameForIndirectCall` 在两种形状下各填了几个槽；这一页没做。

**查这一格要付两次构建**：`-Config Debug` 构建 **315 s**（没有缓存命中，`out/debug` 是独立目录），拿到行号后
把诊断开关撤回、`out/latest` 的 Release 恢复构建 **304 s**（源码回到已提交内容，但指纹变化仍触发一次
`collect-out`）。符号是唯一拿到 `file:line` 的办法，而 `.so` 是 strip 的 —— 所以下一次有人要查这一格，
预算里要有这两次构建。

**读数树**：`cd46c06de`（原 `lane-splitprimal`，该分支已删），`out/debug` 与 `out/latest` 都是同一棵树。