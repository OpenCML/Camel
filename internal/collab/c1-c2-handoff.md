# C1→C2 交接：这条线现在从哪开始

读者：接 C1→C2 的那一格。**本页只写事实** —— 上一格（`lane-kinds-oper`）在收格时上下文已到零，不做实现、未开枝、未改 `src/`，只把下面这些核对过的事实留在这里，省掉下一格重新摸索。

## 1. 枝与树的现状（当场回读的原文）

树 `D:\Projects\Camel-wt\kinds-oper`，读于交回时：

```
$ git status --porcelain
(空)
$ git log -1 --oneline
17d3cd1c0e rtdata: drop the Int32 comparison case; the constant and the arithmetic now agree
$ git rev-parse --abbrev-ref HEAD
lane/i32-encoding
```

枝的现况：

```
$ git branch --list 'lane/pass-decl*'
+ lane/pass-decl-3
```

`+` 表示 `lane/pass-decl-3` **已被另一棵工作树检出**。⇒

- **`lane/pass-decl-2` 已经不存在**。它是上一格昨晚从 `lane/pass-decl @ cc8af549a8` 的尖上新开、并换底到那时的 `origin/integration`（`3d0b445187`）得到的枝；现在枝表里没有它，被 `lane/pass-decl-3` 取代。
- 上一格手上**没有一条可用于 C1→C2 的枝**。负责人裁定：闸线 20/21，不给例外，不动别人树上检出的枝（`lane/pass-decl-3`），**新格等合并位把队列里五条合掉、闸门降下来之后再开新枝**。
- `kinds-oper` 这棵树现在挂在 `lane/i32-encoding` 上、干净；它不属于 C1→C2，新格开工前请另取树或等它让出。

## 2. 卡面出处（逐条判据的原文照抄）

`internal/plans/2026-11-27-cc-before-deadline.cn.md` 第 11、12 行：

> **C1 遍即函数 · 名字与登记** ｜ 计划 `2026-11-26-pass-as-function.cn.md` 第 1 步收尾（核心里的 `passAliases`，`src/kernel/execute/pass.cpp:97`，以及按"是不是遍"开的特例 `isPassDeclaration`）与第 2 步（遍只在算子表里登记一次） ｜ **第 1、2 步原文的判据**：`git grep -n "registerPassAlias\|passAliases" -- src` 为 0；遍注册表的读者为 0；`extract` 的 C++ 入口只剩一个；`stdlib-declarations --check`、`stdlib-reference --verify-ops` 全过

> **C2 遍即函数 · 在程序里调用** ｜ 第 3 步：程序里写 `f->std::cxx::cse` 编成该算子的节点，运行时内核拿到函数值、交回新的函数值；第 5 步那条诊断（程序里调用调度遍）一并做。C1 第 2 步的登记接口先定下来，C2 照它写，两格共用一个接口、各合各的 ｜ **第 3 步原文**：同一个 C++ 遍，程序里调用与命令行上跑，规范 jgir 逐字节相同（nvm、fvm、jit）；作用于闭包、库函数也成立；原函数调用后不变。第 5 步：一条负例钉住诊断

`internal/backlog.md` 里**没有**以 `C1→C2` 命名的行（负责人确认过，不另造一条假卡）；请以上面两行为卡面。

## 3. 上一格在 `lane/pass-decl-2` 上做过什么（命令与读数原文），以及**为什么那不算 C1/C2 判据**

做过的是"接已有枝"的准备工作，不是判据本身：

```
$ cd D:\Projects\Camel-wt\kinds-oper
$ git status --porcelain                 (空)
$ git log -1 --format='%h %s'            59f7d17f65 Merge the coverage table without the case the revert takes back
$ git switch -c lane/pass-decl-2 lane/pass-decl     → Switched to a new branch
$ git rebase origin/integration                     → Successfully rebased（唯一冲突 internal/collab/pending-cards.md，
                                                       两份卡都留，新枝那张从 ### O. 改为 ### R.，+6 行）
$ git log -1 --format='%h %s'            bde63eb825 pending-cards: the parameter a run's target refuses names …
```

空载验证（同一次构建 + 读数）：

```
BUILD_EXIT=0
[provenance] tip bde63eb825121a70e07e9aa2d85e249520f9f32c | binary built 2026-11-28T07:06:30+08:00 |
             tree /home/zjwei/camel-work/kinds-oper | tier fast | jobs 3
[SUMMARY] total: 9, pass: 9, fail: 0          ← test/plans/feat/opt/pass_function.plan.toml
[TREE] clean: 0 before, 0 after
```

**为什么这不算 C1/C2 判据**：9/9 跑的是 `feat/opt/pass_function` 那条既有计划，证明的是"这条枝在新基上编得过、该计划没被换底弄红"。C1 的五条（`registerPassAlias\|passAliases` 为 0、遍注册表读者为 0、`extract` 只剩一个 C++ 入口、`stdlib-declarations --check`、`stdlib-reference --verify-ops`）与 C2 的五条（三后端规范 jgir 逐字节相同、闭包、库函数、原函数不变、一条负例诊断）**一条都没跑**。

`lane/pass-decl` 上已知的现状（只读核对过，见空载验证前的那份 diff 阅读，供新格判"接还是重做"）：

- `src/kernel/execute/registration.cpp` 里两个 `registerPassKernel(const std::string &path, …)` 的**定义已删除**；
- `src/kernel/include/camel/execute/pass/registration.h` 里那两个路径重载**声明也已删除**，改为 `PassKernel` / `PrintingPassKernel` 两个类，构造里直接 `registerModulePass`；
- `src/kernel/execute/pass_declarations.h` 里 `PassDeclaration` **新增 `operatorUri`**（`#op('std:nvm')` 标签），`src/parse/src/pass_declarations.cpp` 读它；`src/stdlib/std.cml` **+327 行无体声明**。
- 仍按字符串落盘的是 `src/kernel/execute/pass.cpp` 的 `registerModulePass(const std::string &path, …)`（`:300`、`:352` 两处）。按负责人口径，**这不欠 C1 第 2 步的账**（判据是"路径重载为 0 + `findPassKernel` 有调用者"，两条都已满足）；"遍内核登记表只剩一张"是另一件事，尚未定是否单开一格。

## 4. `task-361` 那份交接页**不是** C1→C2

`internal/collab/task-361-opt-inline-steps-2-3-handoff.md` 写的是 **`opt::inline` 这个遍的第 2、3 步**（Camel 写的遍 + 与 `std::cxx::inline` 的逐字节对照），依据是 `internal/plans/2026-11-28-opt-inline.cn.md`。

**依据**：那份页的目标是"内联遍的选择规则照写、语料逐字节比、`cxx_isolation.plan.toml` 加 `inline.by_cpp`/`inline.by_camel`"，与 C1（遍的名字与登记）／C2（遍在程序里被调用）**是两件不同的事**；它里面没有任何 `registerPassAlias`、`findPassKernel`、`#op` 登记或"程序里 `f->std::cxx::cse`"的判据。C1→C2 的卡面只有第 2 节那两行。

## 5. 新格从哪开始

1. **等闸门降下来再开枝**（负责人已定：合并位合掉队列里五条后开 `lane/` 新枝，不借闸门例外）。开工第一句照规矩报 `git status --porcelain` / `git log -1 --oneline` / `git rev-parse --abbrev-ref HEAD`。
2. **第一个该跑的判据是 C1 的第一条**：`git grep -n "registerPassAlias\|passAliases" -- src` —— 在基上应为 0；同一口气把 C1 其余四条跑掉（遍注册表的读者、`extract` 的 C++ 入口、`stdlib-declarations --check`、`stdlib-reference --verify-ops`），**先判 C1 是否已经成立**；成立就直接进 C2，不成立再从 `lane/pass-decl` 那批改动上接（第 3 节列了它已做到哪）。
3. **C2 的第一条读数**是"同一个 C++ 遍，程序里调用与命令行上跑，规范 jgir 逐字节相同（nvm、fvm、jit）"—— 写文件比 sha256，别只看"一致"；随后依次是闭包、库函数、原函数不变、以及 `f->std::nvm` 那条负例诊断的原文。
4. 判据口径按负责人最新给的：**73 表 141 名**逐名比（不是旧表的 149 名）；交回带 `[provenance]`、`numstat`、枝名@sha、`dirty=0`。

## 6. 上一格的状态

**不接实现**：本会话上下文已到零，负责人裁定 C1→C2 转新格；本页是唯一产出，未开枝、未改 `src/`、未提交（未跟踪文件一件）。