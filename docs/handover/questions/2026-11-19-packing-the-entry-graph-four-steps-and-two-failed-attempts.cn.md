# 让入口图打包，并把呈现取第 0 份：方案定稿与两次失败的机制

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `43286ebcb`）
**状态**：**方案已定死，等一次干净的落笔**（本页不是问题页，是接手页）
**触发**：作者 `b69635919`「为 `var` 签名建的每个图都打包，入口图也算」；本车道两次尝试都没落成，树已复原干净。

## 1. 要做什么（四步，方案定死）

1. `src/compile/gir/builder.cpp:148` 之后、`:152` 之后，各加一行（缩进随所在块）：

   ```cpp
   if (auto *t = rootGraph_->funcType(); t != nullptr) rootGraph_->setFuncType(t->withExitType(graphExitTypeOf(t)));
   ```

   **改这两处时：`old_string` 必须包含那两行各自前面的唯一上下文（`rootGraph_->setFuncType(rootTypeFactory.build());` 及其所在块的上一行），替换文本只许写那新增的一行并在其后保留原行**——不许把整段重抄一遍（见 §3②）。

2. `src/passes/trans/cpp/program_emitter.cpp:528`：`entry.main->funcType()->exitType()` 若是元组，取**第 0 份**作为 main 的返回类型。

3. `src/execute/entry_args.cpp:607`：结果类型若是元组，取**第 0 份**再判可失败类型。

4. **不改**：`src/execute/exec_plan.cpp:1855`（它问「出口 ≠ void」，元组本来就不是 void，判据不变）；`src/passes/sched/fastvm/compile.cpp:146` 与 `src/passes/trans/cpp/function_emitter.cpp` 的三处（**红线**）。

## 2. 判据

- `linear` 的 `113` **必须减少**；`killed` / `timeout` 必须为 0；涨了或出现 `killed` ⇒ **立刻回滚**；
- `compile` 那 5 条 `(0, 1)` 逐条报：变绿，或换成另一种红并贴原文；
- `opt 4` / `tools 1` **不许涨**。

**已知缺口，必须写进提交信息**：`fastvm` 路径下入口仍会印元组；那一处读者在红线名单上，留给运行时侧。所以 `compile` 的 5 条不会全部变绿，没绿的那些要看是不是走 `fastvm`。

## 3. 两次失败的机制（这是本页最值钱的部分）

- **①（脚本）逐字替换没匹配**：缩进不对 ⇒ **`diff` 为空** ⇒ 一行也没改。**而「`diff` 为空」是最危险的一种失败：它看起来像成功。** 本轮我就是先报了「已加」、随后发现 `git diff --stat` 是空的。
- **②（手改）`old_string` 只含结尾一行，而替换文本里又写了一遍整段** ⇒ 结果 `diff` 里出现**重复的 `else if` 分支**与**一个不存在的函数名**（`createDataNodeOr`）。机制是：**`old_string` 太短、替换文本太长，两者不匹配，而编辑器不会告诉你这一点。**
- **③ 两次都立刻回滚、都没有再试** —— 那是唯一对的做法；在一条「涨了就回滚」的红线上，第二笔盲改比第一笔更贵。

## 4. 给下一个人

这一笔只需要**三次编辑**（`builder.cpp` 两处各一行、两处呈现各取第 0 份），方案已经定死，判断部分为零；**若你也在长上下文里，先开一个新的，不必从头查**。设计依据在 `2026-11-19-what-the-user-sees-of-the-packing-the-entry-graph-makes.cn.md`（默认甲：包内部存在、呈现取第 0 份）与作者的 `b69635919`。

## 默认建议（写死）

**默认建议**：按 §1 的四步落笔，方案不再讨论 —— `src/compile/gir/builder.cpp` 那两处各加一行
`withExitType(graphExitTypeOf(t))`，`src/passes/trans/cpp/program_emitter.cpp` 的入口与
`src/execute/entry_args.cpp` 各取元组**第 0 份**；§1 第 4 条的四处在红线名单上，**一处不动**。
**判据**：① 改完先看 `git diff --stat` **非空**（§3① 说的那种「看起来像成功」只由这一行读数否掉）；
② `-Plan feat/linear -Jobs 8` 的 **113 必须减少**、`killed`/`timeout` 为 **0**，涨了或出现 `killed` 立刻回滚；
③ `opt 4`/`tools 1` 不许涨；④ `compile` 那 5 条 `(0,1)` 逐条报（变绿，或换成另一种红并贴原文），
走 `fastvm` 而没绿的那几条写进提交信息。
