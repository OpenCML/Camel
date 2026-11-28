# 调用的定型次序：合并后只剩一个解析器

**日期**：2026-11-28　**性质**：只读分析页（未开枝、未改代码；这一页在工作树里是 `??`，不提交）
**基线**：`origin/integration`，读数取在 `6aaf4555da`（batch47）。文中的行号都对着这一尖。

## 0. 一句话

"调用选中哪个条目"今天有四处作出决定（收集、单签名、第二条路、算子组），三张缺陷的脸都从**同一个根**长出来：
**调用的定型次序**——先定普通实参、解出类型变量，再拿实例化后的形参类型去定 lambda 与带类型期望的字面量。
按这个次序重写一遍，收集只剩一处、答案只剩一个，其余是同一决定的重复表达。

## 1. 出处（逐字，不改写）

**每日审查第 2 条**（`internal/plans/2026-11-28-daily-review.cn.md:13-16`）：

> 2. **"调用选中哪个条目"有了第二条路径**（归前端类型检查阶段的重构，见架构第 8 节"重载选择只有一个解析器"）。
>    - `src/compile/src/gir/builder_call.cpp` 的 `withParameterOfSelectedEntry` 在 `signatureForArgumentContext` 之外，再扫一遍候选条目，为的是让 `foreach` 作用在 `Range` 上时能定型。
>    - **修法**：`foreach` 对 `Range` 这个例子进回归用例，答案归那一个解析器。
>    - **当场修**：同处那段解释注释整段重复了一份，删掉一份。

**两条类型缺陷**（`internal/backlog.md:2222` `D-1128-195 ①`、`:2223` `DEFECT-call-typing`）：

> ① 两条缺陷（实参位置 lambda 不取形参类型；`[] as U[]` 在泛型函数体里过不去）**不等到 12-03 之后、也不当补丁**，与审查第 2 条（`foreach` 作用在 `Range` 上另走一条选条目的路 = `withParameterOfSelectedEntry`）**并成一件"调用定型只有一个解析器"**。

> **同一根的三张脸**（`task-455`）：① 实参位置 lambda 不取形参类型（`LambdaParameterWithoutType`，"nothing here expects a function type"，帮助文字自引 5.9；`with` 实参位置今天**能** ⇒ 断在普通实参这条期待链）；② `[] as U[]` 在泛型函数体里过不去（`LiteralNotRepresentable`）；③ `foreach` 作用在 `Range` 上另走一条选条目的路。归因：**调用的定型次序**——先定普通实参、解出类型变量，再拿实例化后的形参类型去定 lambda 与带类型期望的字面量。排期：四件重构之后。

架构的第 8 节把这一条写成目标（`docs/design/architecture/abstractions.cn.md:179`）：

> 3. 类型检查：推断、重载、泛型实例化、静态展开，产出类型完整的树。重载选择只有一个解析器（今天四处）……

## 2. 三张脸今天各自在哪一行上

**① 实参位置 lambda 不取形参类型**

```
症状（诊断发出处）：src/compile/src/gir/builder.cpp:1238-1243
    `expected == nullptr` ⇒ `SemanticDiag::LambdaParameterWithoutType`
    `.commit(name, "nothing here expects a function type")`   ← 原文那句在 :1242
    同函数另两处：:1246-1254、:1262-1265（`completeLambdaSignature` 在 :1211）
    GCT 侧同一诊断：src/compile/src/gct/builder_type.cpp:482
根（把期望类型丢掉的判断）：src/compile/src/gir/builder_call.cpp:444-450
    `wanted = parameterInPosition(wanted);`
    `ExpectedValueScope argumentScope(expectedValueType_, wanted && !containsTypeVar(wanted) ? wanted : nullptr);`
    —— 形参只要还带类型变量，这个实参一个期望类型都拿不到
对照（`with` 侧今天为什么能）：builder_call.cpp:697-716 `usableExpected`、:741-783 `withParameterOfSelectedEntry`、:796-806 调用点
```

**② `[] as U[]` 在泛型函数体里过不去**

```
症状（诊断发出处）：src/compile/src/gir/builder_diagnostics.cpp:2879-2881（`rejectLiteralData`）
    同文件 :2804（`rejectLiteralValue`）；GCT 侧：src/compile/src/gct/builder_data.cpp:137/161/167/608/629、gct/builder_internal.h:365
    文案模板（registry.tsv:72）：The literal '{0}' cannot be represented as type '{1}': {2}.
根（读代码得出，**端到端待一次运行钉住**）：src/compile/src/gir/builder_expr.cpp:1568-1570
    `visitCastNode` 里"字面量按写出的类型就地转换"那条分支的条件：
    `operand->type() == DATA && (inferred || containsTypeVar(written)) && asTagged(written) == nullptr && asCollectionTypeOrPrototype(written) == nullptr`
    —— 转成 `U[]` 这类集合类型时 `asCollectionTypeOrPrototype(written) != nullptr` ⇒ 分支不进 ⇒
    字面量改走复合字面量那条路，元素类型还是类型变量 ⇒ 到不了具体值
```

**③ `foreach` 作用在 `Range` 上另走一条路**

```
第二条路本体：src/compile/src/gir/builder_call.cpp:741-783 `withParameterOfSelectedEntry`（声明与使用都在这一段，调用点 :799-806）
它为什么存在（代码自己说的）：builder_call.cpp:738-740
    "The group's resolution cannot answer this here: for a name the built-in layer also declares (`foreach` over a `Range`),
     the entry that fits is one of those declarations, and the group does not hold it."
两条答案的差别：`#2`（singnatureForArgumentContext）给数组条目的 `with <f: (item: T) => void>`（T 没人绑），
    `#6` 给 range 条目的 `with <f: (item: int) => void>`（具体）
顺带（审查第 2 条"当场修"那一半）：builder_call.cpp:717-727 与 :728-740 是同一段解释的**两份**，还在。
```

## 3. 十个解析点（每个标"删 / 留"）

| # | 位置（`6aaf4555da`） | 它回答什么 | 判 |
|---|---|---|---|
| 1 | `gir/builder_call.cpp#callCandidateSignatures`（:99-158） | 这次调用**能落到哪些条目**：组的规则 + 内建层同拼写的带体函数 + 泛型模板（:134-151）；每条取签名有 `fixedSignature`→`declaredSignature`→`graftedSignature` 三种形态（:117-132） | **留**（升为唯一的"收集"） |
| 2 | `gir/type_arguments.cpp#signatureForArgumentContext`（:19-126） | 参数**只能拿到哪一个**签名（条目一致才给，否则 `nullptr`；泛型重载 :20-51、算子组 :63-80 各写一遍同样的"一致"规则） | **删**（作为独立答案）；它退化成入口的一步 |
| 3 | `gir/builder_with.cpp:124` | `with` 实参（`reduce<(a,b)=>a+b, 0>` 那种）拿哪个签名 | **删**（合并后与普通实参同一处；它今天只是 #2 的一个调用点） |
| 4 | `gir/builder_call.cpp:444-450` | 普通实参这一位置的期望类型（形参带类型变量就丢） | **改**：删掉"带类型变量就丢"，位置类型改为取自**被选中的条目** |
| 5 | `gir/builder_call.cpp:697-716` `usableExpected` | 这个候选类型够不够具体、能不能当"位置"（函数类型结果开放也算可用） | **留**（成为唯一判据，并把 #4 并进来） |
| 6 | `gir/builder_call.cpp:741-783` `withParameterOfSelectedEntry` | 普通实参选中哪个条目、**它的** with 形参在这里是什么 | **删**（整条第二条路） |
| 7 | `gir/builder_call.cpp:1040-1050` `accepts` + `src/kernel/include/camel/core/type/resolver.h:214 StaticFuncTypeResolver` | 这个条目**接不接受**这些实参类型 | **留**（内核声明引擎，见 §5） |
| 8 | `gir/builder_call.cpp#resolveGenericCall`（:1387） | 类型变量**解成什么** | **留**（并进同一条次序：先普通实参、后 lambda 与带期望的字面量） |
| 9 | `src/kernel/include/camel/core/operator.h:223 OperatorGroup::resolveForCall`（调用点 `builder_call.cpp:1423`；另有 `builder_control.cpp:114/174/285/346/386/436` 六处合成调用） | 这一组里**哪个算子**接这次调用 | **留**（被调用一次，不再并列；见 §5） |
| 10 | GCT 侧同题两处：`gct/builder_type.cpp:482`（lambda 参数）、`gct/builder_data.cpp:137/161/167/608/629`（字面量） | 同一批问题的第二张脸（架构第 8 节：23 条诊断两层各查一遍） | **删**（随阶段搬迁；"一条诊断只在一个阶段报"） |

## 4. 三处真分歧：合并后只剩哪一个答案

**(a) 位置类型：`#2` vs `#6`**

```
今天两个答案：`#2` 说"参数只拿到条目一致的那一个签名"（foreach over Range ⇒ 数组条目的 `(item: T) => void`，T 没人绑）；
            `#6` 说"普通实参选中的那个条目的 with 形参才是这一位的类型"（⇒ `(item: int) => void`）。
合并后只剩：**取自被选中条目**（#1 已经收集了内建层声明，选中一步就在它上面做）。
为什么 #2 能删：它的"一致才给"是**在没有选中条目时**的权宜答案——它只能看见"条目都同意的那一个签名"，
  而真正的次序是先定普通实参、选中条目、再从该条目读这一位的类型；#2 的那条规则在这个次序里没有位置。
  它留下的一点价值（"哪个名字有多个条目"）由 #1 的收集承担。
```

**(b) 期望类型：`#4` vs `#5`**

```
今天两套判据：`#4`（普通实参）只要形参含类型变量就丢，一档都不给；
            `#5`（with 实参）允许"函数类型结果开放"这种半具体类型当位置。
合并后只剩：`#5` 的判据（usableExpected），普通实参与 with 实参同一套。
为什么 `#4` 能删：它的"含变量就丢"正是缺陷 ① 的根。次序改对之后，普通实参的形参在"选中条目 + 解出类型变量"
  之后已经是具体类型，不再需要"含变量就丢"这一刀；而它挡不住的情形（lambda 结果开放）`#5` 本来就答得上。
```

**(c) 接不接受：`#7` vs `#8`**

```
今天两套引擎：`#7` 用 `assignableFrom`/`StaticFuncTypeResolver::resolve` 判 fit（builder_call.cpp:758 还手写了一份同样的判断）；
            `#8` 用类型变量合一（unify）解出绑定。
合并后只剩：同一条次序里的两步——先 `#7` 判这条声明接不接受、`#8` 在它接受之后解变量；两者不再是"两个答案"。
为什么其中一份能删：真正重复的是 `#7` 的**手写 assignableFrom 副本**（builder_call.cpp:750-759），它必须和
  `accepts`(builder_call.cpp:1040-1050) 给出的答案一致；合成一处后，"同一条声明两种问法"就没有了。
```

## 5. 不能删的两个，各自删了会失去什么

**`StaticFuncTypeResolver`（`src/kernel/include/camel/core/type/resolver.h:214`）**

```
它答的是：**这一条声明的签名**接不接受这些类型、绑定什么。单条目、带 rest/with-rest、带静态参数与编译期常量，
  还会被 `graftedSignature` 补出参数类型（resolver.h:112-119 的注释：让没写类型的 lambda 从声明的形参取类型）。
删了会失去：算子记录自己的"声明驱动接受判断"。内核的算子记录、规则求值、`inspect --ops` 与 C++ 一侧的入口
  都按这条声明问"这次调用能不能落在我身上"；把它搬进编译器，等于让内核丢掉判定自己声明的能力，
  编译器成了唯一能回答"这个算子接不接受"的地方，而编译期之后的阶段（重写、求值、导出）仍需要这个答案。
  它也不回答"多个条目里选哪条" —— 那是 #1/#9 的问题，不是它的。
```

**`OperatorGroup::resolveForCall`（`src/kernel/include/camel/core/operator.h:223`）**

```
它答的是：这一组里**哪个算子**接这次调用（按各 resolver + 静态参数 + entering 形态选）。
删了会失去：合成调用那条路。`builder_control.cpp:114/174/285/346/386/436` 六处不是从源码来的调用，而是建图时
  要落地成一个算子（分支/汇合/循环处的算子），它们直接调 `resolveForCall`；重写器与 `inspect` 同理。
  而且它**看不到**内建层同拼写的声明（`builder_call.cpp:738-740` 自己写着），所以它只能做"已选中之后的落地"，
  不能当"选条目"的唯一答案 —— 这两条正是它留下来、`#6` 却能删的理由。
```

## 6. 缺陷 ①②③ 修完之后应该看到什么读数

不写"变绿"，写**哪条用例的哪个字变了**。

**① 实参位置 lambda 不取形参类型**

```
今天能过的是 with/尖括号写法：test/plans/feat/compile/prelude_signatures.plan.toml 的
  `untyped_lambdas_of_builtins`（用例 test/cases/compile/prelude_signatures/untyped_lambdas.cml，
   写法 `xs->filter<func (x) { x % 2 == 0 }>`），它的 verify_script 已经钉住逐行输出：
   `[2, 4]` / `[10, 20, 30, 40]` / `1` `2` `3` `4` / `10` / `[bb, ccc]` / `[3.0, 5.0]`
修完后应看到：新增一条把同样的 lambda 写成**普通实参**的用例（`filter(xs, func (x) { ... })`、`map(xs, ...)`、
  `reduce(xs, func (a, x) { a + x }, 0)`），其 stdout 与上面那七行**逐字相同**、`expect_exit = 0`；
  而 test/cases/compile/lambda_inference/no_expected.cml（`let g = func (x) { x + 1 }`，位置确实没有函数类型）
  **仍**是 exit 1 且诊断仍是 `LambdaParameterWithoutType` + "nothing here expects a function type"（文案不动）。
判据的反面：若新用例 exit 0 但那七行有任何一行变了 ⇒ 不是位置传递的问题，是实例化次序变了，停手。
```

**② `[] as U[]` 在泛型函数体里过不去**

```
今天 collection 的 Map/Set 有先例能过：test/cases/compile/collections/generics.cml:31 `([] as Map<K, V>)`；
  相邻的 `err_literal.cml` 钉住的是 LiteralNotRepresentable 的另一半（`let z: T = 0.5`）。
修完后应看到：新增一条泛型函数体里 `[] as U[]` 的用例（例如 `func empty::<U>() => U[] { return [] as U[] }`，
  `println(empty::<int>())`），exit 0、stdout 是 `[]`；同一批里 `err_literal.cml` 的诊断**逐字不变**
  （仍是 `The literal '0.5' cannot be represented as type 'T': ...`）。
待钉住的一条（我不拿推断当读数）：②的端到端抛点要一次运行确认；本轮只核到 builder_expr.cpp:1568-1570 的分支条件。
```

**③ `foreach` 作用在 `Range` 上**

```
修完后应看到：新增一条 `(0 .. 3)->foreach<func (x) { println(x) }>` 的用例，exit 0，输出与数组形式
  `[0, 1, 2]->foreach<func (x) { println(x) }>` **逐字相同**（端点含不含由 Range 语义定，落笔前先跑一次打印，
  不在这页里猜）。
代码侧的两个读数（这两条才证明"第二条路没了"）：
  `git grep -c withParameterOfSelectedEntry -- src` = **0**
  builder_call.cpp 那段解释注释从两份变一份（`git grep -c 'hands its arguments one signature only when'` = **1**）
```

## 7. 判据与基线（这一页写完之后才动代码）

```
改前基线（2026-11-28 晚，同一把尺子，池位树 mm-capacity，`-Jobs 2`、`-QueueOnTreeLock`、`-SlotWaitSeconds 900`，一次一组）：
  smoke        total 10   / pass 10   / fail 0   （BUILD_EXIT=0）
  feat/compile total 2741 / pass 2725 / fail 13  （known 3）
  feat/modules total 2100 / pass 2072 / fail 20  （skip 4、known 4）
  红名 13 + 20 逐条都在 `internal/reports/2026-11-28-promotion-86-reds.tsv`，**甲 0**
建枝要重取：队列会动（`graph-kinds-renumber` 一合会消掉 feat/modules 两条红），**不拿这张表当改前**。
改后的判据：同样三组、逐名比新表（不是 86 表），新增红 0；`feat/compile` 与 `feat/modules` 两组都必须报
  ——调用解析与类型检查改动只跑自己关心的计划集不够（skill 里两次栽在这条上）。
```

## 8. 落笔次序（先说好，再一次改完）

```
1. 先写三张脸的回归用例（不动实现）：普通实参 lambda、泛型体里 `[] as U[]`、`foreach` over `Range`
   —— 前两条今天应是红的，第三条今天应是绿的（护栏）。这一步只加用例，不改实现。
2. 再改次序，一次改完：`#1` 收集 → 逐位置取**被选中条目**的形参（`#5` 判据）→ `#8` 解变量 →
   `#9` 拿算子 id；同时删 `#2`/`#3`/`#4` 那刀/`#6` 整条，与 builder_call.cpp:717-727 的重复注释。
3. 删的读数：`withParameterOfSelectedEntry` = 0、重复注释 = 1 份、GCT 那份诊断删掉后
   `feat/compile` 的诊断用例逐字不变（23 条双层诊断里与本件有关的那几条）。
4. 每一步都跑 §7 的三组，逐名比。**不先改断言**；用例的期望只按设计原则 5.9 写"正确答案"。
```

## 9. 未决（交决策者）

```
· `#2` 删除后，`with` 实参（`builder_with.cpp:124`）与普通实参共用的"位置类型"是否也在同一处算完，
  还是 `with` 列表另有一次机会（今天 :118-125 的注释说 with 的形参类型"由调用本身完成"）—— 两次机会会留第二条口径，
  我按"一处算完"写在上面的次序里；若语言作者要求 with 列表保留独立机会，这一条要改。
· ② 的检查应该落在 `visitCastNode`（不让集合类型绕开那条分支）还是落在"字面量按位置类型构造"那一层
  （让 `[] as U[]` 在 U 还没具体化时就保留为待定字面量）—— 两条都能让用例过，差别是**谁记得"这个字面量还没定型"**；
  前者不改数据模型，后者要在字面量上留待定状态。倾向前者（不在数据上加待定态），落笔前请确认。
```