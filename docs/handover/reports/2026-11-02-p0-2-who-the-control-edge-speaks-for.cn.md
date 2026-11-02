# P0-2 第一步：ctrl 边今天还在替谁说话，以及把凭据里的 ctrl 一项去掉会发生什么

**这一轮只做两件事**：量现场（第 1 节）、把 `pass.cpp` 凭据里的 ctrl 一项去掉并贴两侧原文（第 2 节）。
**结论**：凭据一改就红 **`feat.opt` 18 条 + `feat.linear` 96 条**（`feat.trans` 96/96 仍绿），红的是
"变量/cell 那条路"——所以这一笔**不能先落凭据**，代码已回退，红名单在下面当"先红"的证据。

## 1. ctrl 边今天还在替谁说话

`git grep -c`，`src include modules test`（`ctrlPredsOf` 在树上不存在，`waitInputsOf` 才是个总口）：

| 名字 | 行数 | 文件数 |
|---|---|---|
| `ctrlInputsOf` | 194 | 62 |
| `ctrlUsersOf` | 50 | 21 |
| `writeControlLink` | 17 | 5 |
| `appendControlEdge` | 15 | 7 |
| `ctrlPredsOf` | 0 | 0 |

按文件排（四者合计，前 14）：

| 文件 | 行数 | 它在用 ctrl 干什么 |
|---|---|---|
| `src/runtime/draft.cpp` | 25 | 边的读写的实现本体（`appendControlEdge`、`ctrlInputsOf`、unlink），不是语义判定 |
| `src/runtime/draft_inline.cpp` | 17 | 内联时把实参的 ctrl 入边搬到形参，再把形参的搬回来 |
| `src/runtime/control.cpp` | 16 | `reduceControlDependencies` 的落点：证明"某个完成点已经保证我跑了"之后删掉 ctrl |
| `src/passes/test/control.cpp` | 13 | 上面那条的测试（`conditionalDataIsNotMandatory` 断言 JOIN 的一条臂**不该**是强制的） |
| `src/compile/gir/builder_control.cpp` | 12 | 前端把语句写成 ctrl（`writeControlLink` 的一族） |
| `src/execute/graph_runtime_support.cpp` | 11 | 执行前按 ctrl 决定"这一条跑没跑完" |
| `modules/autodiff/pullback.cpp` | 11 | 反向图里把前向的 ctrl 抄一份 |
| `src/runtime/graph.cpp` | 11 | 编码/seal 时读 ctrl |
| `src/runtime/writings.cpp` | 11 | 写集（`WrittenSlots`）用 ctrl 判"谁在谁之后写" |
| `src/passes/opt/generic/rewrite.cpp` | 10 | `handOverSyncWait` **同时写 sync 与 ctrl**（`rewrite.h:110-116`） |
| `src/passes/opt/generic/generic.cpp` | 8 | `pruneBranch` 的 `brchCtrlUsers` 交接 |
| `src/passes/opt/generic/gates.cpp` | 6 | `bypassValueGate` 把 ctrl 挪到消费者 |
| `src/passes/test/edges.cpp` | 8 | 边种类的测试 |
| `src/compile/gir/builder_expr.cpp` | 8 | 表达式的 ctrl |

**一句话答**：ctrl 边今天替三件事说话——(a) **语句顺序**（前端 `builder_control.cpp` 写、
`handOverSyncWait` 在 sync 边旁边再补一条）；(b) **变量的写后读**（`writings.cpp` 与
`reduceControlDependencies` 用它保证"这个 cell 的写在我之前"）；(c) **分支后代的完成**
（`pruneBranch` 的 `brchCtrlUsers`、`builder_expr` 的表达式）。只有 (a) 是 sync 边已经能表达的，
(b)(c) 都还没有对应的边。

## 2. 凭据里的 ctrl 一项：改法与两侧原文

改的是 `src/execute/pass.cpp:1925`，`effectStandsOnSomething` 的最后一支：

```
-    return !graph.syncPredsOf(ref).empty() || !graph.syncUsersOf(ref).empty() ||
-           !graph.ctrlInputsOf(ref).empty() || !graph.ctrlUsersOf(ref).empty();
+    return !graph.syncPredsOf(ref).empty() || !graph.syncUsersOf(ref).empty();
```

（`kGCNodeFlagSyncStatement` 那一支没动——那一行是 `guard` 的，按负责人这一轮第 2 条让开。）

**这一侧的读数（改后）**：

| 计划 | 读数 |
|---|---|
| `feat.opt` | **1123/1141/18** |
| `feat.linear` | **775/873/96** |
| `feat.trans` | 96/96/**0** |

**另一侧（改回）**：`feat.opt` 1141/1141/0、`feat.linear` 873/873/0（上两轮同一构建上量过）。

**红的名单说明红在哪**：

- `inline_argument_effect.inline.*` / `.opt.*`（8 条）——内联后实参效应的 ctrl 入边是它唯一的读；
- `var_params.*`、`var_tensor.*`、`mutable_reference.*`（opt_cse / simplify_cse / inline / opt 各种组合）
  ——**变量的写后读**，ctrl 是"我这次写在你那次写之后"的唯一凭据（`writings.cpp`）；
- `feat.trans` 一条不红——它是 dump 与 trace，不跑程序。

**所以事实是**：ctrl 的 (b)(c) 两种用法今天**仍是唯一凭据**，把凭据里的 ctrl 一项去掉，等于让
这两个形状"站在虚无上"，凭据检查（正确地）把它们全拒了。判据第 3 条要的"只站在 ctrl 边上 ⇒ 必须报"
**已经得到**：那 114 条就是它一次报出来的样子；判据第 3 条的另一半"有 sync 位置 ⇒ 不报"也在
（`feat.trans` 与 `opt` 里那些本来就靠 sync 的用例一条没红）。

## 3. 停在哪一步，要作者答什么

**停在"凭据先改"这一步**：判据第 2 条要的改动我做完了、也量了两侧，但按判据第 5 条它不能落——
114 条红不是靠改期望值能消掉的（那是真行为），而消掉它们要先把 (b)(c) 两族写点迁到 sync 边或
别的表达上，那是 65 个文件里 15 行 `appendControlEdge` + 17 行 `writeControlLink` 的工作量，
不是这一轮能做完的。**代码已回退**，树上干净。

**要作者答的一句**：ctrl 边的 (b)(c) 两种用法，**最终要变成什么**？
- (b) 变量的写后读：是走 `writings.cpp` 的 cell 版本链（`VarAfter` 那一族已经在做），还是给
  `sync` 边一个"跨块也算"的例外？
- (c) 分支后代的完成：`pruneBranch` 的 `brchCtrlUsers` 交接是不是该改成"交接给 JOIN 的 sync 后继"
  （这正是 5.3 的 sync 边，且 `handChainPlaceTo` 已经这么干了）？

**我方默认建议**：按 (b) 先做——把 `writings.cpp` 的"写后读"从 ctrl 换成 `:var/after` 的版本链，
这一族红得最多（96 条里的绝大多数）也最局部；然后凭据那一行才可以删。**不建议**给 sync 边开
"跨块例外"：那是把 `checkSyncEdges` 的"同一块"放宽，和片 4 撞的是同一道墙。

## 4. `control.cpp` 的 Gate 守卫

负责人提的 `control.cpp` 里那条 `h->kind != Gate || !existing.empty() || !syncInputsOf(user).empty()`
这一轮没动：它守的是"这条 Gate 的等待已经写在 sync 上了，别再补一条 ctrl"
（`src/passes/opt/generic/rewrite.cpp:511` 附近的形状），**不是**凭据检查。等 (b)(c) 迁完、
ctrl 不再被写之后，它才是删的对象——那时它守的东西已经不存在了。这一条记在这里，不在这一笔里动。