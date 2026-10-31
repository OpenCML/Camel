# 前向替换落在哪一层，以及它缺的那一块

日期：2026-10-31　车道：`lane-recheck`　状态：**未落地**（这一页是地图，不是结论）

## 判据

**替换的验收不是“跑通了”，是“缺一条规则时，诊断落在缺它的那个节点上”。** 一个能跑出正确数字的
替换，若在 `x / w` 处悄悄用一个猜出来的公式顶上，它验的是“我认得 `/`”，不是“索引里有没有 `/`”。

## 落点（已定，(`a`) 裁定）

`reinterp<H>` 与 `with_grad` 同层：**`modules/autodiff`，紧挨着 `buildGradientGraph`**。依据写在
`modules/autodiff/pullback.h` 的头注释里：“After macros have run, a program is a static graph, so
derivatives are a graph rewrite rather than a runtime tape.” 微分是**宏之后对运行期图的重写**，
前向替换是同一件事的另一个方向：不是 `grad`，是 `jvp`。

已经有的入口（下一笔从这里起）：

```cpp
// modules/autodiff/pullback.h —— 反向的入口，前向要写的是它的对称物
GradientGraph buildGradientGraph(
    const camel::core::context::context_ptr_t &context,
    const camel::runtime::Program *program,
    camel::runtime::GCGraph *function,
    bool withValue, bool hasAux = false);
```

- 图：`Program::graph(GraphId)`、`GCGraph` 的节点遍历、`DraftWritings`（宏之后图的写入口）；
- 索引：`camel::parse::ast::lookupRule(family, head, arity)` 与 `rhsCalleeOf(family, rule)`
  （`8d882ea74`：规则调的那个函数，从规则本身读，不在 C++ 里另抄一份）；
- 键：`(family, head, arity)`，`-x` 与 `x - w` 是两个原语。

## 缺的那一块（下一笔第一个要答的问题）

**名字怎么在宏这一步变成一个图 id。** 运行期的 `Program` 没有按名字取函数的入口（只有
`graph(GraphId)`、`root()`、`graphs()`、`calleeOf`）；`Builder` 那一侧有名字解析
（`ownNativeEntity(const Reference &)`、导入表），但 `buildGradientGraph` 拿到的只有
`program` 与 `function`。

三条可能的路，按代价排：

1. **从规则的图里取**：规则值带着它的两侧图（`RuleObject` 的 `lhs`/`rhs`），右端图里那个对
   `add_tangent` 的调用节点已经指向实例化后的图 id——按 `rhsCalleeOf` 的名字找不到它，但可以按
   “右端图里第一个调用节点”找到。**不需要新入口。** ——**已证：今天不通，见下节。**
2. **让变换在 `Builder` 里跑**（与宏算子同处），那里名字能解析：`addCallNodeOf(calleeGraph)` 已经
   能建调用节点。代价是变换要拆成两份（构造侧与重写侧）。
3. **给运行期 `Program` 加按名字取的入口**——**跨层，除非前三层都走不通，不加**（作者的纪律：
   只用编译期已有的能力）。

## 路 1 的实证：**今天不通**

判据：**一条跨层的路，先用一行打印证明它通；证明了再写第二行。**

打印（临时探针，`std::rulesets` 里遍历 `program->tableSize()` 的每个 `GraphId`，打印图名与
FUNC/CALL 节点指向的 `GraphId` 与图名；程序 `probe_rule_graph.cml` 里把规则**当值用**
`let r = jvp_add`，并 `println(r)` 打出 `rule jvp_add`）：

```
PROBE all 1  name=__root__                          mangled=__root__
PROBE all 54 name=main                              mangled=main
PROBE all 55 name=rulesets.jvp::lift<float>          mangled=rulesets.jvp::lift<float>
PROBE all 56 name=rulesets.jvp::zero_tangent<float>  mangled=rulesets.jvp::zero_tangent<float>
rule jvp_add
```

**整个程序只有四张图，一张也不是规则的两侧**——尽管规则值确实存在（`rule jvp_add` 打出来了）。

取不到的那一处（结构上的缺失，不是某个函数返回空）：

- `include/camel/runtime/program.h:79` —— `graph(GraphId)` 是唯一的取图入口；
- `include/camel/runtime/program.h:175` —— `tableSize()` 是唯一的规模读数，**没有按名字取的入口**；
- 探针处：`src/passes/trans/rulesets/rulesets.cpp` 的 `apply`（已删除，不留）。

**这个结论是错的，作者裁定在先（`fsh-dev = 480523c40`）：“规则本身就是引用，不是名字：规则值带着
它两侧的图。”** 四张图里没有两侧，是因为**探针找错了地方**——找的是“按名字取的函数”，而两侧挂在
**规则值身上**，不在 `Program` 的图表里。

判据（替掉今天那条的一半）：**探针找错地方，会得到一个正确的空答案；`tableSize` 里没有规则两侧，
不等于规则两侧不存在。**

## 那么两侧图去哪了：**被存起来**

判据：**一条路走不通时，最贵的不是绕路，是不知道它为什么不通；这次拿到的是“规则两侧在定型后
不存在”，这比绕路有用。**

读 `src/compile/gir/` 的规则声明一侧，答案是三个字：**被存起来**。存的地方与索引：

- `src/compile/gir/builder_generics.cpp:603` —— `registerGenericRule(...)`，把规则的两侧模板
  登记进表，返回 `ruleId`；**规则值只带这个 id**（`builder_tags.cpp:354` 的注释：
  “no graph stands for it, and the value namespace is the only place it appears”）；
- `src/compile/gir/rule_templates.cpp:35` —— 表挂在 **context** 上（`context.genericRuleTemplates()`），
  “so a rule value can keep an id instead of a pointer and an instance outlives the pass that made it”；
- `include/camel/compile/gir/rule_templates.h:96-112` —— `GenericRuleEntry` 存 `lhs`/`rhs`/`when`
  模板与 `instances`；每个 `GenericRuleInstance`（`:60-77`）带着 **`program`、`lhs`、`rhs`、`when`
  四张真实的图**，“in one program of the context that is never released”；
- `include/camel/compile/gir/rule_templates.h:152` —— `genericRuleEntry(context, ruleId)`；
  `:168` —— `genericRuleInstance(context, ruleId, types, with)`：按类型元组实例化一次、留下，
  **问两次答同一个位置**。

所以：**两侧没有被丢弃，也没有被丢弃的风险**——它们是模板，实例在第一次被问到时建成真图，之后
一直在。缺的只是**运行期**那一侧没有入口（`Program` 里看不到 context 的表）；而替换按作者的裁定
就落在编译期（`(a)`），编译期这一侧的入口已经齐了：`genericRuleEntry` + `genericRuleInstance`。

**结论**：路 2 不需要新 API，也不需要问作者；要证明的是“变换能把 `ruleId` 换成实例的 `rhs` 图”。
下一个一行打印就该打这个。

## 两侧图挂在规则值上（作者的裁定，`fsh-dev = 480523c40`）

字段与位置：

- `modules/rewrite/rule.h:84-85` —— `RuleObject::lhs()` / `RuleObject::rhs()`；
- `modules/rewrite/rule.h:108-109` —— 字段 `::Function *lhs_; ::Function *rhs_;`；
- `modules/rewrite/rule.h:72-73` —— “A rule value that keeps an id has no `lhs`, `rhs` or `when` of
  its own”：**实例化的规则值只带 `ruleId()`**，两侧要从实例拿。

两行打印（探针在 `std::rulesets` 的 `apply` 里，按名字在 context 的通用规则表里找到 `jvp_add`
与 `jvp_div`，`genericRuleInstance(context, ruleId, {Float64}, {})` 取实例）：

```
PROBE rule jvp_add made=1 rhs=graph why=''
PROBE   rhs nodes=4 live=4 graphs=69
PROBE   node 0 kind=1   (Port)
PROBE   node 1 kind=1   (Port)
PROBE   node 2 kind=0   (Data)
PROBE   node 3 kind=4   (Fill)
PROBE rule jvp_div made=1 rhs=graph why=''
PROBE   rhs nodes=4 live=4 graphs=80
PROBE   node 0 kind=1 / node 1 kind=1 / node 2 kind=0 / node 3 kind=4
```

**实例的 `rhs` 是有的（4 个节点的图），但里面没有调用节点**：两个 Port、一个 Data、一个 Fill。
也就是说，**存下来的这一份不是能跑的那一份**——真正被引擎应用的那份规则由
`modules/rewrite/module.cpp:669` 的 `compileRule(ctx, source)` 编出来，存在
`GenericRuleEntry::compiled`（`include/camel/compile/gir/rule_templates.h:112`，“owned by the
engine”）里；`instanceRules`（`modules/rewrite/module.cpp:636`）是那个模块里的 `static` 函数。

所以桥的下一段是：**`ruleId` → 实例 → 编译后的规则（引擎应用的那一个）→ 它的 rhs 图 → 调用节点
→ `GraphId`**。这一段要么给 `modules/transforms` 一个取“编译后规则”的入口（`compileRule` 是
`camel::runtime` 的公开函数，`instanceRules` 不是），要么让变换在引擎应用规则的地方顺手拿——**先
用一行打印证它通，再写第二行**。

## 验收（缺一步不算完）

1. **前向**：`f(x) = x*x + 3/x` 在 `x = 2` 处，`f = 5.5`，`f' = 4 - 0.75 = 3.25`。两个数都要打出来。
2. **缺规则**：`rulesets.probe_missing`（已有，四条、缺 `/`）走同一个 `jvp`，**诊断必须落在 `/`
   那个节点的 `file:line` 上**——这是 `probe_missing` 那条红“从替换层出来”的形态。
3. **还原**：第 2 步的红消失，第 1 步的两个数不变。