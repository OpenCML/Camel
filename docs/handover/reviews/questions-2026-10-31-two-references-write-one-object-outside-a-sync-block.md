# 一问：同一可变对象被两条引用写，读拿到谁的版本？

> 状态：等作者裁决。
> 相关：`docs/design-principles.cn.md` 3.1（写要有人读才执行）、3.5（捕获是引用；函数值的类型带着它写的资源）；`include/camel/runtime/effect_liveness.h`；20:35 裁定的边界（"登记表也不知道的写……不为它造版本边"）。

## 现象

`var counter` 被两条引用写：一条是变量本身，一条是捕获它的函数值，而这个函数值在调用点是一个参数——这一层不知道它是谁。

```camel
var counter = 0

func twice(f: (x: int) => void, v: int): int sync {
    counter = 1     // 第一处引用：登记在册
    f(v)            // 第二处引用：f 捕获了 counter，这里看不见
    return counter  // 拿到的是哪一次写？
}

func main(): int sync {
    println(twice((y: int) => { counter = y }, 3))
    return 0
}
```

按代码读，打印 `1`，不是 `3`：

- `counter = 1` 经 `Builder::recordWrite`（`src/compile/gir/builder.h:2254`）登记；读 `counter` 由 `valueVersionOf` 取最近一次登记过的写（`builder_expr.cpp:742` 的 `orderAfterLastWriter`），拿到的是 `1`。
- `f(v)` 是穿过函数值的调用。调用点只看见一个参数，既不是已知函数（`targetGraph == nullptr`）也不是已知算子（`targetOperator == nullptr`），`builder_operators.cpp:4310-4326` 只把写在它**之前**的读排到它前面，**不记写**；捕获写只在 `:4328-4331`（`captured` 来自调用点认识的函数值）才记。
- `effect_liveness.h:67` 写明了这条边界："A write the writer table does not know is left to the sync blocks and the resource order (3.3): it is not a credential of this layer."
- 这次调用在同步块里，所以它**会执行**（`:4310` 那段把它接上链）；执行了、也确实改了 `counter`，但读的版本边已经连在 `counter = 1` 上。

**这不是说实现写错了**：20:35 的裁定就写了"这种写与这一层后面读的先后，只由同步块和资源顺序保证，不为它造版本边"，并请"找到一个在这条规则下违背直觉的程序，写成一页带最小程序的问题"。上面就是那个程序：两次写都发生、同步块也保证了先后，读却稳定地拿到前一次的值。

（本机不构建，读数来自代码路径，不是跑出来的。`callback_write_ignored.cml` 是同形状、但闭包写在调用点的版本——那个能打印 3，因为 `:4330` 记下了写。）

## 与 `a-version-port-that-must-carry-a-value` 不是同一问

那页问的是**一次已经登记过的调用**怎么把版本交回来（`PendingIndirectCallVersion`，调用节点就是版本）。这页问的是**调用点根本说不出它写哪个变量**时，两条引用写同一对象，读按什么选。前者在 `callValues_` 里有掩码，后者没有；前者是"版本怎么回来"，后者是"两次写谁说了算"。两页相邻，不要合成一份；若作者认为该合成，也应该先把这页的"看不见"补成那页的一节边界。

## 为什么不自己定

它决定 3.1 的"写凭什么活着"在别名上闭不闭合：一条登记过的写、一条看不见的写，读该拿谁。这是语义，不是实现；今天"稳定拿到旧值"不是设计，是两条规则的交点没人写过。

## 选项

- **① 保持现状，把边界写进文档，并加一条诊断**：同步块里同一变量既被直接写、又被"可能写它"的未知函数值调用过、随后被读，报一条诊断（警告或错误）说"你读到的可能不是最后一次写"。
  代价：3.5 或 3.3 加一句；诊断要判"未知函数值可能写这个变量"，今天只有 `pendingIgnoredWrites`（`builder_operators.cpp:4301`）里那句 "it writes" 可借。**验收**：上面的程序要么报诊断、要么在注释里写明为什么允许；`callback_write_ignored` 仍打 3。
- **② 按函数值的类型登记写**：3.5 说"带 `var` 上下文参数的函数可以部分应用……它的类型带着这个资源"。类型既然说得清它写哪个资源，调用点就能把这次调用记成那个变量的写者，读拿到调用作版本。
  代价：要把"函数值类型里的资源"映射回这个作用域的单元；今天的 `EffectSummary` 按签名算，不按值类型。**验收**：上面的程序打印 3；`mutable_reference.*` 四条后端仍绿。
- **③ 禁止**：可能写 `var` 的函数值经参数或数据结构传递后，调用点必须能说出它写什么（显式 `uses`），否则在调用处编译期拒绝。
  代价：一条新规则加诊断；`apply(f, v)` 这类写法被挡在门外，库里的高阶函数要跟着改签名。**验收**：上面的程序编译期报错，而不是打印 1。

**默认建议：②**。"类型带着这个资源"是 3.5 已经写下的东西，②只是让调用点去读它；①把错误结果变成"有诊断的旧值"，可接受但欠一笔；③为了一个少见形状把高阶函数的主路堵上。

**要作者答的一句**：同一可变对象被两条引用写、其中一条在这一层看不见时，读拿谁的值——保持"看不见的写不产生版本"（①），按函数值类型里的资源登记（②），还是拒绝这种写法（③）？

在这一页裁定之前，`twice` 这样的程序打印 `1`；不写出来，下一个人会以为是自己的闭包写错了。

## 裁定与落地（保守的写者集合）

作者裁定：**穿过函数值的调用，是"调用点看得见的变量中，程序里某个函数（lambda、嵌套函数、模块函数）写过的那些"的可能写者**；这个集合由**建图之前整程序扫描**得到。`twice` 的 `counter` 在集合里，所以 `return counter` 取 `:var/after(counter, f(v))`，读 `3`。代价是有些调用变成必须执行、它之后的读被排到它后面——保守但正确；等函数值的类型带上它写的资源，再把集合收窄。

落地：

- 扫描在建图之前走遍每个模块的构造树，按赋值（`__assn*`）与路径赋值（`ACCS`/`SACC`/`__idx__`）记下"变量名 → 写它的函数集合"（`Builder::scanProgramWriters`，`src/compile/gir/program.cpp`）。按拼写记，故意多算：局部同名也是一种可能。
- 调用点（`builder_operators.cpp:4310` 那半边，未知被调者）把每个可见且被程序写过的变量记成这次调用写的（`recordWrite(cell, call, throughCallee)`），图里还没有的变量按图登记、由后到的读领取（`claimedWriters_` / `readVariableThrough`）。
- 读取版本时，写者是这种调用就取 `:var/after(cell, call)`（`valueVersionOf` → `variableAfterCall`）。

### 这一问的验收：为什么是 `_unsynced` 的 `1 → 3`，不是 `twice` 的 `3`

**`twice` 带 `sync`，今天本来就打印 `3`**（四条后端实测，`std::opt` 也是 `3`）：同步块让调用执行，读拿到的是活着的单元。一个在改动前就成立的读数不能当验收——本轮的第三个实例（前两个：`twice` 页上写的"打印 1"、`callback_write_ignored` 的读数）。所以：

- `test/cases/effects/two_references_write_one_object.cml`（带 `sync`，期望 `3`）：**回归**，注释里写明它不判别写者集合；
- `test/cases/effects/two_references_write_one_object_unsynced.cml`（同一个身体、`f(v)` 在同步块外）：**验收**，改动前 `1`、改动后 `3`；
- `test/cases/effects/overcounted_writer_keeps_the_value.cml`：**多算的证明**，`called 3` + `1`。

### 记一个写者，不是记"谁最后写"

**记一个写者，不是记"谁最后写"，是记"这一版从哪来"。把前一次写挤掉，等于让那次写没人读、按 3.1 被丢——结果比不算更错。**

这不是推演，是先量到的：只把调用记成变量的最后一个写者时，`counter = 1` 没人读、被丢，读拿到 `0`（`called 3` + `0`）。修法是 `recordWrite` 记下"写之前的版本"（`writeBeforeCall_`），`:var/after` 的**第一个实参取那个版本**，不取裸单元。

### 资源账上的两条

- **声明一个算子不碰的东西，会在别人的账上多出一条无主的访问；算子的资源声明要和它的语义一样小。** `:var/after` 曾写成 `read variable (operand 0)` + `result alias(0)`，`main` 的 footprint 因此多出 `rmw variable (any)`。
- **`alias` 是"可能同址"，`same` 是"就是这一格"。资源账要的是后者；用 `alias` 会把一条真账写成一个无主的 `(any)`。** 改成 `result same 0` 之后，`main` 的 footprint 回到原文：`RESOURCES footprint main rmw ambient rng; write ambient stdout; read ambient fs; rmw object (any); external`。
- 另外 **check 5 的原文**："不是纯算子就必须碰、分配或影响某物"——它说明"什么都不声明"这条路不存在，每个算子至少要有一条访问或一条别名关系（只写别名会被拒：`The semantics declared for the operator ':var/after' are rejected`）。
- 判据：**保守的代价会出现在别人的账上（这里是 footprint），所以做保守化的那一笔必须自己跑一遍那些记 footprint 的用例。**

### 冲突解决里"漏掉一侧"

merge `origin/develop` 时，`:var/after` 已在 develop（另一车道落地）。冲突解决里 `valueVersionOf` 的兜底被整块换成 develop 的注释、**代码那三行没留下**，于是读又回到裸单元，`return counter` 交给 `println` 的成了单元而不是值（`called 3`，没有 `1`）。这只有在整闸里以一条红的形式出现。判据：**冲突解决里"漏掉一侧"是最难发现的错，它只会在整闸里以一条红的形式出现；解决冲突后必须重跑那条用例，不能只看编译通过。**

### 已知边界（留给收窄那一笔）

- 只靠"把变量当 `var` 实参传出去"才被写的变量不在扫描里（调用点没有类型，判不出）。
- 调用之后才第一次捕获的变量，按程序的书写顺序当"之后"（`claimedWriters_`）。
- 两次相邻的未知调用，前一次本身也是未知调用修饰时，不走 `writeBeforeCall_`（Call 修饰被跳过）。
