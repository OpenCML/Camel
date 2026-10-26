# 事实／规则的形参"`v: T`"到底是值还是"产出 `T` 的节点"？（丙 落地时撞到的硬阻塞）

**问谁**：语言作者（负责人）
**问什么**：`fact`／规则的类型实参按您的裁定（**丙**）从**声明的形参类型**做常规 5.9 推理之后，**声明的值类型就是参数端口的类型**，于是把**节点**连进 `int` 端口会**破坏运行期边不变量**：`internal error: Runtime graph 'range__case1_fact' breaks its edge invariants (4 violation(s))`。要"保留声明类型"与"端口是节点"**兼得**，形参必须写成"**某个类型的节点**"这一类形态——您把这一类列为**未决**，所以我们停下问您，不自己定。

## 量到的事实（逐字）

- **语言参考自己说** `v: T` 是节点（`docs/language/09_rules_and_facts.md:300`）：`v: T` 意思是**产出类型 `T` 的那个节点**；"任何拿到 `Node` 的函数"都能查。**同一处还写着**："今天把值传给查询得到的是 `NoMatchingFunction`"——这句在本批之后**不再成立**（事实位置现在一律放行）。
- **我们的落地**（丙）：`src/parse/ast/builder.cpp` 的查询形参不再抹成 `Node`，改成保留**声明类型**（有参数类型就用它，没有才退回 `Node`）；条件函数非 const 形参同改。
- **硬阻塞（两处内部错误，逐字）**：
 - `docs/language/09_rules_and_facts.md:202` → `camel: internal error: Runtime graph 'range__case1_fact' breaks its edge invariants (4 violation(s))`
 - 同文件 `:247` → `… 'mul_by_one__case0_fact' breaks its edge invariants (1 violation(s))`
 - 英文版 `:132` → `terminate called after throwing an instance of 'std::runtime_error'`
 根因一句话：**声明的值类型就是参数端口类型**，节点连进值类型端口必破坏边不变量。
- **已顺带修掉的**：`src/compile/gir/builder_calls.cpp:439` 那条 `ASSERT(entered != nullptr, "An argument the binding accepted is converted.")` 会**崩**（曾让 140 个无关用例挂掉）——已改成**语言拒绝**（`ArgumentsMismatch` ＋ 说明"实参类型 vs 被调类型"）。**崩溃本身是缺陷**，这条改动无论您怎么裁都该留。
- **13 处（8 处文档片段＋3 处 `with` 事实用例＋教程 `17_fact` 四后端）逐处判定**：**无需**写 `::<T>`、**也无需**拒绝——按 `09_rules_and_facts.md:300` 的定义，它们的实参本就是**节点**或与声明同型；它们红的原因**不是类型检查**，而是上面的端口真相。
- **验收现状**：正向用例 `rules.fact_reads_a_fact_at_the_type_argument_it_is_given` **红**（`expected exit 0, got 1`）；**反向钉子仍绿**（`GenericArgumentsMismatch` ＋ `cannot be determined from the arguments or expected result` ＋ 提示写 `literal_of::<…>`）。另有 **32 条红**同根因两簇：条件里查事实（`rules.when_*`／`explain_*`／`why_not_*`／`gc.*condition*`）与事实主体里查事实（`fact_lib`／`fact_parts`／`parts_query_*`）＋`docs.snippets_compile`。
- **stdlib 侧**：`stdlib` **没有任何 `fact` 声明**（所以这批不牵动库）。

## 我们的候选（请选一个，或给别的划法）

- **甲（把"节点"落到类型上）**：给"`T` 的节点"一个真正的类型实例——`HandleType::cloneWithParams` **已经存在**，让事实／规则形参位置上的 `v: T` 解析成"产出 `T` 的节点"，端口类型与声明类型就一致了；5.9 推理仍按**声明的类型实参**做（丙不变）。
- **乙（端口可收节点，声明类型只用于推理）**：明写"事实／规则形参的端口取节点（`Node` 一族），声明的值类型**只用于推断类型实参**"，类型检查按声明类型判、端口按节点接。**代价**：类型与端口两套说法，容易再长出口径分歧——您上次正是为这个判的丙，所以我们把它列出来但不敢自己选。
- **丙（回退到抹成 `Node`）**：就是我们落地前的样子。**这会让您 2026-10-24 那条裁定（去掉 `queryParams` 的 `Node`）作废**，我们不建议。
- **丁（别的）**：例如"事实查询只接受**具名节点**或显式 `::<T>`，其余形态给具名诊断"，或"节点形态先不做、把 `fact depth::<T>` 只留在类型实参侧"。

## 现状（不自行决定）

- 这一批**停在 `wip/h602-context-keywords` 的 `fc2293154`**（工作树干净、**未合并、未推送**）；手交分支仍停在**绿**的 `1bfba0099`。**在您裁定之前我们不会合并**——它现在有 32 条红，而且"保留声明类型"与端口真相不可兼得。
- **已合进 develop 的部分**（绿）：`__fact` 的型参名进实参、GIR 的 maker、重写模块按 `name<类型实参, with 实参>` 组键、`Type::determinesTypeParameters`、「句柄不决定类型形参」这条反向钉子、AST 快照重录。
- 另外两条通用诊断的调用点计数（供您参考）：`ConstructNotSupported` 剩 2 处（多元下标那条另一车道正在删；fact 的 6 个 `giveUp` 调用点——那 6 处各自都该是**说出规则本身**的具名诊断，其中两处（空 `case`、类型数 ≠ 1）是解析器已报错后才到、不能裸删），`FeatureNotSupported` 剩 1 处（`yield`，生成器那件事我已另开一页问您）。