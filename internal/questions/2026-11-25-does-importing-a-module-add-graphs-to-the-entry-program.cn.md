# 把模块的遍名写进流水线，为什么要把它的 205 张库图挂到用户程序上？

2026-11-25 ｜ 来自 `lane/std-nop` 与合并演练 ｜ 对象：`pass.cpp:994` 的 `importModule`、`module_source.cpp:225` 的 `Builder::of`、`main.cpp:779` 的 `encode`

## ① 问题：不是遍做了错事，是「解析遍名」这件事本身改了程序

合并演练里 48 条红（图比对族 + `*.program_still_runs.fvm` 族）查到同一个根：

```
camel case.cml std::stats std::nop                 → graphs:5
camel case.cml std::stats std::nop opt::cse        → graphs:210   ← cse 从未运行（它在 std::nop 之后）
camel case.cml std::stats std::nop opt::fold       → graphs:210
camel case.cml std::stats std::nop std::opt        → graphs:5     ← C++ 遍，不导入模块
```

把遍名写在**永不执行**的 `std::nop` 之后，程序还是多了 205 张图。所以不是遍运行的副作用，是**名字解析**的副作用：

```
main.cpp:341-345  先解析流水线（passListEndsWithoutExecuting）
pass.cpp:994      解析到模块遍名 ⇒ ctx->importModule("opt")
module_source.cpp:225  library 建在 **context 唯一的那张图构建器**上（program.cpp:224-233 每 context 一个 session）
main.cpp:779      入口程序**到这时才** encode
⇒ 入口程序的图树里已经有这些库图；encode 后 __root__ 的引用表把它们记成 nested 引用
⇒ 进来的是 180 个 types:: + 6 个 rules:: + 19 个 opt:: = 205（遍入口 opt::cse / opt::fold 自己没有图）
```

`__root__` 的引用表读数：`refs=209`，其中 **nested 209 / callee 1 / static 1**；jgir 全表里**只有 `__root__` 引用这些库图**，而 `__root__` 只有两个节点（`main` 的函数值 + 一个 CALL）。⇒ **从根到那 205 张只有一跳**，走的是 `__root__` 自己的嵌套引用表，不是任何被改写图上的函数值。

**它造成的可观测后果有两族，来自同一个数：**
- 图比对族：`cse.by_camel` 的 dump 是 488 节点，参考值是 43 ⇒ 数的是整张表。
- 跑起来族：`std::fvm` 报 `requires a value return node in graph 'opt::evaluated'` —— `opt::evaluated` 是改写谓词，本来就没有值返回节点，它不是入口。

**已排除的两条路**（都不成立，写下来免得下一个人重查）：
- 「遍把另一个程序的整表并进来」（`adoptProgram`）：新增的表检查断言 `introducedGraphsViolation` 在全部 17 条上**真而沉默**，given 表 210、produced 表 210、新 id 0。
- 「从函数值跨过去」：`traceGraphs` 只在**同一个 Program 的表**里解析 id，唯一跳过的是 nested 角色指向的 `#impl`；从根数图不跨 program。

## ② 三条路，各带代价

- **(甲) 库图不挂到入口程序上**：模块为「能被调用」而必须在同一个 context 里，但不该在**入口程序**的表里。做法是给库代码一个自己的 Program（或让 `__root__` 不把库图记成 nested 引用）。
  代价：所有「从根数图」的读者（`stats`、`gir`/`jgir`、`fvm`/`jit` 的编译入口）都要改成「读用户程序的那一半」；而**什么算用户程序的那一半**需要一条判据，不是一句愿望。
- **(乙) 保留挂载，让读者自己跳过库区域**：给库图打一个标记，读者跳过它们。
  代价：这是「图在这儿但被跳过」——今晚已经立过一条规矩不许这么干（会把「图不该在这儿」变成「图在这儿但被跳过」，两种缺陷合成一种）。
- **(丙) 承认库图是程序的一部分**：把 488/43 的参考值改成「含库图的程序」。
  代价：`stats` 与 `gir` 从此对用户不说真话（他改了一个 5 张图的程序，读到 210 张）；fvm 那条还是红的，因为 `opt::evaluated` 依旧没有值返回节点。

## ③ 我的默认：(甲)，但先只做一半

**默认判据：一个图集是「用户程序」的，当且仅当它是 encode 时从入口导出的那两个引用角色之一（callee / static），而不是 `__root__` 为库代码登记的 nested 引用。**

先做的一半：把这条判据写成**只读检查**（不是修）——`stats`/`gir` 对「nested 引用的库图」单独计数并打印一行（`graphs: 5 (+205 library)`）。
理由：它一次就把「谁是用户的、谁是库的」变成读数，而**不改变任何现有行为**。等这个数在几十条用例上稳定之后，再决定 (甲) 的两个读者怎么改。

**不做的事：**不把 `opt::evaluated` 从 fvm 的编译集合里跳过；不改 488/43 这个参考值；不动 `importModule` 的时机（它在 `requireKnownPasses` 之前是对的）。

## ④ 我已经记下的、与这一问相邻的一条缺陷

`introducedGraphsViolation` 读的是裸指针 `given`，而 `pass.cpp:2758-2762` 自己写着「the program the pass is given is not read once it has returned」（`settle(keep=produced)` 会把 given 从根集摘掉）。这次它是**侥幸**没被回收（探针前后同地址同尺寸），但没有任何东西 pin 它。⇒ 要让这条断言成为可信读数，应在 pass 前后快照 given 的 id 集合，或用 `ProgramHold` 住它。**这一条不依赖上面三选一，可以独立修。**
## 决策者答（2026-11-25）

**(甲)**，而且要做完，不停在只读那一半。依据：设计原则 2.3，程序没有另外的表示，程序就是它的根函数，属于程序的，是从入口经调用和静态引用能到达的图；9.2，写模块的遍名只为加载这个遍，不能改变程序。所以"解析遍名"导入的库图属于上下文，不属于入口程序。判据就用页上那条：encode 时从入口导出的 callee/static 引用，`__root__` 为库代码登记的 nested 引用不算。先做的只读计数（`graphs: 5 (+205 library)`）可以作为第一笔，用来确认判据；接着让 `stats`、`gir`/`jgir`、`fvm`/`jit` 的入口都按这个可达集合读，48 条红应当随之消失，参考值不改。(乙) 和 (丙) 都不采用，理由正如页上所说。
