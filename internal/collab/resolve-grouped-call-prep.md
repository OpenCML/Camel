# 只读准备：`resolveGroupedCall` 的候选集合一

**性质**：静态准备，**无计划读数**（按 `D-1128-98`，本页**不写"甲 0"**）。未建枝、未改 `src/`、未 build；所有代码原句用 `git show <rev>:<path>` 读出。
**基**：`origin/integration = 1622aebe0b`（`backlog: the tangent type was moved on purpose, and the law it was moved to is not written yet`）。卡面页 `internal/collab/task-resolve-grouped-call.md` 写的是 `c8c740774b`，**已过期**。
**已进 integration 的前提**：`task-379` 的三笔已合（`feba3437a9 Merge lane/call-candidates into integration`，含 `78fb4f4045` 与 `1dcabf9cde`）——所以 `callCandidateSignatures` 在 `builder.h:3766`、`builder_call.cpp:99`、`builder_call.cpp:744` 三处都在。

---

## 1. 卡面落点逐条核对（对的打勾，错的指出）

| # | 卡面写的 | 我在 `1622aebe0b` 上核到的 | 判 |
|---|---|---|---|
| 1 | `Builder::resolveGroupedCall` 只遍历 `ops->resolvers()`，内建 `graphs`/`templates` 一条也不进 | `builder_operators.cpp:1406`：`for (size_t i = 0; i < ops->resolvers().size(); ++i) {`；函数体内**没有** `operatorOverloads_` / `graphs` / `templates` 的任何读 | ✅ |
| 2 | 返回类型是 `std::optional<oper_idx_ptr_t>`，表达不了"选中的是内建声明" | 定义 `builder_operators.cpp:1396`：`std::optional<oper_idx_ptr_t> Builder::resolveGroupedCall(`；声明 `builder.h:2137` 同形；唯一写入点 `builder_operators.cpp:1463`：`chosen = std::make_shared<OperatorIndex>(ops->name(), *result, uri, ops->operAt(i));` | ✅ |
| 3 | "第一个吃下的赢；两条以上写 `when` 且条件成立才报歧义" | `builder_operators.cpp:1462-1464`（`if (!chosen) { … }`）+ `:1472-1479`（`holding.size() > 1` → `sort` → `unique` → `*ambiguous = std::move(holding)`，注释原句 "that order is exactly what must not decide"） | ✅ |
| 4 | 落点 1：`visitLinkNode` 主路 | `builder_call.cpp:1873`：`auto res = resolveGroupedCall(`（传 `withInputs` 指针，收 `unmetConditions`/`&ambiguousConditions`） | ✅ |
| 5 | 落点 2：`with` 适配后的重试，答案失败就丢 | `builder_call.cpp:1985`：`auto retried = resolveGroupedCall(`；自己的 `retriedUnmet`（`:1984`）在失败时丢弃，注释 `:1980-1983` "its answer is dropped when it fails" | ✅ |
| 6 | 落点 3：entrance/change 扫描，`normInputTypes` 临时换成 `enteredTypes` | `builder_call.cpp:2015` `normInputTypes = enteredTypes;` → `:2018` `auto answered = resolveGroupedCall(`；失败回滚 `:2030` `normInputTypes = std::move(held);` | ✅ |
| 7 | 落点 4：`Builder::overloadValueFor` "按期望函数类型挑一条，**自己走一遍 `ops->resolvers()`**" | `builder_operators.cpp:1290` 定义。**但它有两支**：`:1299-1329` `std::holds_alternative<graph_vec_ptr_t>(target)` —— 已经在遍历**内建 `graphs`**（`for (const auto &g : *graphs)`，返回 `createFuncDataNode(chosen, …)`）；`:1331` 起才是 `oper_group_ptr_t` 支，走 `ops->resolvers()`（`:1338`）。 | ⚠️ **不准**：它不是"只读 group 的候选"，而是**两种答案各一支** |
| 8 | 另：拿到答案后按 `(*res)->uri()` 回 `ops->resolvers()` 再找一遍那条规则，"**这两处**" | 全文件 `(*res)` 只出现在 `builder_call.cpp:1891` 与 `:1898`，**都在同一个循环**（`:1887 for (const auto &[ruleUri, rule] : ops->resolvers())`）里；这件事是这个循环里的**两件事**：补 `with` 默认值（`:1896 completeWithFor(...)`）与 `constas` 检查（`:1909-1912`）。**没有第二处循环**。 | ⚠️ **"两处"数错了**：是**一处循环**（内含两件事） |
| 9 | `selectOverloadFor` 今天 0 处 | `git grep -c selectOverloadFor origin/integration -- src` → **0** | ✅ |
| 10 | `writtenParameterTakes` 今天 6 处（`builder_generics.cpp`） | `git grep -c writtenParameterTakes origin/integration -- src` → **0** | ❌ **过期**：`task-379` 已合，当前尖是 **0**，不是 6 |
| 11 | 不动 `settleOverloaded` 的 `graphs.size() + operators.size() != 1` | `builder_generics.cpp:2138` `const auto settleOverloaded = [&](Argument &arg, Type *&type) {`；`:2195` `if (graphs.size() + operators.size() != 1) { return; // stays a name… }` | ✅（形态未变） |

**卡面要改的四处**：第 5 行的基（`c8c740774b` → 现尖 `1622aebe0b`，且 `task-379` 已进 integration，"在那一笔之上做"改成"在 integration 尖端上做"）；第 17 行"两处"→ 一处循环；第 16 行落点 4 的描述（两支并存）；第 18 行与判据 ④ 的 `0 / 6` → `0 / 0`。

---

## 2. 两种答案的类型（`file:line` + 原句）

**group 规则的答案 `oper_idx_ptr_t`**
- `src/kernel/include/camel/core/operator.h:80`：`using oper_idx_ptr_t     = std::shared_ptr<OperatorIndex>;`
- `src/kernel/include/camel/core/operator.h:84-107` `class OperatorIndex`：成员 `name_` / `type_` / `uri_` / `oper_`；`uri()` 在 `:98`；
  `:100-103` 原句：**"The operator's identity in the operator table: the id of the entry `uri` names, recorded when the rule that resolved to this declaration was registered (`OperatorGroup`). It is not a lookup"**。

**内建声明的答案 `compile_graph_ptr_t`**
- `src/compile/src/gir/builder_types.h:54`：`using compile_graph_ptr_t   = std::shared_ptr<DraftGraphBuilder>;`（`builder_support.h:29` 同一行重复声明）。
- 一个内建声明在 `operatorOverloads_` 里就是以这个类型存放（`graphs` 是 `std::vector<compile_graph_ptr_t>`）。

**`operatorValueGraphOf` 造的是"算子当值用"，不是"调用一条声明"**
- 定义 `src/compile/src/gir/builder_operators.cpp:1077`：`compile_graph_ptr_t Builder::operatorValueGraphOf(const oper_idx_ptr_t &op, const GCT::node_ptr_t &origin) {`
- `:1092` 原句：**"One per place that names the operator: the function is built where it is used."**
- `:1131` 原句（决定性的那句）：`node_handle_t call       = graph->addOperNode(op, 0);` —— 这一节节点调的是**算子**（`op` 的 uri / oper id），不是某条声明图。
- `:1143`：`setGraphReturnFromResult(graph, nodeIdOf(call), nodeIdOf(call), opType->exitType());`

**结论（可复核的形状）**：`resolveGroupedCall` 的答案类型只能指向 group 的规则，所以它连"选中了内建声明"都表达不出来；而另一侧今天**没有**"调用一条声明"的构造器（`operatorValueGraphOf` 造的是另一个东西）。⇒ 返回类型必须动，且这是**唯一**必须动类型的地方；4 个落点是它的下游。

---

## 3. 判据草案

### 3.1 两条 `std::gir` 阳性对照（"两个来源都得进同一张表"的最小读数）

在宿主 `/home/zjwei/camel-work/parse-old`，`export CAMEL_HOME=$(pwd)/out/latest`：

```
./out/latest/bin/camel test/cases/probe/foreach_lambda/range_form.cml std::gir std::nop
./out/latest/bin/camel test/cases/probe/foreach_lambda/array_form.cml std::gir std::nop
```

期望（**已实测**，binary built `2026-11-28T11:42:44+08:00`，tip `ea8c75725d`，即 `task-379` 已合之后）：

```
range_form  EXIT=0   dot 里 label="prelude::foreach" 出现 2 次（调用节点 + 其体内再调 :mark/foreach_arr），label=":mark/foreach_arr" 出现 1 次
array_form  EXIT=0   dot 里只有 label=":mark/foreach_arr" 1 次，没有 prelude::foreach
```

取命令：`grep -o 'label="[^"]*foreach[^"]*"' <dot> | sort | uniq -c`。
**改坏的第一个信号**：`range_form` 丢了 `prelude::foreach`（内建声明不再被选中），或 `array_form` 多出 `prelude::foreach`（内建声明抢了 group 规则的位）。

### 3.2 两个符号的处数（当前尖 `1622aebe0b`，现核）

```
git grep -c selectOverloadFor        origin/integration -- src   → 0 处
git grep -c writtenParameterTakes    origin/integration -- src   → 0 处
git grep -c callCandidateSignatures  origin/integration -- src   → builder.h:1, builder_call.cpp:2（3 处）
```
这两个 0 是 `task-379` 的结果（已合），**不是本卡的战果**；本卡若让它们变非 0，说明基或改法错了。

### 3.3 顺序与"改前改后选中同一条"

沿用 `builder_call.cpp#callCandidateSignatures` 开头的注释：**group 注册序 → 内建 `graphs` 原序 → `templates` 原序**；`operatorOverloads_` 是 `unordered_map`、只按调用节点索引、不参与排序。3.1 的两条命令就是"改前改后选中同一条"的对照：同一个 `range_form` 改前改后都必须选中 `prelude::foreach`。

---

## 4. 最小改动面 / 要不要动 `settleOverloaded`

**最小改动面（三处，按建议顺序）**
1. **先把 `overloadValueFor` 合成一处**（`builder_operators.cpp:1290`）：它今天已经两支并存（`:1299` 读内建 graphs、`:1331` 读 group resolvers），把两支的"挑一条"收敛到与 `callCandidateSignatures` 同一张表，但它**返回 `node_handle_t` 的出口不变** ⇒ 不动任何调用点（`builder_call.cpp:945`、`builder_expr.cpp:1626`）。这是唯一能"只改内部"的一步。
2. **再动 `resolveGroupedCall` 的返回类型**（`builder.h:2137` + `builder_operators.cpp:1396`）：让答案能装两种东西（新的小结构或 `std::variant<oper_idx_ptr_t, compile_graph_ptr_t>`），并把"调用一条声明"的构造补上（今天缺的就是这个）。
3. **三个调用点各自取用**：`:1873` 主路还要走 `(*res)->uri()` 回查（`:1887-1912`，补默认值 + constas）——内建声明那条路没有 uri 回查，要按"命中内建声明时跳过这段"落；`:1985` 与 `:2018` 只关心"有没有答案"与替换输入 ⇒ 改动最小。

**要不要动 `settleOverloaded`**：**不要**。它判的是"恰好一条"（`builder_generics.cpp:2195`），与本卡的"候选集合一 / 谁赢"是两件事；它今天也已分两支各数一遍（`:2152` graph_vec / `:2160` group），语义不变，保持原样。

---

## 5. 一句话

本卡的**唯一类型改动**在 `resolveGroupedCall` 的返回类型；`overloadValueFor` 是最便宜的起点（已两支并存、出口不变）；`settleOverloaded` 与"第一个吃下的赢"两处判定**都不动**。