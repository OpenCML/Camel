# `std::mem::blocks()` 在 standalone 程序里没有块空间：能不能报，报什么

**问题**：`std::cpp` 写出的程序没有**块空间**（它的 `T[]` 是 `new` 出来的一块 `{refs, size, capacity, elements}`，不是运行时 `mm` 的 block space），所以 `std::mem::blocks()` 的四个数在那边**没有意义**。要问作者的是：这条算子要么**拒绝**（说"standalone 程序没有块空间"），要么**让 standalone 运行时接上块空间**，要么**在 standalone 里改成数它自己的分配**——三选一，我不该替作者选。

**我量到的**（今天，`8d39b9bcb` 之上）：

`test/cases/feat/mm/mem_blocks.cml`，解释器（`std::fvm`）：
```
true
true
1 1 0 true
100 7
```
我把它译成 C++（四个数从 `camel::core::mm` 的 `blockCreations/blockFrees/blockLiveCount/blockBytesLive` 读，再聚合初始化成结构体），**编得过、跑得动**，输出：
```
true
true
0 0 0 false
100 7
```
前两行相同（`allocs - frees == live` 在两边都成立），**第三行不同**：standalone 程序里 `make(100)` 从 `new` 拿空间，一块也没进运行时的块空间，所以计数是 0，`b.bytes > a.bytes` 也成了 `false`。

**为什么这不是"再努一把"能补的**：要让那四个数在 standalone 里动起来，就得让 `rt::Array` 的块从 `camel::core::mm` 的块空间分配——那是**每个** standalone 程序的分配路径（性能与语义都变），不是一条算子规则的改动；而"数 standalone 自己的分配"是给这条算子换一个含义，同样是设计决定。

**所以我退了这一步**：把那条规则与 `rt::bridge::memBlocks()` 一起撤掉，让 `:op/mem_blocks` 仍然被 `TranslationRefused` 拒绝（原文：`function 'main' uses the operator ':op/mem_blocks', whose result of type '{ allocs: int, frees: int, live: int, bytes: int }' has no translation in std::cpp yet.`）。**宁可拒绝，也不给出一个和解释器不同的数**——一个"能跑但算错"的翻译比一条拒绝贵得多。

**问作者**：
- **A. 拒绝**：把这条拒绝的措辞改成"standalone 程序没有块空间，`std::mem::blocks()` 的计数在那边不可报"，让拒绝说实话；
- **B. 接上块空间**：`rt::Array` 的块走 `camel::core::mm`，四个数就真了，代价是每个 standalone 程序的分配路径都变；
- **C. 换含义**：standalone 里数它自己的堆分配（`allocs`/`free` 一对自记计数），代价是同一条算子在两个后端含义不同——我倾向 A，因为含义不同比数字为零更难查。

**堵着谁**：`std::cpp` 普查里 `:op/mem_blocks` 这一格（10 个程序）今天仍拒绝；`memory`/`gc` 族里凡是要读块计数的程序，在 standalone 后端都停在这一步。
**等作者裁。** `lane-mcts` 的倾向已写在上面：**A**（继续拒绝，并把拒绝的措辞改成"standalone 程序没有块空间"）——今天按 A 处理，规则未落地，`:op/mem_blocks` 仍按原样拒绝。