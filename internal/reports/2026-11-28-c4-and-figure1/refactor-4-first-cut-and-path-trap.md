# 重构 4 的第一刀，以及"按枝判"的三个坑

本页为 `D-1128-238①`（决策者裁定）与 `D-1128-234` 的落笔，供下一个人接手时先读。

## 0 裁定：重构 4 的表按这两行

| 刀 | 提交 | 状态 | 内容 |
|---|---|---|---|
| 重构 4 第一刀 | `f229687410`（2026-11-28 19:28:53） | **已落** | 推导式降低不再造 `$elementN`；挡着 `map` 改成收集循环 |
| 重构 4 第二刀 | **未开始** | — | `map` 节点在**字节码生成那一处**降成循环（`fvm`/`jit` 共用），不是改 prelude |

**第二刀按 `D-1128-245` 更正（"prelude 的 `map` 改成收集循环"这句作废）**：把 prelude 的 `map` 改写成 `out = [] … out->append<f(x)>` 会让**每个元素都依赖上一轮的 `out`**，图上元素之间不再独立；而原则 **3.6** 定了 `map` 要求元素函数**纯** ⇒ 元素互不影响、**并行调度器可以并发执行**。改写成收集循环等于**在图上丢掉这条性质**。正确的第二刀三条：

1 **`map` 在图上仍是声明"元素互不依赖"的高阶算子**，prelude 的 `map` 定义**一行都不改**；
2 改的是**字节码生成那一处（`fvm` 与 `jit` 共用）**：`map` 节点降成一个**字节码循环**，元素函数**能内联就内联**，`fvm` 解释这个循环、`jit` 编这个循环 ⇒ **一个机制，两个后端用**；
3 `prl` 侧顺带：`taskflow.cpp` 的 `mark_map_arr` 今天**按算子名**认 `map`，按 3.6「并行来自声明的性质，不来自名字」改成**问元素函数是否纯、元素是否互不依赖**。

`task-473` 已按新形状重写，判据含 prl 正反例两条：名字叫 `map` 但元素函数不纯 ⇒ 不再判可并发；纯元素函数但名字不叫 `map` ⇒ 仍判可并发。**第一刀 `f229687410` 照旧有效，不随此更正改动。**

`f229687410` 的 `numstat`：`10/6 src/compile/src/gct/builder_data.cpp`、`42/1 src/compile/src/gir/builder_call.cpp`、`2/2 test/plans/feat/consistency/comprehension.plan.toml`。`is-ancestor f229687410 origin/integration = 1`（未进 integration）；`is-ancestor f229687410 lane/map-as-loop = 0`（是该枝 tip 的祖先）。

`D-1128-238①` 的口径：它**既是 5.9 调用定型的实现，也是重构 4 的起点**——两步不是互斥的归属。不要写成"前提/实现"二分（曾经写过，已作废）。

## 1 三个坑（今晚各踩一次）

### 坑一：按枝名判归属
**同一个枝上的提交不一定属于这条车道对外的名字。** `lane/map-as-loop` 的 tip 下躺着 `f229687410`，它看着像"map 是循环"的第一步，实际由 `D-1128-238①` 归到重构 4 第一刀、同时也是 5.9 的实现。**判归属按内容（diff）判，不按枝名判**；初判"属调用定型那一族、不归重构 4"已作废。

### 坑二：扫"未合区间"找首笔
`git log origin/integration..<branch>` 只看**尚未合入**的提交：首笔一旦被合进 integration，它就从区间里消失，剩下的全是报告笔，于是被读成"无代码提交"。
**规则：每件的首笔用显式 sha + `git merge-base --is-ancestor <sha> origin/integration` 核，退出码 0 = 已在 integration。** 不靠区间扫描。

### 坑三：拿 A/C 当 B 的对照
缺陷 ② (`[] as U[]`) 的两个控制组不是同一条路：
```
CASE=A rc=0 LOGLINES=0     CASE=C rc=0 LOGLINES=0
CASE=B rc=1 LOGLINES=1
  [CAST] origin=8294 inferred=0 hasVar=1 boundHasVar=0 tagged=0 collection=0 type=U[]
```
A/C 的 `LOGLINES=0` 说明**具体类型的 `[] as int[]` 不经 gir 的 `visitCastNode`**（GCT 侧已化成常量）。所以 A 过了不能当"B 改好了"的证据。根在 `builder_expr.cpp` 类型变量分支 `:1596` 的 `rejectLiteralData`（`data->convertTo` 对空字面量失败），而 `Builder::coerceTo`（`builder_literal.cpp:700–705`）有 `[]` 的专用构造 ⇒ 入口用错转换，不是缺能力。

## 2 `isElementFunction` 那一支留

决策者：它只换诊断、不改定型。**但 `task-465` 合并时三段必须一起收进那一个解析器**：`usableExpected`（`builder_call.cpp:697–716`）、`withParameterOfSelectedEntry`（`:741–783`）、`f229687410` 新增的 `:809` 段（`visitLinkNode` 同一函数内）。不收就是第三个入口。

## 3 断言改动：方向可判、读数未取

`comprehension.plan.toml` 两行：
```
-  expect_output_contains = ["name=SemanticDiag::GenericFunctionAsValue", "it returns 'int', and the function expected here is '(item: int) => bool'"]
+  expect_output_contains = ["name=SemanticDiag::ReturnTypeMismatch", "err_condition_type.cml:2:39", "Return type 'int' does not match the function's declared return type 'bool'"]
```
旧断言钉的是"借来 `GenericFunctionAsValue` 的形状说同一事实"，新断言钉"`ReturnTypeMismatch` + 精确位置 + 两类型"，**是收紧**，判"随语义改判"。

**仍缺读数**：该用例改前/改后**各一行 stdout**（`GenericFunctionAsValue` / `ReturnTypeMismatch` 原文）。取到才升为"已验"，在此之前只记"方向可判、读数未取"。