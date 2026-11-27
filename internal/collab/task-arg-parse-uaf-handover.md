# 交接卡：arg-parse-uaf-w（`lane/arg-parse-uaf-w` / `witness` 树）

写于 2026-11-28 03:44（Asia/Shanghai）。当前一格上下文已尽，按规矩先写卡、不动手。

## 一、当前一步

```
工作树 : D:\Projects\Camel-wt\witness
枝     : lane/arg-parse-uaf-w
HEAD   : e7928eaa41  (origin/integration 曾为 6939a34b51；请以当场 fetch 为准)
status : 空
```

最近一次 git log -1（原文摘录）：

```
commit e7928eaa417a26433376691f7eab6512e575dc4f
Date:   Sat Nov 28 02:49:45 2026 +0800

    The refusal for a member read whose position is not known when the function is compiled
    is StaticAccessNotConstant, pinned where it is spoken: at the access, in its own words.
```

那一笔是 `item-13b`（非典常量位置）的用例提交，合并在队列里；它不是本枝要修的 UAF 那两笔，也不是 `task-301`
的 transforms 那笔 —— 开工前请先 `git log --oneline -10` 看清楚当前枝上到底有哪些提交。

## 二、读数

合并位在**载体枝**上量的（不是本枝直接量的）：

```
枝: wip/arg-parse-uaf-w-2 @ 58cf038c74   （--onto cd4c77534c，零冲突）
计划: feat/compile (fast) + t301_tangent
读数: 2729 / 2702 / 24    ⇒ 甲 11 名（全不在 26 表）
   cast_target_type_expression.nvm
   type_expression_positions
   literal_head_type_expression
   literal_head_type_variable
   typeof.derived_type_argument
   type_packs.function_pack_params.nvm
   type_packs.function_pack_params.fvm
   type_packs.tangent_space.nvm
   type_packs.tangent_space.fvm
   rulesets.jvp_lift.nvm
   rulesets.jvp_lift.fvm
另有:
   -Check check-doc-links 红：t301_tangent.plan.toml:27 写了 `typecall.cpp:178` 行号，
      应写成 `...#Symbol`（行号会漂；今晚第三次撞）
   ast 8 stale
```

本枝自己实测过的旧读数（供对照，**勿直接当新证据**）：

```
Release, fault_deep.prl_off 10 连跑: 7/10 崩（70%）
  [SUMMARY] total: 25, pass: 24, fail: 1, killed by a signal: 1
  SIGABRT 134，Camel Invariant Violation:
    frame.h(71) Frame::codeAt(data_idx_t)  Guarantee index != 0  "Data index is invalid."
Debug 栈: 未取得（工具缺陷：远端 run 目录不存 *.out；本地包装被回收即丢输出）
```

## 三、下一步怎么接

1. **先 fetch 再换底**：`git fetch origin` → `git rev-list --count HEAD..origin/integration` → `git rebase origin/integration`。
   甲 11 名的形状（类型表达式 / `type_packs` / `tangent_space` / `jvp_lift`）指向 `typecall.cpp` 的大改，
   而这处**有三方在动**：
   - 本枝：`typecall.cpp` `+14/−28`
   - `cap-probe`：`task-371`（离散切空间），改 `tangent_space.cpp:107-111` 与 `typecall.cpp:48-56, 80`
   - 尖端：已合 `item-13b` 族
   ⇒ **换底之后，类型表达式那一块要重做，不要在旧解法上修补甲。**
2. 重做完成后再跑：`feat/compile` fast + `t301_tangent`，甲必须为 ∅；同时 `-Check check-doc-links` 要绿。
3. AST 等重做之后**由接手人自己录**（5 个新 case + 3 个改过的 case）。**不要**在旧解上重录。

## 四、边界

```
不推枝；不再开新枝（分支闸已紧）。
不在别人的位置上修甲：甲≠∅ 就退回，不硬修。
不动规则里的 `const`（那是另一个任务 lane/no-const-with）。
文档引用一律写 `...#Symbol`，不写行号。
`internal/review`/`internal/plans` 的过程痕迹不进代码注释。
```

## 附：本次会话中与本枝相关的两条已验证事实（可复核）

```
① clipp UAF（task-273）已修：run 路径的 grammar 具名持有，提交在 arg-parse-uaf 线上；与本次甲红无关。
② fault_deep.prl_off 的 SIGABRT 是独立缺陷（非 UAF 家族）：
   执行路径 `frame.h:696`（ArgsView::code）与 `:747-748` 把 `indices_[index]==0`（"没有槽"）交给 codeAt；
   打印路径 `:183/:213` 不是崩点。最小复现：
   camel test/cases/para/handoff/fault_deep.cml "std::prl bytecode=false"
```