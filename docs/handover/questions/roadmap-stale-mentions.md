# `ROADMAP.md` 仍在提已从编译器删掉的诊断 `TryCatchNotInLanguage`

基线 `origin/develop` `75c095089`（`Merge origin/fsh-dev (4e6f2262a): take the author line's newest tree, in which the unparsable prelude is repaired.`）。下面每条读数都在这个 tip 上现取，命令与整行读数一并给出。

## 我量到的

**(1) 这个名字现在只出现在三个文件里，共 5 行**（行号与整行原文，反引号照原文保留）：

```
git grep -n 'TryCatchNotInLanguage' origin/develop -- ROADMAP.md docs
```

```
ROADMAP.md:60
- `[定]` `??`、`!!`、`->` 实现为图上的分支；`throw` 实现为返回错误值；`try`/`catch` 从语法中移除并给出指引性的诊断。（状态：部分完成——三个运算符、`throw` 与对 `try` 的指引性诊断 `TryCatchNotInLanguage` 已实现并有测试（`test/plans/feat/compile/tagged.plan.toml`）；`throw` 接受什么、`->` 能否用于 `T?`、`??`/`!!` 右侧的类型按建议实现，标为 `DESIGN-QUESTION(nullable-error)`，待裁定）

docs/handover/README.cn.md:212
5. **清理保留字**：`is`、`of`、`keyof`、`try`、`catch`、`finally` 这些不进语言的词不再保留，写了就是普通的名字或语法错误；展开 `...xs`、结构体和数组解构同样处理。删掉只为指路而存在的诊断：`ConstructNotSupported`、`FeatureNotSupported`、`TryCatchNotInLanguage`（负责人 2026-10-25："try catch 不保留，应该是历史债"；设计原则 3.9 已改）。`throw`、`namespace`、`using`、`typeof`、`constas` 都是现行语法，保留。

docs/handover/README.cn.md:637
   - **编译器这一侧同样处理**：已经裁定的写法，按裁定实现；没有裁定、也不打算有的写法，变成普通的语法错误或未知名字，删掉只为指向别处而存在的专用诊断（`ConstructNotSupported`、`FeatureNotSupported`、`TryCatchNotInLanguage` 等，以及消息里点名内部任务编号的那几条）。词法表里"保留，尚未实现"那一行一起处理：仍有用的关键字放进对应的分组，没用的就不再保留；

docs/handover/reviews/questions-2026-10-24-evening.md:1083
收下：第 1、9、17 章按评审改了（开头讲为什么、图由脚本从例子生成并接进检查、`sync` 带理由、第 17 章换成无条件的定律并显式交给 `std::opt`）；`is`、`of`、`keyof`、`try`、`catch`、`finally` 不再是关键字，`TryCatchNotInLanguage` 删了；格式化器的声明头超过 80 列时子句另起一行；编辑器语法快照不再永远跳过，开发依赖进了安装步骤。

docs/handover/reviews/questions-2026-10-24-evening.md:1092
2. **注册表里"`# 0x000074 was TryCatchNotInLanguage`"这类注释**（6 处）：记录已删诊断的名字，就是给删掉的东西留记录。空号只写"空号"不写原名，等最后诊断码重排时一并收掉。
```

**(2) 编译器里已经没有这个名字。**

```
git grep -n 'TryCatchNotInLanguage' origin/develop -- src include
→ 0 行（无输出，exit 1）
```

原来的诊断码只留空号：`include/camel/core/error/diagnostics/messages/semantic.h:80: // 0x000074: empty. The number is not reused.`

从加名到去名的三处提交（逐一点数 `src`+`include` 的命中行）：`17ac776a9` 起有（`semantic.h` 1 行、`semantic.cpp` 2 行、`builder.cpp` 1 行）；`f5750e1fb`（`refactor!: 清理保留字：is、of、keyof、try、catch、finally 不再是关键字`）之后只剩 `semantic.h` 1 行注释；`88f33ef1e`（`chore(diagnostics): leave the retired code numbers unnamed`）之后 **0 行**。

**(3) 没有任何用例或计划在断言它，路线图点名的那份计划也没有。**

```
git grep -n 'TryCatchNotInLanguage' origin/develop -- test
→ 0 行（无输出，exit 1）

git grep -n 'try\|catch' origin/develop -- test/plans/feat/compile/tagged.plan.toml
→ 0 行（无输出，exit 1）
```

`tagged.plan.toml` 的 `desc` 只写 `T?`、`T!`、`??`、`!!`、`->`、`match` 与 error 值；它的用例覆盖的是这些形式，没有 `try`/`catch` 的指引性诊断。

**(4) 按已落的裁定，它应当不存在；记录里也写着已删。** `docs/handover/README.cn.md:212` 把它列在"删掉只为指路而存在的诊断"里；`docs/language/02_lexical.md:53` 写"`is`、`of`、`keyof`、`throw`、`try`、`catch`、`finally` 不是关键字：它们是普通的名字，写了就是普通的名字或语法错误（设计原则 3.9）"；`reviews/questions-2026-10-24-evening.md:1083` 写"`TryCatchNotInLanguage` 删了"。

**(5) 路线图这份文件仍在维护，所以这条过期状态会被读到。** `git log origin/develop --oneline -5 -- ROADMAP.md` 的第一条是 `02f0b0569`（`refactor(syntax): ?-> leaves the language; -> carries the null or the error along`）——同一节（3.9 带标记形式）的邻条刚更新过，而 `:60` 这条的状态仍是"已实现并有测试"。

## 没量到的

- 未量到 `ROADMAP.md` 里其它状态行是否同样过期：本页只查了 `TryCatchNotInLanguage` 这一个名字，没有逐条核整份路线图。
- 未量到 `README.cn.md:212`、`:637` 这两处在诊断删掉之后还算不算待办：它们是清理指令，与 `ROADMAP.md:60` 那条状态不是同一种文字，本页没有把它们分类。
- 未量到现在写 `try` 的程序实际得到什么输出：本机不构建、不跑编译器；只量到 `src`/`include` 里没有这个名字、`test` 里没有用例，以及 `docs/language/02_lexical.md:53` 说它会被当作普通名字或语法错误。

## 要问的一句话

`ROADMAP.md:60` 的状态分句与那两处清理待办里对 `TryCatchNotInLanguage` 的提及，是**历史记录（保留）**，还是**已过期的路线图条目（可删，或改写成"已删除"）**？

## 附一条现状（不要求回答）

"从不被读的计划"做成固定批次点的方案还在出（`test/plans/` 里登记了、但没有任何入口读到的那些）；方案出来后另开一页，不占这一页。

本页只报测量与一句询问：不改代码、计划、断言或生成物。