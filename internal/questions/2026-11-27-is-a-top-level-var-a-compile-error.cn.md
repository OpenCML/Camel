# 模块顶层的 `var` 是编译错误，还是模块的资源？

一页一问。规范页与实现设计页在同一个仓库里，**两边都自称权威，对同一件事给出相反答案**；代码和用例站在其中一边。谁改谁只有负责人能定，所以我（一条车道）停在这里不动代码。

## 问题

模块顶层写 `var x = …`，今天**被接受**，它是一份模块级的可变状态（一个格子/the module's resource），可以被本模块的函数读写，也可以 `export` 出去给导入方读写。规范页说它应当是**编译错误**。两种说法都写在仓库里，而且都不是随口一提：

```
· 规范页（docs/design-principles.cn.md，AGENTS.md 说它优先于代码与其他文档）
  :200  模块顶层只有声明：它只求值一次，没有可以重新绑定名字的地方，所以顶层的 `var` 没有用处：
        **编译错误，提示用 `let`**。
  :756  模块顶层的代码不会默认执行 …… 顶层只有声明 …… 也没有可用的 `var`（3.5）。
  :203  真正需要共享的可变状态 …… 用显式的可变引用 `Ref<T>`：`ref(v)` 创建，`get` 读，`set`/`update` 写。

· 实现设计页（docs/design/graph-model/README.cn.md，第 7 步标「已完成」）
  :1548 「导出值：`export let` 导出的是 root 上的那个节点 …… **`var` 也可导出，是模块的资源**
        （原先的 `ExportedVariable` 已删除）」
  :1711 「`var` 可以写，不能导出（已定）」（命名空间块）
  :1522 第 6 步的验收里也写着「导出的函数可以使用自己模块顶层的 `let` 与 `var`」
```

`:1548` 那句里「原先的 `ExportedVariable` 已删除」是关键：它说明模块级 `var` 不是没人管的遗留，而是**被主动改过、被当成能力在维护**的一条。

## 代码与用例站在实现页一边（三条证据）

```
① test/cases/effects/module_var_read.cml
   文件头：「Reading a module-level `var` is the other side of effect masking: the cell is the
   module's, not the body's ……（design principles 3.3）」；正文 `var counter = 3`
② test/cases/effects/module_var_write.cml
   同一族：写模块 var 是被支持的形状，并且有专门的效应诊断
   test/cases/effects/err_module_var_in_clause.cml —— 模块 var 写进 `pure` 的 `uses` 子句才被拒
   ⇒ 被拒的是「把模块 var 写进 uses」，不是「存在模块 var」
③ test/cases/linear/toplevel_var/ 一整个目录（10 个用例）钉住这条能力的每一面：
   counter.cml（本模块读+写）、initial_value.cml（初始化用另一个顶层值 `base + 5`）、
   unread_write.cml（写了没人读）、argument_order.cml（与普通实参的次序）、
   err_pure_read.cml / err_pure_parameter.cml / err_uses_other.cml / grad_refused.cml（拒绝面）、
   **counter_lib.cml + exported.cml（导出并被导入方读写）**
   运行它们的计划：`feat/linear/fvm_nvm_diff`（其中 5 条）、`feat/compile/effects_syntax`、
   `feat/effects/masking_transition`、`feat/opt/defun`，以及 `feat/parse/golden/ast_hashes.txt`（金标）
```

## 爆炸半径（裁决要的最硬一个数）

```
模块顶层声明 `var` 的用例           47 个 .cml（`rg -l '^var ' test/cases -g '*.cml'`）
  其中 `export var`                 1 个：test/cases/linear/toplevel_var/counter_lib.cml（`export var count = 0`）
  被另一个模块读/写                 1 个：test/cases/linear/toplevel_var/exported.cml
                                    （`import { count, bump } from counter_lib`，`count = 10` 写、`count` 读）
  只在本模块内读/写                 46 个
```

⇒ 「按 3.5 收回」要删的不是「一个没人用的写法」：它要**改写 47 个用例、重录 `feat/parse` 的 `ast_hashes` 金标、动 `feat/effects` 与 `feat/compile` 的期望**，并且会改效应面（模块 cell 是一份资源读，`module_var_read.cml` 的注释就是按这条写的）。这不是「加一条诊断」的量。

## 我为什么没有按 S8 开工

`internal/plans/2026-11-16-direct-ssa-construction.cn.md` 的页末批复第 4 条已经把它定成 **S8**：「模块级 10 个 `var` 不进 `Ref<T>`，作为 S8（原则 3.5：顶层 `var` 是编译错误）」。也就是说，计划页按规范页这一边规划过。

但落地这一刀要先回答一个问题：**3.5 那两行还算不算**。因为
- 依据规则 14「不为让检查变绿改语义」：冲突两边都是仓库里的设计页，我不能挑一边当它是错的；
- 依据 `camel-lane`：不动金标与计划期望，不是一条车道能替负责人改的；
- 而反过来，如果规范页这一边说对了，那实现页 `:1548`/`:1522` 与那 47 个用例是一处**实现了不该实现的能力**的缺陷，也不是「加一条拒绝」就能了结。

## 默认做法（在回答之前）

**保持实现现状**（模块级 `var` 继续被接受，代码不改、用例不改、金标不动），这条冲突记进 `internal/backlog.md` 的缺陷行（`C-2026-11-27-1`，状态「待负责人裁」）。理由是保持现状不会让任何检查变绿或变红，而两种改法都会让一批绿的用例变红，撤不回来。

## 要回答的两件

1. **3.5「顶层的 `var` 是编译错误」还算不算？**
   - 若算：实现页 `:1522`/`:1548` 要改回「顶层只有 `let`」，47 个用例与 `ast_hashes` 金标要按拒绝重写（这是「删掉一个已被维护的能力」，需要一句授权）。
   - 若不算：规范页 `:200`/`:756` 要改成「顶层 `var` 是模块的资源，导出即别的模块可读写的那一份状态」，并说明它与 `:203` 的 `Ref<T>` 各自管什么。
2. **若保留模块 var，`export var` 允许吗？** 实现页 `:1548` 说可以（`counter_lib.cml` 就靠它），`:1711` 说命名空间块里的 `var` 可以写但**不能导出**。这两句之间还差一条规则：模块顶层的 `var` 能导出、命名空间块的不能，是**有意的不对称**吗？