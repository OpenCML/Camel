# 开放秩数组的循环轮：轮的类型是什么，运行期该交出什么值（实测，2026-10-25，h571 lane）

`for v in x` 里 `x` 的声明类型是**开放秩**数组（`f32[..]`，也就是文档建议的张量参数写法
`func f::<T>(x: T[..])`）时，**轮（round）的声明类型**与**运行期真正交出的值**是两件事，今天不一致；
按"数组的形式"读它（任何张量算子，如 `sum(v)`）会读到一个野指针并 SIGSEGV（本次现象的实际爆点）。

本页只记事实与取舍，**不定语义**；本 lane 在实现里只做了"让交出的值具有它声明的类型"这一步（见末节）。

## 一、已定的事实（逐条有出处，读数取自 `tip=57e35d693`）

1. **沿第一维、核心负责**：设计原则 5.4（`t[i]` 取第一维第 `i` 个，多维是一行）与
   `docs/technical/39_loops.md` §2a（对任何数组的 `for` 都是核心的规则，模块的 `iter` 随 P4 删除）。
2. **类型侧**：`ArrayType::firstDimensionElement()`（`src/core/type/composite/array.cpp:270`）——
   秩 1 → 元素类型 `T`；秩 n → `T[d1, …]`；**秩开放 → 同一个开放秩数组**（"少一维"对未知秩仍是未知秩，
   即 `f32[..]` 的不动点）。`docs/technical/39_loops.md:115` 把这个读数写成了规则：
   "a `Tensor` whose rank is unknown gives elements of the same open-rank array, one dimension fewer"。
3. **运行期侧**：`Array::firstDimensionElement(i)`（`src/core/rtdata/array.cpp:119`）——运行时
   `rank_ <= 1` → **元素自身**（`getSlot(i)`，`f32` 的位型就是 `0x3F800000`）；`rank_ >= 2` → 一行
   （compact 是共享数据块的视图，slots 是拷贝）。
4. **秩零的约定**：没有维度的值就是它持有的那一个元素（`Tensor<f32, []>` 是 `f32`）；但一个槽的
   **声明类型说数组**时，它的读者按数组取——`modules/tensor/ops/support.cpp` 的 `wrap`（"declared 是数组
   或无固定秩时，秩 0 的结果保持是数组"）与 `tensorArg`（`args.get<Array*>(index)`）都是这么读的。
5. **实测**：`func f(x: Tensor<f32>)`（= `f32[..]`）配 `let x = new([1.0, 2.0, 3.0] as f32[])`，
   `loop s = 0.0 for v in x do s + sum(v)` → `input.signal === 'SIGSEGV'`（139），**修后** `exit 0`，
   `stdout` 逐字 `6\n`。
6. **开放秩的轮今天只有一种能通过编译的读法**：同一个程序把体写成标量读法
   `loop s = 0.0 for v in x do s + v`，编译期被拒（逐字）：
   `Invalid call to '__add__' with argument types <> (float, f32[..]): the scalar operand has type
   'float', but the tensor's element type is 'f32' ... (write the conversion, f32(value))`
   （`SemanticDiag::ArgumentsRejected`）。也就是说 `v` 声明成开放秩数组时，"它是标量"的读法根本写不出来：
   类型侧说数组，而能通过的读法（`sum(v)`、下标等）都按数组取 → 运行时必须交出数组。
   所以本现象不是"两种读法都能用、只崩一种"，而是**声明与运行期只有一边能对**。

## 二、两种静态读法（第三种是它俩之外唯一能让两边都成立的办法，见末节）

| 读法 | 轮的类型 | 运行期秩 1 | 运行期秩 ≥2 | 代价 |
|---|---|---|---|---|
| A（今天）| 同一个开放秩数组 `f32[..]` | 交出元素自身 → 声明是数组、值是标量 | 交出一行（数组），自洽 | 读者按指针取 → 野指针（本次 SIGSEGV） |
| B | 元素类型 `f32` | 自洽（标量） | 交出一行数组，却声明成 `f32` → 把指针当浮点读，**静默错值**（比崩更坏） | 秩 ≥2 的 `Tensor<f32>` 参数普遍用法全错 |
| C（本 lane 的做法）| 保持 A 的类型规则不动 | 交出"少一维的子张量"= 0 维数组（同一元素，compact 是共享数据块的视图）| 自洽（一行）| 每轮多一个小对象（compact 是视图，零拷贝）；不改任何类型侧读数 |

读法 B 是"元素类型是类型的一部分"这句的自然推论，但**只有运行时秩 1 时成立**：A 与 B 的差别不在
"元素是什么"，而在"声明一个未知秩的数组的轮时，该按最保守的那一层（数组）还是按最小秩那一层（元素）"。
类型不能同时说"秩 1 时是元素、秩 ≥2 时是一行"，所以这里要作者定的是**声明**，不是元素类型。

## 三、本 lane 做了什么（终局 vs 权宜）

- 做的是 **C**：新增 `camel::execute::loopRoundValue`（`include/camel/execute/loop_driver.h`），
  由 `loop_frame.h` 的 `loopTest`（nvm/fvm/jit/prl/macro 共用的那一个）与 `driveLoopRounds` 一起用。
  规则的表述是"轮的值必须具有它的声明类型"：声明类型是数组而第一维给出的是元素自身时，交出
  **0 维数组**（compact 走 `Array::view`，与"行"同一条共享数据块的规则；slots 拷贝那一个元素）。
  类型侧的规则（`firstDimensionElement` 的开放秩分支）**一个字没改**。
- 对**崩溃**是**终局**：不管作者怎么定轮的声明类型，"槽里是元素位型而声明类型说指针"都不再成立。
- 对**本页的问题**是**权宜**：它把 A 作为给定。若作者裁定 B（或第三种拼写，例如"秩 1 是元素、
  以上是一行"的写法），改动点是两处——`ArrayType::firstDimensionElement()` 的开放秩分支，
  以及删掉 `loopRoundValue` 里的那一步包装。

## 四、要裁定的问题

1. `f32[..]`（开放秩）的轮的**声明类型**：保持同一个开放秩数组（A/C），还是元素类型（B），
   还是引入一个能说"秩 1 时是元素、以上是一行"的拼写？若维持 A/C，请确认"`for v in x` 里 `v` 是
   **0 维张量**（不是标量）"是语言要的读数：今天 `f32[3]` 的 `for v in x` 给 `v: f32`（标量），
   而 `f32[..]` 给 `v: f32[..]`（数组）——同一个运行期值、同一条运行期规则，声明不同。
2. 若维持 A/C，`f32[]`（秩 1、长度未知）与 `f32[..]`（秩未知）在轮上的差别是否有意？前者给 `f32`，
   后者给数组。今天这一点没有任何用例钉住（本次最小复现是本 lane 临时文件，未入树）。