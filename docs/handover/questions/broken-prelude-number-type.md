# 前奏的 `number` 已在 `733e88213` 修好：本页记根因定位与两条残留

车道 `handover/prelude-number-type-question`，起点 `origin/develop` `f3e0594f2`；读数取 `origin/fsh-dev` `4e6f2262a`（修复在 `733e88213`）。这是**事后记录**：问题已经修好，决策者也已在 `docs/handover/reviews/questions-2026-10-26-late.md` 作答。下面只记我们这一侧量到的根因，以及两条仍需处理的残留；本页不再提问。

## 决策者的答复（原话）

> 读数对，原因也对；这是决策者这边合入时带进的缺陷，抱歉。
>
> - **已修，在 fsh-dev 的 `733e88213`**：前奏里那八条数组转换改成泛型声明（`i32::<T>(x: T[]): i32[..] when is_numeric::<T>()` 这样），不存在 `number` 这个类型，也不该有。前奏里已不剩别的裸类型名，第一条车道修完在 `733e88213` 上跑了整个快层：7448 个测试，通过 7427 个，剩下的 9 条与此无关（见上一节）。
> - **不要退回 `a91dcbdfa`**，直接以 `733e88213` 为准合 develop。若你们那条排查车道还找到别的缺失名字，写在问题页里，我们修。
> - **流程**：合进 fsh-dev 的每一批，决策者这边今后都先跑一次快层再推送，不只跑车道里改动涉及的计划。
>
> **`var` 形参的读取留在链上（`builder_expr.cpp:1475` 的 `GCNodeKind::Port` 分支）**：语义是有意的，写法是过渡的。
> - **语义**：一个 `var` 形参可能是调用方把同一个变量传了两次，被调函数分不清两个名字是不是同一个单元，所以这次读只能靠写下它的位置，和另一个名字的写排序。
> - **写法**：它现在接的是控制边。删控制边那一批里，它与其他语句的链位一样改成同步边，不留控制边，也不另设特例。

修复本身（`733e88213` 对 `stdlib/prelude.cml` 的差分）把那八条改成泛型，并顺手把结果拼写从 `int64[..]`、`f64[..]` 统一到 `int[..]`、`float[..]`：

```
-export func i32(x: number[]): i32[..]
+export func i32::<T>(x: T[]): i32[..] when is_numeric::<T>()
```

## 根因定位（我们量到的）

**(1) 不是 `0f6c2f3b6`。** `git show --stat 0f6c2f3b6` → `4 files changed, 10 insertions(+), 2 deletions(-)`，四个文件全是文档（`docs/design-principles-log.cn.md`、`docs/design-principles.cn.md`、`docs/design-principles.en.md`、`docs/handover/reviews/questions-2026-10-26-late.md`），代码 0 行。

**(2) 引入点是 `59d2b574b`**（`feat(prelude): the declarations of the built-in functions are compiled, and their lambdas are untyped`）：它在同一次提交里写下那八条 `number[]`，又让这些声明参与编译。

```
git show 59d2b574b^:stdlib/prelude.cml | grep -c 'number\[\]'   → 0
git show 59d2b574b:stdlib/prelude.cml  | grep -c 'number\[\]'   → 8
```

同一提交里 `stdlib/prelude.cml` 改 86 行，并加上编译侧接线（`src/compile/gir/builder_modules.cpp` +41、`src/compile/gir/type_arguments.cpp` +8、`src/core/context/context.cpp` +24、`include/camel/core/context/context.h` +18）。于是这八处第一次进入类型解析时，树里没有 `number` 的任何声明：`git grep -c 'type number' 0f6c2f3b6 -- stdlib modules` → 0 行（无输出，exit 1）。

**(3) 再往前是"每个程序都解析、但声明不编译"。** `6532c872d`（`The prelude's declaration file is read with every program, the formatter knows a type without a right side, …`）加了那条读路径（`src/core/module/userdef.cpp` +8），当时文件名还是 `stdlib/signatures/globals.cml`，里面没有一处 `number[]`。文件名沿革：`6857ac2bc` 新增 `stdlib/signatures/globals.cml`；`5b3571094` 把它 R100 改名为 `stdlib/prelude.cml`；`59d2b574b` 让声明参与编译。

**(4) 事发时的读数（`0f6c2f3b6`，全量快层）**：`total 7447, pass 240, fail 7196, skip 7, known failures 4`；单因 `Unresolved type reference: 'number'` 重复 6975 次（占 7196 的 96.9%）。基线 `a91dcbdfa`：`total 7443, pass 7383, fail 54`。

## 两条仍待处理的残留

**① `stdlib/prelude.cml:10` 的文件头注释已失真。**

`4e6f2262a` 上这一行仍是：

```
 * For reference only: these declarations are not compiled. They are checked against the
```

而自 `59d2b574b` 起这些声明参与编译——这次崩溃的机制正是"声明被编译、而名字不存在"。建议把这句改成"参与编译"的写法；同段其余句子也以"不编译"为前提，改时一并看。

**② 新用例叠到 develop 上编译不过（不是 develop 的红）。**

我们这一侧测得 `develop + 2097d73c9`（`fix(gir): a var read in a sync body is not a statement of the chain`）：`total 6996 / pass 6970 / fail 20`，其中 `var_between_statements.*` 6 条全红，报

```
test/cases/linear/runtime/var_between_statements.cml:5:1: [Error]: Unexpected 'module'
```

原因是那条树里这份用例第 5 行写着 `module var_between_statements`，而 `module` 声明在 develop 上已退役：`cd41c81c3`（`Retire the module declaration: a file is a module, named by its file`）经 `2720b5216` 合入，develop 自己的同一份用例第 5 行是空行，`git grep -l '^module ' origin/develop -- test` → 0 个文件。纯 `develop` `f3e0594f2` 是 0 红——这 20 条红只在把带旧结构的用例叠上去时出现。该用例按退役后的写法改（删掉那一行）即可，不需要动 develop 的语法。

**另注**：`4e6f2262a` 上 `stdlib/signatures/tensor.cml`（`:21`、`:193`、`:219`、`:225`、`:232`）与 `stdlib/signatures/nn.cml`（`:134`、`:144`）里还有 7 处 `number[]`（`git grep -n 'number\[\]' 4e6f2262a -- stdlib` 的其余命中都在这里）；这两份文件今天是否参与编译，我们没量。前奏本身在 `4e6f2262a` 上已无 `number[]`。

## 事发时的影响面（`0f6c2f3b6`）

- `fail 7196` 里 6975 条是同一个原因，其余 221 条不是这条。
- `skip` 2→7：新增 5 条，用例名 `device_startup.*` 两条、`gpu_preference.named_device`、`gpu_scope.*` 两条；`discovered` 269→270。
- 下列计划的红数在 prelude 挂掉时不反映各自的收敛：`stdlib_faults` 112、`attempt` 144、`docs_ai_reference` 3。
- `check-doc-links` 那条继承红在 `0f6c2f3b6` 上已经是 0，已从待办里划掉。

本页只作记录：不改代码、计划、断言或生成物。