# 开放长度数组的零没有宽度：是**拒绝**，还是**把前向实参交进来**？

## 一、要问的那一句

`internal/plans/2026-11-18-nested-transforms.cn.md` 补充二第 1 条（`ee36e647d`，17:50）：

> 「**转置的零值**：`transpose.cpp` 的 `zeroOfType`（约 1666 行）对长度开放的数组用空数组作零，不满足伴随等式（反例：长度开放的数组上 `(t1, t2) ↦ t1` 的转置，一个没有见证的零不可能同时对长度 2 和 3 都对）。改为以前向的实参作见证造零（`zeros_like`），与 `zeroOfSpaceOf` 同一条路，删掉按类型造空数组的那一支；补这个反例为用例。」

**照这一条做，只需要删一支**：`zeroOfType` 里 `!exact` 那一段（开放长度 ⇒ `extents.assign(..., 0)` ⇒ 空数组）。删掉之后它 `return rt::kInvalidNodeRef`，调用方 `zeroOf` 拒绝。我按这个做了，`BUILD_EXIT=0`。

## 二、它反掉的是**一条被用例钉住的定律**，不是一处 bug

`compile` 立刻从 `2630/2613/0/17` 变成 `2630/2607/**6**/17`，六条全是钉旧定律的：

| 用例 | 条目 |
| --- | --- |
| `vjp_const_function_answers_zero` | nvm / fvm / jit / prl 四条 |
| `vjp_const_function_open_rank_zero_is_refused_by_a_real_array` | nvm |
| `transpose_unread_array_parameter_stops_earlier` | nvm |

而这两条 negative 用例**是用散文为旧定律辩护**的，不是顺手写的：

- `vjp_const_function_open_rank_zero_is_refused_by_a_real_array` 的 `desc` 逐字：「The zero of a `float[...]` parameter is an array of no elements: `shape(dx)= [0]`, `sum(dx)= 0.0` … **an answer whose width the type did not state makes the consumer say so; it does not quietly become 0 or broadcast**」，断言 `dx= []`、`shape(dx)= [0]`、`sum(dx)= 0.0`。
- `transpose_unread_array_parameter_stops_earlier.cml` 的正文说：「Whether an empty array is the right zero for a length nobody stated is item 2 of `…2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal.cn.md`；this case pins what the tree does」，并点名那条 2026-11-04 的评审页还标着「undecided」。
- `vjp_const_function_answers_zero.cml` 的正文**自己预告了今天**：「`float[...]` … The zero the type states has no elements: `[]`. **The line is here so that the day a member is handed the value, the answer reads `[0.0, 0.0, 0.0]` and this line says so**」。

⇒ 三处都说明：旧定律是**被有意选定并写下来的**，`ee36e647d` 要换的是它。**换一条被有意选定的定律，要你明说「旧理由撤回」** —— 这一页就是来要这一句的。

## 三、还有一条读数：拒绝这条路今天会不会把图造坏

改完之后失败**不是干净的拒绝**，`vjp_const_function_answers_zero.nvm` 这一条上出现了运行时内部错误：

```
camel: internal error: Runtime graph 'main' breaks the branch-arm invariant: OPER 26 (:io/println)
reads JOIN 12, which lies in an arm of BRCH 24 that may not be taken when OPER 26 (:io/println) runs
```

**这条读数的归属我没有钉死**（同一份日志里还有一条 `var` 计划的 `10 + 1 + 10 + 1` 校验行，我没能把两行分到确定的条目上），所以它现在是**线索，不是结论**。但它指向的位置是确定的：

- 抛错处：`src/runtime/branch_arms.cpp:389-397`（「A node reads only nodes that run whenever it runs」那一循环的最后一个 `fail`），由 `validateRuntimeGraphPayload`（`src/runtime/graph.cpp:2914`）在**发布运行时图**时调用 ⇒ **不在变换里，在变换交出图之后**。
- 那条用例的源码**没有任何分支**（`const_open(x: float[...])` 只有 `let _ = x` 和 `return 1.0`）⇒ `main` 里的 `BRCH 24` 是**导数流水线自己造的**，不是用例写的。
- 构造侧**今天没有读到臂外节点的判据**：`builder_expr.cpp#recordWrite` 里，`e111ae5e4`（`ordfix`）加的那一条是 `if (readRegion != exitRegion && !synced_) { pending.push_back(use); … }`（约 673 行），它管的是**顺序边**（读在外围区、写落在臂里 ⇒ 读保持待定，由区域自己的出口经 JOIN 的控制输入排序）；`regionExit` 那一条管的是反方向。**而「一个节点读臂里的另一个节点的数据」这一侧，构造期没有任何条件**，只有这里这个发布期检查。

⇒ 所以**二选一**要你定：**(i)** 拒绝路径把图造坏了（改拒绝路径：拒绝时不许发布半成品）；**(ii)** 图本来就这样、拒绝只是把它暴露出来（改构造：数据读也要有 `recordWrite` 那样的条件）。**我倾向 (ii)**，因为那条用例的 `BRCH` 是流水线造的、拒绝本身不造节点；但这条只有一次 `std::gir` 对照（同一用例、改前改后各打一份图，看 `BRCH 24` 的两个臂与 `JOIN 12` 的位置）才能定，我没跑。

## 四、默认建议（写死，好让你只回一个字）

**默认：按 `ee36e647d` 执行 —— 开放长度上「无见证的零」= 拒绝。**

理由：伴随等式是**可判定**的那一侧（一个空数组对长度 2 与长度 3 都不对，这不是缺功能而是答错），而「把前向实参交进来」是**能力**那一侧（`Transpose<F>` 的类型律还得知道该把哪个实参交进来）。先拒绝、再补见证，两步都可测；反过来先补见证，拒绝这条路就永远没人测。

**配套要你点头的两件事**：

1. **改六条既有断言**（`vjp_const_function_answers_zero` 四条的 `open pullback(1.0)= ([])`、`vjp_const_function_open_rank_zero_is_refused_by_a_real_array` 整条 negative、`transpose_unread_array_parameter_stops_earlier` 的 `([], 2.0)`），连同它们的 `desc` 与正文里为旧定律辩护的那几段。**这六条是「本提交要改的断言」，不是被我改掉的断言** —— 但按红线，我要你一句话。
2. **2026-11-04 那条评审页**（`is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal`）要从 `undecided` 改成「已定：拒绝」，否则三条用例的正文都还指着一个「未定」。
## 决策者答复（2026-11-18）

**旧理由撤回，但不是拒绝：零按前向实参的形状造。** 默认的"拒绝"不采纳。
- 撤回的是 11-04 选定的"开放长度的零是空数组"：它答错了（伴随等式不成立，一个空数组对长度 2 与 3 都不对），这一句我明说撤回。
- "把前向实参交进来"不是新能力：按 11-18 定的 GNC 形状，前向交出 `(y, pb)`，`pb` 是捕获残差的闭包。转置需要某个输入形状的零、而它的余切没人读时，前向就把这个输入的**形状**（或它本身）作为一个残差捕获进 `pb`，转置里用 `zeros_like` 造零。`Transpose<F>` 的类型律不用改：残差不出现在类型里。ECOOP 一侧的定义也是"用前向的实参作见证造零"。
- 用例改法：`vjp_const_function_answers_zero` 四条改为 `pullback(1.0)` 得到与 `x` 同形状的零（长度 3 的 `x` 得 `[0.0, 0.0, 0.0]`）；`vjp_const_function_open_rank_zero_is_refused_by_a_real_array` 这条 negative 删除（它为旧定律辩护）；`transpose_unread_array_parameter_stops_earlier` 改为钉住"零的宽度来自前向实参"。`2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal` 那页改为"已定：零按前向实参的形状造，空数组撤回（2026-11-18）"。

**第三节的内部错误单独查，不论 (i) 还是 (ii) 都是缺陷**：一次拒绝不许发布半成品——变换失败时整份草稿放弃，什么都不提交（编辑的"全成或全不成"）；若查出图在拒绝之前就违反了臂的不变式（数据读没进 `recordWrite` 那条判据），那是构造缺陷，按第 8 条规矩在构造处修。归属钉死后在合并说明里写明是哪一个。
