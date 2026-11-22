# m5：`reinterp<vmap>` 的消费者要复用 `jvp` 的哪一段

**状态：只读笔记。本轮断网（构建机不通），未编译、未提交。车道 `lane-nestedlit`，2026-11-01 中午。**

## 一句话

`jvp.cpp` 里"读一条规则、用规则右边建节点"是 **9 步**，其中**第 1 到第 6 步 `vmap` 原样复用**（问索引、实例化、拿到实例的图、建函数值节点），**第 7 到第 9 步不同**：`jvp` 建两个调用节点（先原语实参、后切线），`vmap` 建一个（批实参）。

## 一、`jvp.cpp` 第 50-129 行逐行说明

函数 `buildRuleTangent(context, transaction, uri, values, tangents, ruleName, why)`，返回切线节点或 `kInvalidNodeRef`。

| 行 | 做什么 | 失败时 |
|---|---|---|
| 58 | `auto &draft = transaction.edit();` 图通过持有它的 transaction 写，不另取引用（图库的直连账本要记账） | — |
| 59-63 | `camel::core::forwardRuleOf(uri)` 问 core 的绑定表：这个算子绑在哪个 family、哪个键。**每次现问，没有 C++ 表** | `why = "the operator has no forward rule bound at all"` |
| 64-69 | 绑定在、family 的索引却没有给它规则：`NoSuchFamily` 与 `NoRuleForPrimitive` **分开说**（一个模块没加载，一个规则集有洞） | 两种 `why` |
| 70-80 | 规则名 → 模板 id：线性扫 `genericRuleTable(context)` 找 `name == ruleName`；id = **下标 + 1**，0 表示没有 | `"the rule is named in the index but its templates are not registered"` |
| 85-86 | `genericRuleInstance(context, ruleId, {Type::Float64()}, {})` → 实例下标 `at`（类型实参是**逐元素类型**） | — |
| 87-96 | `genericRuleEntry(context, ruleId)->instances[at]`；检查 `made`/`program`/`rhs` 三者都在；`instance.why` 是实例化失败的原因，**原样带出去** | `instance.why` |
| 101-107 | 右边必须是"两层箭头"：`rhs->funcType()->exitType()` 的 `TypeCode` 是 `Function`，取成 `partialType` | `"the rule's right side does not answer with a function of the tangents"` |
| 110-115 | 空闭包元组：`instance.rhs->closureType()`（为 null 时现建空 `TupleType`），`::Function::create(instance.program, instance.rhs->id(), closureType, mm::autoSpace())` | `"the rule's right side is not a graph a function value can name"` |
| 120-121 | `draft.materializeStaticValue(toSlot<::Function *>(function), rhsType)`：规则右手的图变成**一个静态常量节点**，类型是 rhs 的整个函数类型 | — |
| 122-124 | **外层调用**：`addCallNode(partialType)` + `setNormInputs(values)`（原语的实参）+ `setWithInputs({rule})`（函数值放 `with` 位） | — |
| 125-127 | **内层调用**：`addCallNode(partialType->exitType())` + `setNormInputs(tangents)` + `setWithInputs({partial})` | — |
| 128 | 返回内层结果 = 这个原语的切线 | — |

三条要点：

1. **右边是柯里化的**（46-49 行的注释写死了这一点）：`(x, w) => ((dx, dw) => Tangent<T>)`。所以"用右边建节点"= 两跳调用，不是一次。
2. **`with` 位放函数值，`norm` 位放数据**：函数值节点是常量，数据是这条链的节点。
3. **规则每次现问**（40-44 行）：这一点是它取代"第二张 C++ 表"的全部理由，`vmap` 不能改成注册期缓存。

## 二、"从规则右边取一个名字"的入口（现在有这些）

| 入口 | 位置 | 返回什么 | 何时可用 |
|---|---|---|---|
| `rightCalleeOf(syntax::Node right)` | `rule_head.h:31` | 右边调的函数名（**裸名**），右边是值则空 | 语法期，需要 AST 节点 |
| `RuleHead::rhsCallee` | `rule_head.h:45` | 同上，随 `RuleHead` 记下 | 建库期 |
| `collectRuleHead(module, head, rule, rhsCallee, shape, variables, arity, …)` | `rule_head.h:58` | 写入一条（**取走语义的入口之一**） | 只有 ruleset 模块自己的构建代码能用 |
| `takeRuleHeads(module)` | `rule_head.h:71` | 该模块的全部 `RuleHead`，**取走**（读过即空） | 同上 |
| `lookupRule(family, head, arity)` | `rule_head.h:95` | `RuleMatch{status, rule}` —— **只有规则名，没有 `rhsCallee`** | 建库后，变换用 |
| `rhsCalleeOf(family, rule)` | `rule_head.h:99` | 该规则右边调的函数名（裸名） | 建库后，**变换要的就是它** |
| `ruleFamilies()` / `rulesOf(family)` | `rule_head.h:102` / `:113` | 有哪些 family / 一个 family 的全部条目（`head, arity, rule, variables`） | 建库后，读的人用 |
| `forwardRuleOf(uri)` | `forward.h:46` | 绑定 + **由绑定 family 的索引**解析出的 `RuleMatch` | 建库后 |
| `forwardBindingOf(uri)` | `forward.h:53` | 绑定本身：`family/head/arity`，**给第二个 family 用** | 建库后 |

三条坑（都踩过）：

1. **`RuleMatch` 没有 `rhsCallee` 字段**，只有 `status` 与 `rule`。要名字必须再调一次 `rhsCalleeOf(family, rule)`。
2. **名字是裸名**（`vmap_mul`），程序里的图叫 `rulesets.vmap::vmap_mul<float>`。建函数值要在 `instance.program->graphs()` 里按"去掉模块前缀与类型实参后的叶子名"找回那张图。
3. `takeRuleHeads` 是**取走**语义 —— 变换一旦碰它，规则集模块自己就建不出索引了；变换只读 `lookupRule`/`rhsCalleeOf`。

## 三、复用序列：第 1 步到第 6 步

| 步 | `jvp` 怎么做 | `vmap` 怎么做 |
|---|---|---|
| 1 | `forwardRuleOf(uri)`：绑定 + family 的答案 | **改**：`forwardBindingOf(uri)` 拿键，再 `lookupRule("rulesets.vmap", head, arity)` |
| 2 | 规则名 → `genericRuleTable` 的 id | 原样 |
| 3 | `genericRuleInstance(context, id, {元素类型}, {})` | 原样（元素类型 = 原语节点的类型） |
| 4 | `genericRuleEntry(...)->instances[at]`，查 `made/program/rhs` | 原样，`instance.why` 照带 |
| 5 | `closureType` + `Function::create(instance.program, instance.rhs->id(), …)` | **改**：图不是 `rhs`，是 `rhsCalleeOf` 点名的那个（见第四节第 3 步） |
| 6 | `materializeStaticValue(toSlot<Function *>, rhsType)` 得到函数值节点 | 原样（类型换成被点名的图的 `funcType()`） |
| 7 | 外层 `addCallNode(partialType)` + `setNormInputs(values)` + `setWithInputs({rule})` | **不复用**：批处理的图里没有未批实参可给外层 |
| 8 | 内层 `addCallNode(exit)` + `setNormInputs(tangents)` + `setWithInputs({partial})` | **不适用** |
| 9 | 返回内层结果 | **合并成一次**：`addCallNode(rowsOf(T))` + `setNormInputs(批实参)` + `setWithInputs({函数值})` |

一句话记法：**1-6 是"把规则变成函数值"，7-9 是"拿函数值算这一节点"**；`vmap` 换的是 7-9。

## 四、我们要加的那 3 到 5 步

1. **键从算子来**（已落，`05a8b3496`）：`forwardBindingOf(uri)` → `lookupRule("rulesets.vmap", binding->head, binding->arity)`；`jvp` 占了五个键，第二个 family 只能读键，不能改绑定。
2. **数量与名字分两次问**：`match->status` 判有没有，`rhsCalleeOf(kFamily, match->rule)` 取名字。
3. **名字 → 图**：在 `instance.program->graphs()` 里按叶子名找（`rulesets.vmap::vmap_mul<float>` → `vmap_mul`）；找不到就把"实例里有哪些图"写进 `why`，别只说"没找到"。
4. **一次调用，批当实参**：`addCallNode(rowsOf(原语类型))`、`setNormInputs(提升后的实参)`、`setWithInputs({函数值})`。这里没有外层/内层之分。
5. **常量提升成"一行批"**：`Array::create(alloc, T[], 1)` + `dataAs<T>()[0] = 常量` + `materializeStaticValue(slot, rowsOf(T))`；再给每个原语计数，印 `asked=N found=M`（判据那一格：规则集写对了 ≠ 用上了）。

## 五、边界

- 不动 `modules/transforms`、`autodiff`、`stdlib/rulesets` 的接口；core 只读已有的 `forwardBindingOf`，`jvp` 的路径一字未改。
- 规则机制一行不改：右边点名的名字走 `rhsCalleeOf`，与 `jvp` 读 `add_tangent` 是同一个入口。

## 六、草稿（已就地提交）

断网前已按上面的 1-5 步写过一版，提交时读数已齐、闸门还差一格：

- 新文件：`modules/transforms/vmap.h`（77 行）、`vmap.cpp`（479 行）、`vmap_operator.h`（30 行）、`vmap_operator.cpp`（189 行）；改 `module.cpp`（+28/-4）、`CMakeLists.txt`（+2）、`stdlib/rulesets/vmap.cml`（+75/-20）、`stdlib/signatures/transforms.cml`（+18/-1）。
- 读数（`vmap_elementwise.cml`，四个后端一致）：`REINTERP vmap function=f graph=232 type=(float[]) => float[] nodes=8 kinds=8/8 asked=4 found=4 rules=vmap_rule_mul,vmap_rule_div,vmap_rule_add,vmap_rule_sub`，然后 `[1.5, 4.0, 6.5, 0.25]` 两遍（`vmap(f)(xs)` 与 `xs->map(f)` 逐位相同）；`g` 一侧 `asked=5 found=5`、`[0.375, 6.0, -9.0]` 两遍；`REINTERP vmap op … same=yes`（算子签名与建出来的图一致）。
- 全量快闸 **8139 / 8145 过，2 红**：`registry.docs_complete`、`docs.links.citations`。后者已定位并修好（`vmap.h` 里一处 `src/builtin/operators/derivative.cpp#registerBuiltinDerivatives` 是"引行号"，改成 `#bindForwardRule`，本地 `node scripts/check-doc-links.mjs --citations` 现在是 `524 of 524`）；前者要 `--camel` 二进制，断网只能在主机上验，怀疑是新算子 `vmap` / 类型 `Vmap<F>` 的登记或示例。
- 上一笔已提交的绿 sha：`05a8b3496`（`forwardBindingOf`）。

**判据（自这一拿起）**：**四后端一致说"这不是某一个后端碰巧对了"，两遍一致说"这不是偶然"——两个都要有，缺一个就只是"跑通了"。**

## 七、作者 11-01 05:10 的追加裁定：右边不许引用左边的模式变量

**裁定原文（照页里那句）**：**外层既然不被调用，就不能有任何东西依赖它。** 所以 `rulesets.vmap` 这类"变换只读右边点名的函数"的 family，规则右边不许写左边的模式变量（`x`、`w`），**在建索引时检查，违反就报编译错误**（不是运行时、不是警告）。

落地方式（一笔，已提交）：

- **数据**：`outerNamesUsedInRight(left, right)`（`include/camel/parse/ast/rule_head.h`，实现 `src/parse/ast/rule_head.cpp`）——左边叶名字与右边叶名字取交；`RuleHead::outerUses` 带着它走，`collectRuleHead` 多一个参数，`src/parse/ast/builder.cpp` 在**两侧都还是语法**的地方算出来（同一个地方已经在算 head / callee / variables，没有第二个读法）。
- **检查点**：`src/compile/gir/program.cpp` 的建索引循环里，紧跟"左边不是一个算子"之后、`indexRule` 之前——**被拒的规则不进索引**，和重复键一样是"先拒后不记"。诊断 `SemanticDiag::RuleRightUsesOuterVariable`（`0x03000207`，`diagnostics/registry.tsv` 里一行，`--write` 发的号）。
- **哪些 family**：`familyCallsOnlyItsCallee`（同文件匿名 namespace）= `rulesets.vmap` + fixture `rulesets.probe_batch`。**`rulesets.jvp` 不在里面**：`jvp` 自己调右边（先拿操作数调一遍、再拿它答的东西调一遍），所以它的规则就该写 `x`、`w`，那条检查加不到它头上。
- **反例**：`stdlib/rulesets/probe_batch.cml`（fixture，形状照 `rulesets.vmap`，一条故意违例的 `probe_rule_mul`）+ `test/cases/compile/transforms/vmap_rule_outer_variable.cml`；报错原文——

  ```
  error[SemanticDiag::RuleRightUsesOuterVariable]: The rule 'probe_rule_mul' reads 'w' on its right side, and 'w' is a name its left side binds: …
     --> stdlib/rulesets/probe_batch.cml:22:1
   22 | rule probe_rule_mul(x: float, w: float) {
   23 |     x * w => (bx: float[], bw: float[]) => probe_scale(bx, w)
  ```

- **读数**：`asked=N found=M missing=K` 是遍的**正式输出**（stdout，无环境变量、无开关），`askedFoundMissing()`（`modules/transforms/vmap.cpp`）一处拼、三处用；`missing` 是"问了但规则集没有"的洞。
- **收尾（下一笔，不在这一笔做）**：`stdlib/array.cml` 里逐行的 `vmap` 在 `reinterp<vmap>` 落地后改成**调变换**，同一程序**逐位相同**才算数，然后把逐行实现**删掉，不留两份**。

**预计改哪个文件、几行**：`modules/transforms/vmap.cpp` 约 480 行（新文件，其中规则读取 1-6 步约 60 行照 `jvp.cpp:50-129` 抄改、构建约 300 行）、`vmap_operator.cpp` 约 190 行、`vmap.h` 约 77 行、规则集 `stdlib/rulesets/vmap.cml` 约 +75/-20 行、`module.cpp` 约 +28 行、`CMakeLists.txt` +2 行、`stdlib/signatures/transforms.cml` +18 行。第七节那一笔另加：`rule_head.h/.cpp`（+1 个函数、+1 个字段、+1 个参数）、`builder.cpp`（+6）、`program.cpp`（+15 建索引处 +11 helper）、`diagnostics/registry.tsv`（+1 行）、fixture `stdlib/rulesets/probe_batch.cml` 与反例用例各一个新文件。