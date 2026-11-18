# 同一个优化的两套实现：C++ 遍与 Camel 遍在同一张图上的两次读数

> **判定"能不能挑大梁"要靠一次运行，不是靠读代码。两次读数逐字节相同，才叫"给出同一结果"；不同，就要指名道姓地说是哪一个字段、由哪一次提交写的。**

日期：2026-11-18。范围：`docs/roadmap.cn.md` 第 0 节 P（作者 2026-11-18 裁定：C++ 版优化遍不删，用 `cxx::` 命名空间与 Camel 原生版隔开，默认流水线 `std::opt` 暂留 C++ 版，做对比实验）。本页是那个实验的报告。实验是可运行的判据：`test/plans/feat/opt/cxx_isolation.plan.toml`，语料 `test/cases/opt/cxx_isolation/subject.cml`。

## 1. 隔开的到底是什么

`std::` 下 14 个优化遍的**注册名**从 `std::opt*` 移到 `std::cxx::*`（`src/execute/pass.cpp`，`registerCoreKernels`）：

```
std::cxx::opt  ::inline  ::small  ::arm  ::hybrid  ::devirtualize  ::specialize
std::cxx::fold ::algebra ::saturate ::extract ::cse ::dce ::order
```

对应的 14 个声明在 `stdlib/std.cml` 里从 `namespace opt` 移进 `namespace cxx`（声明本身仍是一行 `export func fold::<F>(f: F): F!`，参数表不变）；Camel 原生版是模块 `opt`（`stdlib/opt.cml`），名字 `opt::fold`、`opt::cse`、`opt::dce`、`opt::saturate`，**一个字符没动**。

旧名字全部保留为别名（`passAliases`）：`std::opt` → `std::cxx::opt`，`std::opt::fold` → `std::cxx::fold`，依此类推 14 条，加上原有 6 条内联策略拼写一并指向新名。`std::opt` 仍是默认流水线的名字，程序不点遍、`fixedLoweringSteps` 插的那一步、`docs/cli.md` 每一页写的 `std::opt`，都照旧解析到同一个 kernel。

**这一层隔开对用户可见的行为是零**，除了两处：`std::cxx` 不再等于 `std::cpp`（那条未文档化的同义拼写删了，否则读者按新命名空间写的 `std::cxx` 会拿到 C++ 导出器而不是优化器），以及 `std::jgir`/标签读者表里多了一个 `std::cxx` 读者。

## 2. 移名字时被撞出来的两处"按写下的名字比较"

两处代码拿**写下的**名字跟 `"std::opt"` 比，而解析后的名字现在是 `std::cxx::opt`。两处都改成比较 `resolveAlias()`：

1. `valueProblem` 里 `settle_runs < 1` 的拒绝（`src/execute/pass.cpp`）。不改则 `std::cxx::opt settle_runs=0` 不报错，而 `std::opt settle_runs=0` 报错——同一台 kernel 两个答案。
2. `expandFixedLoweringSteps`（`src/execute/pass.cpp`）：它把 `std::defunctionalize report=false` 插到 `std::opt` 之前。不改则**只有** `std::cxx::opt` 这个拼写不插那一步——C++ 优化遍是对着"没有函数值调用、没有闭包"写的，跳过它不报错、只是少做了降低，这是本次改名里唯一会改变图的地方。

## 3. 实验怎么做的

一张语料，一个程序，五个图（`__root__`、`main`、`chain`、`shared`、`work`）：

- `chain`：`2 * 3 + 4` 串成 let 链，折叠要两轮才到底（一轮就停的版本会把中间结果留下）；
- `shared`：`x * 7` 写两遍（非 `sync`），CSE 该并成一个；
- `work`：`n * 3` 在 `sync` 块里写两遍，语句链的门挡在中间——"看不看得穿门"两套实现的答案都要一致；
- `main`：调用三者。

两遍各跑一次，读数用 `std::jgir canonical=true`（同一份 `src/passes/trans/json/gir_canonical.cpp`：丢图名、节点名、id、源位置，边只留传递约简后的部分，键与节点序规范），**`normalizers = []`**，所以比较的 `stdout_normalized` 就是进程原样打印的字节。另外加一条控制项：`std::opt::fold`（旧名）对 `std::cxx::fold`（新名）——改名若动了图，这条会红。

## 4. 结果一：折叠，两次读数逐字节相同

`cxx_isolation.fold.by_cpp` / `fold.by_camel` / `fold.by_old_name` 三条绿。C++ 遍和 Camel 遍在每一个节点、每一个常量、每一条边上一致；旧名和新名打印同一个文档。

红过的假象要说清楚：第一版语料是 `sync` 函数，`std::opt::fold` 之后 `reduceReachableGraphs` 走"没有改动就返回原程序"这条路，`std::jgir` 打出空 stdout，两边都是空串——**空对空会比成相等**。所以 `fold.by_cpp` 现在自断言"没有操作数全是常量的 OPER 还留着"（折叠到了不动点），空文档和没折的文档都会红。

## 5. 结果二：CSE，两次读数只差一个字段，且能指名

`std::cxx::cse` 与 `opt::cse` 的读数，逐图逐节点记下来是：

```
g0 f2 [n0:DATA n1:CALL]
g1 f2 [n0:DATA n1:DATA n2:DATA n3:FUNC n4:FUNC n5:OPER::io/println@1.0 n6:OPER::io/println@1.0 n7:FUNC n8:OPER::io/println@1.0 n9:GATE]
g2 f0 [n0..n7: DATA×4, :op/add_l, :op/mul_l, :io/println@1.0, GATE]        <-- Camel 边上是 f2
g3 f2 [n0:PORT n1:DATA n2:OPER::op/mul_l n3:OPER::op/add_l]                <-- 两个 x*7 已并成一个
g4 f2 [n0:DATA n1:PORT n2:DATA n3:OPER::op/mul_l n4:OPER::op/add_l n5:OPER::io/println@1.0 n6:GATE]
```

两边的 `decls`、`tags`、`types`、`entry` 相同，五张图的节点数、id、种类、算子、操作数、顺序全部相同；`g3`（`shared`）两遍都把两个 `x * 7` 并成一个，`g4`（`work`）两遍都并了——**CSE 该做的事，两套实现做的一样**。

唯一的差别是 `g2`（`chain`，CSE 无事可做的图）的 `flags`：C++ 边 `0`，Camel 边 `2`。`2` 是 `kGraphFlagGatesDissolved`（`include/camel/runtime/graph.h:342`）：

- C++ 的 `rewriteReachableGraphs` 只写筛出来的图；`chain` 没有可并的东西，没人提交它，`flags` 保持 `0`。
- Camel 的 `opt::cse` 是**整个程序一个事务**：`open` 之后 `commit` 一次，而 `commit` 会对它打开过的每一张图跑 `dissolveUnorderedGates`（`kGraphFlagGatesDissolved` 的注释原文："A graph without it is looked at by the first commit that reaches it"），于是 `chain` 也被看了一眼，`flags` 变 `2`。

所以这是**备忘位**（谁看过它的门），不是图的差别。两边的程序等价。

## 6. 因此这条对照今天不是文本对照

`cse.by_cpp` / `cse.by_camel` 是两条 behavior 项，各读一次并各断言：是规范文档、是程序自己的五张图、扫到了不动点（同一张图里没有两个 `:op/…` 读同一组操作数）、以及打印出来的 `flags` 字（`0,2,2,2,2` 对 `2,2,2,2,2`）。任何一侧改变都红，而差异写在断言的消息里。

要让这两条变成逐字节对照，只有三条路，都是要作者拍板的事：

1. **规范文档不记备忘位**：`gir_canonical.cpp` 打的 `flags` 里去掉 `kGraphFlagGatesDissolved`（它描述"某次提交看过门"，不是图）。代价：`std::jgir canonical=true` 的输出变，所有 golden 要重录；而且这等于宣布规范文档不含对象状态，是个格式决定。
2. **Camel 遍按图提交**：`merge_hits` 只在真有命中的图上开事务、提交。代价：`opt.cse` 从"一个程序一次事务"变成"N 张图 N 次事务"。
3. **两遍都在末尾提交一次**：对照实验里给两边加同一个"提交所有图"的尾步。今天没有这样一个便宜的遍（`opt::dce` 没有可删的就不提交，`opt::cse` 第二轮没有命中也不提交），要为此加一个遍，代价是实验多一个变量。

默认（本车道采用）：不动规范文档、不动 Camel 遍，把差异记在这里，对照继续可运行。

## 7. 怎么核

```powershell
pwsh -NoProfile -Command "& 'D:\Projects\Camel-wt\cxxns\internal\tools\camel-remote.ps1' -Path D:\Projects\Camel-wt\cxxns -Plan test/plans/feat/opt/cxx_isolation.plan.toml -Jobs 8"
```

五条：`fold.by_cpp`、`fold.by_camel`（与前者逐字节）、`fold.by_old_name`（旧名与前者逐字节）、`cse.by_cpp`、`cse.by_camel`（各读一次，见第 5 节）。

改名本身的可核之处：`camel prog.cml std::opt::fold std::jgir` 与 `camel prog.cml std::cxx::fold std::jgir` 打印同一个文档（`fold.by_old_name` 就是这个断言）；`stdlib/std.cml` 的 AST 哈希已重录（`test/plans/feat/parse/golden/ast_hashes.txt` 三行）。