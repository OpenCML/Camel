# 模块顶层的 `var` 与顶层外部写：谁来做那 35 个用例的迁移？

## 问什么

作者任务书（2026-11-14，`origin/fsh-dev` `c8af40747`）把「状态规则」放在同一条规则下，而这条规则
**同样管模块顶层**：顶层只被求值一次，它声明的每个函数都被交出去，所以任何函数捕获顶层 `var` 都是
把那个格子带出唯一一次求值——顶层 `var` 因此无从使用；同理，顶层绑定不得写外部资源（导入模块不跑代码，
`let banner = println(...)` 会在导入时执行）。

两处拒绝我都写了、都实测了，落不下来，卡的不是规则本身，而是**它连带要迁的东西不在本车道里**：

1. **顶层 `var`**：拒绝点是 `src/compile/gir/builder_expr.cpp#visitNRefNode` 里 `isVar && currGraph_ == rootGraph_`
   的五行，诊断 `SemanticDiag::TopLevelVariableUnusable`（0x00020E，已写、已生成）。
2. **顶层外部写**：拒绝点是「一次顶层声明建出的那些节点」（`src/compile/gir/program.cpp#buildTopLevelDeclaration`
   的 `firstNode` 到当前 slot 数）里第一个写外部资源的节点，诊断 `SemanticDiag::TopLevelExternalWrite`
   （0x00020F，已写、已生成）。

两个诊断的文案里都写了「用 `let`」和「把写搬进 `main` 或它调用的函数」两条出路。

## 证据（原文照录）

带这两处拒绝的树，跑 `-Changed -ChangedBase 5ec45c70f -Jobs 8`：

```
[SUMMARY] total: 5022, pass: 4755, fail: 246, skip: 3, known failures: 18, killed by a signal: 16
RUN_EXIT=1
```

其中两个不是用例而是**文档本身编译不过**（`docs.links` 的 `snippets_compile`）：

```
docs/language/05_modules.md:234: does not compile: error[SemanticDiag::TopLevelExternalWrite]: 'words' is a value of the module's top level, and computing it writes an external resource.
docs/language/07_statements.md:38: does not compile: error[SemanticDiag::TopLevelVariableUnusable]: 'count' is a `var` of the module's top level, and nothing can use it.
docs/language/en/05_modules.md:113 / :234、en/07_statements.md:38 同上
```

其余 244 条挂在这些面上（`[FAIL]` 名字去重后的家族）：`feat/linear/toplevel_var` 整个计划（它的题目
就是「模块顶层的 `var` 是模块的资源」）、`feat/modules` 的 `imports.d1_var`/`d1_shadow`/`d2_value`/`root.*`、
`feat/linear` 的 `counter.*`/`captured_write_*`/`callback.*`/`entering.assignment.*`、
`feat/compile` 的 `nested_function_value.*`/`named_struct_types`、`feat/effects` 的 `effects`。
设计稿自己写着这一项是「35 个测模块顶层 `var` 的用例改写或删除」——那 35 个文件正是这些计划的主体。

**把两处拒绝撤回之后**（本车道交付的形状），同一个门只剩状态规则自己的读数：见
`test/plans/feat/compile/state_escape.plan.toml`，8/8 绿；`-Changed` 的红集只有基线就有的那几条
（报表里点名）。

顶层 `var` 今天仍然照旧工作（这是**没被拒绝**的读数，不是规则说它合法）：

| 程序 | 命令 | 读数 |
| --- | --- | --- |
| `test/cases/effects/module_var_read.cml`（模块 `var counter` 被 `peek`/`bump` 读写） | `camel module_var_read.cml` | exit 0，stdout `3` |
| `test/cases/compile/state_escape/ok_top_level_value.cml`（顶层 `let` 常量 + 函数值） | `camel ok_top_level_value.cml` | exit 0，`11` / `22` |

## 现状（本批交付的形状）

- **规则本体已落**：闭包不得把 `var` 捕获带出声明它的那次求值，三条出路（结果、写进不属于这次求值的格子、
  交给可能留下的函数/外部读者）都是编译错误 `SemanticDiag::ClosureCaptureEscapes`（0x000210），
  文案给出作者点名的两条改法（捕获当时的值 / 把状态作为值进出），**不**提供「悄悄地改成按值捕获」。
  单测：`test/plans/feat/compile/state_escape.plan.toml`，7 反例 + 2 正例全绿。
- **模块顶层**：两个诊断与两处拒绝点都写好了，然后**撤回**，因为撤回前 246 条红、文档自编译失败。
  撤回后不残留任何「无产者的诊断」（`--no-producer` 干净）。

## 默认建议（按它开工，不等回复）

**本车道只交闭包状态规则，顶层两项留给一条专门的迁移车道**，理由是代价归属：
244 条红的修复不是改规则，而是「把 35 个用例和 2 段文档例子从顶层 `var` 改写成 `let` 或改成函数进出状态」——
这是作者已经预见并写进设计稿的一项独立工作，混进状态规则这条车道会让「规则是否成立」这件事
被 244 条改写淹没。**不要在一条车道里既改语义又改 35 个用例**：一旦有改写是错的，就分不清是规则错还是改写错。

迁移车道的入口就是本文档上面那两处拒绝点 + 两个诊断码（0x00020E / 0x00020F，已经生成过一次、又从
registry 删掉，重加只需三行）。若作者要求**同一批**完成，则本车道再加一笔提交：恢复两份诊断、恢复两处
拒绝、迁移 35 个用例与 2 段文档例子，并让 `feat/linear/toplevel_var` 从「顶层 `var` 是资源」改题为
「顶层 `var` 被拒绝」。
## 决策者答复（2026-11-14）

**由第 3 路自己迁移，这是 R3 的一部分，优先级一。** 规则已由负责人定（原则 3.5 规则二及其两条推论），迁移是它的一半，不能因为面广就只落一半。做法：

1. **测"模块顶层的 `var` 是模块的资源"这个功能本身的用例删掉**：功能已取消（`feat/linear/toplevel_var` 整个计划、`imports.d1_var`/`d1_shadow` 这类）。原则说不保留已删的东西，也不留指向新写法的诊断记录（第 1 节"不留过渡"）。
2. **借顶层 `var` 做别的事的用例改写**：计数、累加、回调之间的状态，按 `docs/design/state/README.cn.md` 第 4 节的标准写法改：循环携带状态、函数返回新状态、`with` 注入；外部框架回调之间的状态用 `Ref<T>`（`Ref<T>` 本身没落地之前，这类用例先改成传状态的写法，或等 `Ref<T>` 一起落）。期望输出不变；变了的逐条列出交决策者。
3. **文档示例**（`docs/language/05_modules.md:234`、`07_statements.md:38` 及英文版）按同一节的写法改写。
4. 然后把两处拒绝加回来；`--no-producer` 干净、`docs.links` 的 `snippets_compile` 绿、`-Changed` 的红集回到基线。

`counter_closure`、`captured_write_*`、`callback.*` 这类用例若测的是"闭包带着私有可变状态离开函数"，它们本身就是规则二要拒绝的写法：改成测编译错误 `ClosureCaptureEscapes` 的反例。
