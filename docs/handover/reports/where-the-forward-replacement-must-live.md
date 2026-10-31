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

## 桥通了：`ruleId` → 右侧图（`GraphId` + 图名）

同一段探针，把实例右侧图自己的 id 与名字打出来：

```
PROBE jvp_add rhs GraphId=64 name=rulesets.jvp::jvp_add__rhs<float> nodes=4
PROBE jvp_div rhs GraphId=75 name=rulesets.jvp_div__rhs<float> nodes=4
```

**这就是作者说的那条桥**：规则值只带 `ruleId`，`genericRuleInstance` 把它换成实例，实例的
`rhs->id()` 是一个真的 `GraphId`（`64`/`75`），名字就是 `<rule>__rhs<float>`。**替换要拿的就是这
张图，调用它**——不是到图里去找一个“指向 `div_tangent` 的调用节点”。

**为什么图里没有调用节点**：`compileRule` 的产物 `camel::runtime::Rule` 的 `rhs` 是
`Pattern`（`include/camel/runtime/writings.h:64-75`），而 `Pattern::Kind` 只有
`Var / Constant / Op / Compute`——**没有 Call 这一种**。`div_tangent` 的调用在**编码后的模板体**
里，实例图上只有它的入口形状（2 Port + 1 Data + 1 Fill）；调用这张图，`div_tangent` 就在里面跑。

页首那句“找右图里那个调用节点”要按这个读数修正：**桥是“拿到右侧图”，不是“拿到图里的调用节点”。**

## 判据：方向与形状不是一回事

> **裁定给的是方向，读数给的是形状；桥是“拿到右侧图”，不是“拿到图里的调用节点”——差这一句，能让
> 下一个人在 `Pattern` 里找半天 Call。**

裁定说“把右侧图实例化出来调用”，方向没错，但它没说“那是一条边还是一张图”。读数说的是形状：
`Pattern::Kind` 里没有 Call，右侧图本身就是被调用的那段代码，所以替换手里要攥的是 `GraphId`
（`64`/`75`），不是某个节点指针。**方向对了不等于落点对了**；每个跨层的地方都要这样对一次读数。

## 第一个形态：图建出来了，但还没有地方住

`modules/transforms` 里写了一个 pass `transforms::reinterp`：走遍程序里每个“由算子组成”的函数，
为每个函数在自己的 `GraphDraftGroup` 里预留一张图、建出来、打印 `GraphId` 与节点数。手工跑：

```
REINTERP target=f primitives=3 other=2 graph=49 nodes=1
REINTERP target=main primitives=1 other=4 graph=50 nodes=1
5.5
```

**建得出来**（`graph=49`、`nodes=1`），primitives 也数对了（`f = x*x + 3/x` 是乘、加、除三个，
两个常数不算）。它还不是替换：图里只有一个 port 直接接到输出——**先证“建得出来”，再证“建对了”**。

**但它进不了提交**，卡在登记簿上，这条留给作者判：

- `declaration.guard_script`：注册了 pass 就必须在模块的 `.cml` 里声明它，否则
  `a pass is registered and its module's .cml declares no function for it: transforms::reinterp`。
- 写了 `stdlib/transforms.cml` 之后，`registry.migrations_stay_migrated` 要求
  `scripts/stdlib-reference.mjs` 的 `MODULES` 里有一条 `transforms`。
- 那条一加，`--verify-ops` 就红：`MODULES lists 'transforms', but no module of modules/ registers
  operators under that name`（`scripts/stdlib-declarations.mjs:470`）——**登记簿的单位是算子，
  一个只注册 pass 的模块没有算子**。
- 退到 `UNCHECKED_MODULES` 也不行：它要 `covered_by`，而 `--verify-ops` 看不见一个没有算子的模块。

所以“pass-only 模块在登记簿里住哪”今天没有答案。我没有用 `registered: '<builtin>'` 去糊弄过去，
也没有把 `GraphDraft` 写成 `auto` 去躲 `graph_draft_ledger` 的计数——**这两个都是让计数变成谎话**。
工作树已退回 `5db161bc2`，一个红的提交都没留。

## 判据：账本的类型错了，不是模块错了

> **一个只贡献 pass 的模块，在按算子记账的登记簿里没有位置——那是账本的类型错了，不是模块错了；
> 改账本，别让模块冒名。**

裁定给了路：`MODULES` 条目可以写 `passes_only: true`（`scripts/stdlib-reference.mjs`），判据
（`scripts/stdlib-declarations.mjs`）对这条跳过“必须注册算子”，改成查另外两件事：`modules/<name>`
存在且源码里真有 `registerModulePass(`，以及 `stdlib/<name>.cml` 里真有那条声明。**边界守住了**：

- 记成 `passes_only` 却实际上注册了算子的模块，会被判红（“它按普通算子模块查，不带这个旗子”）；
- `passes_only` 且目录不在、或源码里没有 `registerModulePass(`、或声明文件不在，三种都红；
- `--verify-ops` 的算子覆盖、`UNCHECKED_MODULES` 的 `covered_by` 一个字没动。

**一处容易漏的坑**：`readReferenceModules()` 只把 `name/kind/source/registered/declaration/computed`
抄进解析后的条目，新字段不加进去就等于没写——第一次跑它照样红，就是这么回事。旗子要跟着
`field()` 的清单一起长。

改完 `reinterp` 接回来了，`reinterp_builds_its_own_graph.nvm` 绿，`graph_draft_ledger` 四个数没动。

## 判据：第二个键记进索引，不要每次反查

> **两套键（URI 与写法）之间不要用“族里恰好有”来翻译；把第二个键记进索引，比每次反查便宜，也不会因
> 族变化而失真。**

索引按**写法**建键（`a + b` → `+`, arity 2），节点按 **URI** 说它是谁（`:op/add_d`）。**这两套键
不是一回事**：一条 `+` 规则在 float 上是 `:op/add_d`，在 int 上是 `:op/add_i`——按写法反查族，是把
“族里此刻恰好有这条规则”当成翻译函数；族一变，翻译就假。

做法是把 URI 记在**实例**上：`GenericRuleInstance::lhsOperatorUri`
（`include/camel/compile/gir/rule_templates.h`），在 `genericRuleInstance` 把左侧 publish 出来之后，
从左侧图的第一个 OPER 节点读（`src/compile/gir/rule_templates.cpp`，`nodeBodyAs<GCOperBody>`）。
它是“这条规则的左侧写的是什么”的事实，跟着左侧走，不跟着族走。

读数（`import rulesets.jvp` 之后，`transforms::reinterp` 打在 `f(x) = x*x + 3/x` 上）：

```
REINTERP uri :op/mul_d -> rule jvp_mul -> rhs 63
REINTERP uri :op/add_d -> rule jvp_add -> rhs 74
REINTERP uri :op/neg_d -> rule jvp_neg -> rhs 85
REINTERP uri :op/sub_d -> rule jvp_sub -> rhs 96
REINTERP uri :op/div_d -> rule jvp_div -> rhs 107
REINTERP target=f primitives=3 other=2 graph=112 nodes=1
REINTERP target=main primitives=1 other=4 graph=113 nodes=1
5.5
```

**一行就是替换要攥的全部**：`/` 那个节点是 `:op/div_d`，规则 `jvp_div`，它的右侧是图 `107`。
（注意这个用例得 `import rulesets.jvp`——索引里只有程序自己的 ruleset 模块放进去的东西，不 import
就一条都没有，`reinterp` 会安静地只打 `target=` 那两行。**“没有输出”和“没有规则”是两件事**。）

## 判据：第一笔只换一个原语

> **逐原语替换的第一笔只换一个原语——三个一起换，验不过时不知道是谁的错。**

## 那一笔之前先量到的一件事：右侧不是算子的签名

把右侧图的端口数也打出来之后：

```
REINTERP uri :op/mul_d -> rule jvp_mul -> rhs 63 ports=1 nodes=4
REINTERP uri :op/add_d -> rule jvp_add -> rhs 74 ports=1 nodes=4
REINTERP uri :op/neg_d -> rule jvp_neg -> rhs 85 ports=0 nodes=3
REINTERP uri :op/sub_d -> rule jvp_sub -> rhs 96 ports=1 nodes=4
REINTERP uri :op/div_d -> rule jvp_div -> rhs 107 ports=1 nodes=4
```

**`jvp_div__rhs<float>` 只有 1 个端口，而它要替换的 `:op/div_d` 有两个操作数。** 所以“把 `div_tangent`
的调用放到 `/` 站的那个位置”**过不了类型**：右侧不是算子的替换品，它是**规则整个左侧的变量**上的一张
图（值、切线的成对输入），端口是那组变量的入口。

这一条把“第一笔”的写法改掉了：**先要有“规则的变量表 → 一个元组”的打包**（把 `/` 两个操作数的值与
切线按规则左侧的顺序装进去），才谈得上调用。端口数是这把尺子，不是猜测。

## 验收（缺一步不算完）

1. **前向**：`f(x) = x*x + 3/x` 在 `x = 2` 处，`f = 5.5`，`f' = 4 - 0.75 = 3.25`。两个数都要打出来。
2. **缺规则**：`rulesets.probe_missing`（已有，四条、缺 `/`）走同一个 `jvp`，**诊断必须落在 `/`
   那个节点的 `file:line` 上**——这是 `probe_missing` 那条红“从替换层出来”的形态。
3. **还原**：第 2 步的红消失，第 1 步的两个数不变。