# `task-361`：`opt::inline` 的第二、三步 —— 交接

接手人：任何一格干净的车道。**余量够就能收尾**：底下的活是「一个读数 + 4 条语料 + 8 条计划条目」，没有需要设计判断的地方了 —— 两处判断（③④）已由负责人裁定 (a)+(a)。

## 0. 现场

```
工作树  D:\Projects\Camel-wt\op-names
枝      lane/opt-inline
基      origin/integration = 23dbcf2886（已 rebase 到它，落后时先 rebase）
tip     见本卡所在提交（三笔：a67106ecb3 第一步 + d7b4fc04c7 第二步 + 我的编译修复）
树      git status --porcelain 为空；未 push
```
第一步、第二步的代码**已经在这条枝上**，不是空白起步。新树若不继承 `internal/tools/camel-gate.local.json`（git-ignored），先从别的树拷一份，否则第一句就卡。

## 1. 第一步：已合格（不用返工）

`graph.cml` 的 `export func inline_call(at: Node) => Node`：
- `@fails` 写明三种拒绝：不是调用 / 被调的图不确定（经形参、正在组装的闭包）/ 调用的是正在编辑的图（递归）；
- `src/stdlib/graph/module.cpp` 注册为内核算子，并给 `drafts({0})` 权限；splice 走既有 `spliceCallInDraft`（`inline(t, at, fn)` 与它共用），**没有第二套复制逻辑**。

## 2. 第二步：交来时**编不过**，三处已修（tip 那一笔）

```
SemanticDiag::UnresolvedName             'drop' 没从 array import
SemanticDiag::GenericArgumentsMismatch   drop(calls, n: 1) -- drop 的 n 是 with 参数
SemanticDiag::ParamArgumentUnknown       'n' is not a parameter of '<n: int> (xs: T[]) => T[]'
（另：@example 少一个右括号，库示例检查会红）
```
修法：`import { flatten, all, any, drop } from array`；两处调用改 `drop<1>(calls)` / `drop<1>(gs)`；`@example (func (x: int) { x + 1 })->inline`。**没动任何既有断言，没动 C++ 侧。**

读数（`test/plans/feat/opt/cxx_isolation.plan.toml`，`-NoBuild` 不行，.cml 改动要重新 stamp）：
```
[SUMMARY] total: 15, pass: 15, fail: 0
```

## 3. 递归缺陷的探针读数（判 (a)：加读数）

探针：4 条语料 × 2 边，`std::jgir canonical=true` 之后数每个图的 `CALL` 节点：

```
figure1   std::cxx::inline            → graphs 4 calls [1,0,0,0]     opt::inline 逐字节相等 PASS
capture   std::cxx::inline            → graphs 3 calls [1,1,0]       opt::inline 逐字节相等 PASS
budget0   std::cxx::inline growth_budget=0 → [1,0,0,0]               opt::inline 相等 PASS
recursion std::cxx::inline            → 原样留着调用                 opt::inline **失败**：
   graph.inline_call: the call is of the graph being edited (a recursive call): expanding it would splic…
   （differential 的 exit_code 与 stdout_normalized 都比不上）
```

**原因**：`opt.cml` 的 `expandable` 用 `callee(n) != g` 判断「这个调用是不是落在它自己调的那张图里」，跨草稿/发布句柄比不相等；`name` / `creation_id` 都是 `spelling`（design principles 9.5：变换不许依赖），不能用。

**判 (a)：图模块加一个**新增读数**（只读、不建图、不改 `spliceCallInDraft`）：
```
/// @fails fault `InvalidArgument` when the node is not a call, or its callee is not known
export func callee_is_self(n: Node) => bool!
```
- 语义：`callee(n)` 指向的是不是**这个节点所在/正在编辑的那张图** —— 也就是 `inline_call` 拒绝时用的那个判断，只不过作为读数交出来；
- 位置：声明 `src/stdlib/graph.cml`（挨着 `inline_call`），注册 `src/stdlib/graph/module.cpp`（挨着 `inline_call` 的两处表），实现 `src/stdlib/graph/txn.cpp`（挨着 `spliceCallInDraft`，复用那个拒绝处现成的谓词）；
- 用法：`expandable` 里 `c != g` 换成 `not callee_is_self(n)`，然后**重跑递归语料**：`opt::inline` 与 `std::cxx::inline` 应当逐字节相等。

## 4. 「逐字节相等可能是两次没动」（单独一节，最值钱的一条）

同一批语料**不跑 pass** 的基线（`std::jgir canonical=true` 直接 dump）：

```
figure1   [1,0,0,0]   ← 与 std::cxx::inline 之后一模一样
capture   [1,1,0]     ← 一样
budget0   [1,0,0,0]   ← 一样
recursion [1,0,0]     ← 一样
```

⇒ 我第一版造的 4 条语料里，**C++ 侧一个调用都没内联**：前端已经把 `inc(x)` 这类单表达式小函数就地展开了。所以「两边逐字节相等」= **两个都没动**的相等，正是计划页第 3 节警告的坑。

**判 (a)：第三步的语料改用 `test/cases/opt/inline/`（19 条）里确有内联发生的 4 条**，按 `test/plans/feat/opt/inline.plan.toml` 已绿的条目挑；挑的时候**贴「内联前/后 CALL 节点数」证明动了**。Figure 1 那条可以留着作形状展示，但**不作等值证据**。计划 `desc` 里必须写一句：

> the corpus is chosen so that the pass moves the graph; equality of two unmoved graphs proves nothing

## 5. 剩余活清单（给干净的一格就能收尾）

1. **新读数** `callee_is_self`：`graph.cml` + `module.cpp` + `txn.cpp` 三处，然后 `expandable` 换判据，重跑递归语料（要看到两边相等）。
2. **4 条语料**：从 `test/cases/opt/inline/` 挑「确有内联」，按需复制成 `test/cases/opt/cxx_isolation/inline_*.cml`（或直接引用，注意计划 `case` 路径从 `test/plans/feat/opt/` 起是 `../../../cases/...`）。
3. **8 条计划条目**：`test/plans/feat/opt/cxx_isolation.plan.toml` 里加 `inline.<corpus>.by_cpp`（behavior + `verify_script` 数 CALL 节点、断言减少/不变）+ `inline.<corpus>.by_camel`（`kind = "differential"`、`compare_to` 指 `by_cpp`、`compare_fields = ["exit_code", "stdout_normalized"]`；本文件没有 `[defaults]`、不写 `normalizers`，所以 `stdout_normalized` **就是**逐字节）。
4. **模块头两处清单**：`src/stdlib/opt.cml` 文件头的 pass 清单与「两套实现比对」那段把 `inline` 加上（第二步那笔已加了清单那一半，比对那段要核）。计划 `tags` 加 `inline`。
5. 验收：`@example` 过 `check-doc-snippets.mjs`；对 promotion-26 新增红 0；`[provenance] tip` + `binary built`。

## 6. 操作备忘（踩过的坑）

- **`.cml` 改动也要 build**：`-NoBuild` 会以 `out/latest` 与树不一致拒绝跑，std 库是安装的一部分。
- 取失败原文：`-FetchFile 'fail/<plan>/<plan>.<test>/stderr.txt' -FromThisRun`（目录名带计划名前缀，我在这上面空跑过两次）。
- 探针计划放在 `test/plans/probe/`，跑完删掉；本轮探针与 4 条草稿语料已删，树是干净的。
- **红线**：不改 C++ 侧的选择规则、不改既有断言、不依赖 `name`/`creation_id`、不动主检出、不 push。