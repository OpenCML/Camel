# `task-379` 交接卡：一次调用只有一个候选集

枝：`lane/call-candidates`（从 `origin/integration = ad276f5467` 开），树：`D:\Projects\Camel-wt\parse-old`。
`git status --porcelain` 为空时开工。本卡只读结论已取齐，代码一步未动。

## ① 当前一步

**做到哪**：只读第一站走完，接口落点定了，**发现一处形状之前没算到**（见 ③ 的"候选不是同一类东西"），
其余三件（歧义、顺序、`takes` 等价）的读数都取齐了。下一步是**先定返回类型再动循环**，不是先改 `resolveGroupedCall` 的循环。

**下一步**（按顺序，一次一步，每步编译过再走下一步）：
1. 定 `CallCandidate` 的形状（`std::variant`：group 规则的 `oper_idx_ptr_t` 与内建声明的 `compile_graph_ptr_t`），
   加在 `src/compile/src/gir/builder.h` 里 `Builder` 的私有区；
2. 写 `callCandidatesOf(node_handle_t target, const oper_group_ptr_t &ops)`，表 = group `resolvers()`（注册序）
   + `operatorOverloads_[target]` 的 `graphs`（原序）+ `templates`（原序）；
3. 改 `resolveGroupedCall` 的返回类型与循环（4 个调用点：`visitLinkNode` 的正式决议、它下面两处重试、
   `builder_calls.cpp#operatorValueGraphOf` 那条路径）；
4. 把 `settleGenericOperatorArguments` 的 `accepted()` 改成问同一张表，**删 `takes`**；
5. 判据见 ③。

## ② 读数（本次会话实测，均来自 `ad276f5467` 的源码）

### 2.1 两处候选集查询点

- **正式决议只问 group**：`src/compile/src/gir/builder_operators.cpp#resolveGroupedCall` 第一层就是
  `for (i < ops->resolvers())` —— 候选只有 `oper_group_ptr_t` 自己的 resolver 表。
  调用点 `src/compile/src/gir/builder_call.cpp#visitLinkNode`（`auto res = resolveGroupedCall(ops, …)`）。
- **`with` 取型问合并集 + 自写挑选**：`src/compile/src/gir/builder_generics.cpp#settleGenericOperatorArguments`
  收 `const OperatorOverloads *overloads`，里面
  - `accepted`：`ops->resolve(…)` **或** `overloads->graphs/templates` 逐个 `takes(signature)`；
  - `takes` 自己用 `unifyTypeVars` + `writtenParameterTakes` 复写"这个签名吃不吃这些实参"；
  - `settleOverloaded` 自己遍历 `group->resolvers()` 并 `resolver->resolve({}, {first})`/`{first, second}` 试签名。
  调用点两处：`builder_call.cpp#visitLinkNode`（`settleGenericOperatorArguments(ops, setOf…, …)`）与
  `builder_call.cpp` 通用调用路径里的同符号调用 —— **两处都必须走同一张表**。
- 候选集来源：`builder.h#operatorOverloads_`（`unordered_map<node_handle_t, OperatorOverloads>`），
  写入在 `builder_expr.cpp`（名字解析合 `builtinBodies.graphs/templates`）、
  `builder_operators.cpp#addDeclaringModuleOverloads`（`operatorOverloadsOf(group)` + 声明模块导出）。

### 2.2 `takes` 的语义现在由谁覆盖（判 ③）

`src/kernel/core/type/resolver.cpp#DeclaredFuncTypeResolver::resolve`（无显式类型实参时 `resolveForCall` 直接转给它）：
```
① 参数个数必须相等（rest 参数例外）                    → 与 takes 的 withTypesCount()/normTypesCount() 相等判据同
② 逐个 unifyTypeVars(声明形参, 实参值类型)
③ 每个 bindable_ 变量必须被绑定
④ substituteTypeVars 出 concrete，再逐个
   fits(concrete 形参, 实参) = assignableFrom(...)（外加空数组字面量 elem=void 的例外）
```
`resolveForCall` 有显式类型实参时：按位置写进 `bindable_` → substitute → 再走上面同一段；
`normStatics` 被丢弃（`(void)normStatics`）。
⇒ **`written` 语义由 resolver 自己的 `assignableFrom(concrete 形参, 实参)` 覆盖**，
这段就是 `writtenParameterTakes` 想做的手工版；差别只剩"函数类型的形参"这一支
（`writtenParameterTakes` 对 `want` 是函数类型时按写下的参数逐个比，`assignableFrom` 比的是函数类型的可赋值关系）。
**这条差别必须在改完后用 `probe.foreach_lambda`/`probe.foreach_lambda_family` 与 5 条 `docs.snippets_compile` 用例验**，
不是靠推断收掉。

### 2.3 两类"歧义"不是同一件事（判 ①，别一起删）

| 处 | 现行判据 | 语汇 |
|---|---|---|
| `resolveGroupedCall` | **第一个吃下的赢**（group 注册序）；只有多条候选都写了 `when` 且都成立时才报歧义（`ambiguous`，排序去重后打印） | "先到先得 + `when` 并列才报" |
| `settleOverloaded` | `graphs.size() + operators.size() != 1` ⇒ **拒**（名字没有值，`rejectNameWithoutValue` 列出候选） | "必须恰好一条" |
⇒ 合成一张表**不会**让歧义判定消失：两处各保留自己的判据，只是**候选从同一张表来**。
`settleOverloaded` 的 `!= 1` 原样留（它判的是"一个参数名代表哪一条"，不是"算子选哪条"）。

### 2.4 候选顺序（判 ②，写成规则，不依赖 `unordered_map` 遍历）

```
1. group 的 resolver，按 ops->resolvers() 的注册序（现行为）
2. operatorOverloads_[target].graphs，按该 vector 的既有顺序
3. operatorOverloads_[target].templates，按该 vector 的既有顺序
```
理由：`graphs`/`templates` 由 `operatorOverloadsOf` 与 `addDeclaringModuleOverloads` 按导入序 append（有去重），
本身是稳定序；`operatorOverloads_` 的 `unordered_map` 只是**按节点索引**，不参与候选排序。
"改前后选中者一致"的读数：造一条 group 与内建都吃的调用（`Range` 上的 `foreach` 就是），
改前贴 `LAW_APPLIED`/选中的 uri，改后同一条。

### 2.5 之前没算到的一条（改变了改法）

`resolveGroupedCall` 返回 `std::optional<oper_idx_ptr_t>`（`OperatorIndex` = group 名 + 函数类型 + uri + oper），
而内建声明是一条 **graph**（`settleOverloaded` 里 `graphs.front()` 是 `compile_graph_ptr_t`）——
`operatorValueGraphOf`（`builder_operators.cpp#operatorValueGraphOf`）是用 `op->uri()` 造"调用这个算子"的函数值，
**造不出"调用一条声明"**。⇒ 候选不是同一类东西，**必须先定 `variant` 与返回类型**，
再动循环；否则会把内建声明硬塞进 `OperatorIndex`，变成又一份假身份。

## ③ 下一步怎么接（判据）

```
① 单一入口探针读数：`candidates=N sources=group+builtin` 一类原文（探针写在 test/plans/probe/ 下，随后删）
② probe.foreach_lambda / probe.foreach_lambda_family / feat/docs_links（docs.snippets_compile）仍绿
③ feat/compile、feat/modules 对 70 表 142 名（internal/reports/2026-11-28-promotion-70-reds.tsv）新增红 0，逐名比
④ 正式决议只发生一次选择：贴 `resolveGroupedCall` 与 `callCandidatesOf` 两处符号
⑤ numstat + [provenance] tip + binary built
⑥ （本卡新增）歧义判定不消失：造一条"两个入口都匹配"的调用，改前改后选中的同一条、
   歧义时报同一种错（贴两轮原文）
⑦ （本卡新增）顺序规则：见 2.4，且给"两边都能吃"的调用改前后选中者一致的读数
⑧ （本卡新增）`takes` 等价：见 2.2，函数类型形参那一支由 ② 的用例兜底，不许靠推断
```
构建/跑计划一律 `pwsh -NoProfile -File D:\Projects\Camel-wt\parse-old\internal\tools\camel-remote.ps1
-Path D:\Projects\Camel-wt\parse-old -Plan <计划> -Jobs 1 -NoGpuWatch`，输出重定向到 `D:\Temp\` 下落盘再看。

## ④ 边界

- 只改 `src/compile/src/gir/**`（`builder.h`、`builder_call.cpp`、`builder_operators.cpp`、`builder_generics.cpp`）；
  **不碰** `src/kernel/**`（`resolver.h`/`resolver.cpp` 只读：内建声明的匹配就用它）；
  **不碰** `docs/design-principles.cn.md`。
- `lane/deriv-tails @ 0070c131b0` 已在合并队列，**不许切走、不许 reset、不许删**。
- 工作树里另有别的车道：`D:\Projects\Camel`（主检出，负责人）不碰；`kinds-oper`/`op-names`/`value-ports`/`mm-capacity` 不碰。
- 一次一条 run；`exit 4` 先查是不是自己的旧 run 持锁；不许 `git stash`、不许 push。
- 这活最容易越做越大：**先定变体与返回类型、再改循环**；每一步 commit 一次，别攒到最后。
    改 `resolveGroupedCall` 的返回类型会牵到 4 个调用点（`visitLinkNode` 的正式决议、其下两处重试、
  `builder_calls.cpp` 里 `operatorValueGraphOf(chosen, …)` 那条），一处不改就是两条路两个答案。