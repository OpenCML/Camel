# 两个探针补丁：从文字重建

2026-11-15。清理车道按令清理 `lane-escape16` 的工作树时，`.probe/` 目录（23 个文件、
111,617 字节）被一条并发的清理删掉。目录里除了试验用的 `.cml`、gate 日志与两份 plan
全文，还有两个 **`.patch`**：

| 文件 | 改动对象 | 作用 |
|---|---|---|
| `nocheck.patch` | `src/compile/gir/program.cpp` | 注掉 `checkCellEscapes()` 的调用 |
| `oldplace.patch` | `src/runtime/draft_inline.cpp` | 把 `placePreds.empty()` 这一支关掉 |

**丢的是原文件，不是结论。** 两个补丁都是**一行的、可逐字重放的**，下面把改前原文与改后
原文都写出来，任何人都能照着重做。本文存在的唯一理由：**「把守卫关掉之后仍得到同一张图」
这类反证点，一旦补丁丢了，复核就得从头写一遍。**

## 一、`nocheck.patch`

改前（`src/compile/gir/program.cpp:959`，原文一行）：

```cpp
checkCellEscapes();
```

改后（原文逐字，含它自己留的标记）：

```cpp
                // checkCellEscapes(); // .probe/nocheck.patch -- counter-test only, reverted before any commit
```

定位用：hunk 头 `@@ -956,7 +956,7 @@ bool Builder::buildFrom(ModuleUnit &entry, bool callEntry)`，
blob `index c664dc3a1..dfaa32fba 100644`。

它关掉的是**构建期**的闭包逃逸检查（`checkCellEscapes` 定义在
`src/compile/gir/builder_state.cpp:254`）。这一支的作用是让 `ClosureCaptureEscapes`
的拒绝**不再发生**，从而观察「拒绝消失之后优化管线会不会给出不同的图」。

判据方向：**若关掉它与开着它得到同一张图，那么这条检查就只是提前拒绝，而不是在掩盖
一个优化缺陷**——两者对「程序能不能跑」的结论相同，但对「该修哪里」的结论相反。

## 二、`oldplace.patch`

改前（`src/runtime/draft_inline.cpp:2387`，原文一行）：

```cpp
    if (placePreds.empty()) {
```

改后（原文逐字，含它自己留的标记）：

```cpp
    if (false && placePreds.empty()) { // .probe/oldplace.patch -- counter-test only
```

定位用：hunk 头 `@@ -2384,7 +2384,7 @@ DraftInlineResult inlineCallableInDraft(`，
blob `index 5edfab90d..436fe3f16 100644`。

**只动 2387 这一处。** 同文件里 `placePreds.empty()` 还有 2421、2436、2477 三处别的判定，
改错位置就白跑——这也是为什么原文行号必须写进本文。

这一支是 `a1723fbb9` 引入的「调用所在的位置」判定：调用落在哪条链上，由它的 `sync`
前驱（或同 pair 的 control 前驱）决定；没有前驱时退到控制前沿。上面 2380–2397 行的注释
就是这条判定的理由原文（`docs/design/effects/edges.cn.md` 5.7 与 `chainLinkSaysSomething`）。

判据方向：**把这一支关掉（总是走「控制前沿」那条）之后，内联出来的图与开着它逐字相同**
⇒ 这条判定**没有被证明有用**。`test/cases/opt/inline_cycle/seed23_fold_inline_within_call.cml`
与 `test/cases/opt/adb67e081/rc04_seed72_within_call.cml` 的文件头写明了这一点：
它们断言内联**发生了**（`direct_calls 24→3`、`2→0`），但**不**守卫那条 place 判定。

## 三、重建方法（谁要重放就照这个做）

**先看这一节的一句话：只有补丁、没有输入，下一个人仍然跑不起来。** 同一个目录里还丢了
下面这些，它们**同样不在历史里**：

| 类别 | 丢掉的 |
|---|---|
| 重放闸门的**输入** | `stage2/adb67e081.full.toml`、`stage2/inline_cycle.full.toml` |
| 手写**候选用例** | `cand_rc04_within_call.cml`、`cand_seed23_within_call.cml`、`cand_shapes.cml`、`tA.cml`…`tE.cml`、`w23.cml`、`wrc04.cml` |
| **读数** | `gate-adb.log`、`gate-inline.log`、`gate-run.log`、`escape16-round2.txt`、`escape16-table.txt` |

`gate-*.log` 里的结论已经在提交正文与两条 `.cml` 的文件头里，`stage2/*.toml` 的判据可以按
`test/plans/feat/opt/inline_cycle.plan.toml` 与 `adb67e081` 那两份 plan 重写，候选用例可以
按文件头的断言反推——**但没有一份是原样可用的**。这就是「清理未跟踪目录」的实际代价。

1. 在一条车道工作树里改上面两行（一处一次，不要同时改）；
2. `npm run build`；
3. `camel <用例> std::opt test::exec_plan`（或 `std::opt::inline test::exec_plan`）各跑一遍，
   把执行序存下来；
4. 与不改时的那份逐字 `diff`；
5. **不要把这两处改动提交**——它们的存在是为了取读数，不是为了进入树。

## 四、这次为什么会丢

派清理车道时我给的判据是「删除未跟踪的临时产物」，而**未跟踪并不等于可删**：判别式不是
「它在不在 git 里」，而是**「有没有哪份提交正文引用过它」**。`.patch` 天生是别人复核时
唯一能重放的东西，**清理看到 `*.patch` 就该停**，这一条本该写在派工的黑名单里。

同一个教训的另一种说法（2026-11-15 早些时候，`refcore` 那笔 revert 连带删掉问题页）：
**撤销的结果通常是「少了什么」，而少了什么在 `git log` 里看不出来。** 那一次把页面恢复
回来再更正；这一次没有可恢复的东西，所以只能把内容写下来。

—— the owner, 2026-11-15