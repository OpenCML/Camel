# 泛型声明处的名字解析：落地范围与四处留给作者的边界

Author: 车道 `handover/h427-decl-names`（`D:\Projects\Camel-wt\h427-decl-names`），2026-10-24。Status: open —— 四处边界需要作者裁定（下面「要作者定的」）。裁定来源：`docs/handover/reviews/questions-2026-10-23.md` §H401 三份 → `### generic-rule-condition-unresolved-name`（532–538 行），以及它引用的 `### generic-rule-instantiation-set-and-predicate`（277 行）§1。实测都在 Linux 主机（`88zjwei`）上、同一构建窗口内，走 `camel-remote.ps1` 与计划运行器。

## 一句话

「拼错的谓词就是一个未解析的名字，和别处一样在声明处报错，模块不加载」这一半落地了：泛型函数与泛型规则的**体**里、不依赖类型参数的名字在**声明处**解析，解析不了报 `SemanticDiag::UnresolvedReference`、报在声明那一行、退出 1；依赖类型参数的检查仍留在实例化。

## 落了什么（逐字可复现）

- 泛型规则：`rule add_zero::<T>(x: T) when is_integerr::<T>() { x + 0 => x }`，规则从未被应用 → 改前 stdout `1`、退出 0，模块照常加载；改后 `err_when_unknown_name.cml:15:31 [Error]: Unresolved reference: 'is_integerr'`、`camel: 1 error`、退出 1。用例 `test/cases/modules/rules/err_when_unknown_name.cml`，钉子 `test/plans/feat/modules/rules.plan.toml` 的 `rules.when_predicate_misspelled`。
- 为什么规则的 `when` 天然走「体」这条路：规则脱糖时条件被写成 `name__when` 这个函数的**体**（`src/parse/ast/builder.cpp:1038-1104`），不是 FUNC 的 `when` 子句。所以「和别处一样」在规则上就是「和函数体一样」，报 `UnresolvedReference`。
- 泛型函数体：`func never_called::<T>(x: T): T { return x + no_such_name }`，从未被调用 → 改前 stdout `ok`、退出 0；改后 `err_decl_name.cml:10:16 [Error]: Unresolved reference: 'no_such_name'`、退出 1。钉子 `compile/generics.plan.toml` 的 `decl_name_error`；对照组 `body_error`（`err_body.cml` 的 `x + x` 配 `[1]`）逐字不变——`NoMatchingFunction` 仍在实例化时报、仍带 `in the instance 'double<int[]>' ... instantiated by the call`。
- 未实例化就不做类型检查这一半保留，但**用例的写法要诚实**：`test/cases/compile/generics/unused_body.cml` 原来的体是 `x + no_such_name`（名字错），现在改成 `x + x`（依赖类型参数、只在实例化时报），意图不变、名字不再混在里面。
- 渲染侧新增一条：`render.sem_generic_decl_name`（golden `test/plans/feat/compile/golden/diag_render/sem_generic_decl_name.txt`）——声明处一条、无实例注记；`render.sem_generic_instance` 的 golden 重生成，只剩两条带实例注记的 `NoMatchingFunction`，该测试「实例注记会被渲染」的目的没丢（`json.related_information`、`lsp.code_and_related` 两个硬编码行号也因此不用动）。

## 实现期实测到的三处**假阳性**（不修就是缺陷，已修）

1. `VARI` 节点**没有名字**：`GCT::Tree::makeVari` 不写 payload，而 `GCT::Node::name()` 的契约只允许 DECL/FUNC/NREF/DREF（`include/camel/compile/gct/node.h:136-142`，`ASSERT` 在 release 里编译掉，于是走到 `*payload_.name` 的越界读）。原来的 `walkStaticBodyNames` 在 VARI 分支读了 `node->name()`，也就是说 **`match type` 的未选中分支里只要出现 `var x = ...` 就会崩**（`resolveStaticBodyNames` 走的就是那个分支）。本车道顺带修掉：VARI 只递归子节点，名字由它的 NREF 子节点绑定。实测：`func bump::<T>(var x: T, d: T): void { x = x + d }` 改前 SIGSEGV（`features.cml`、`effects.cml`、`indices`、`static_loop` 等 6 个计划用例一起红），改后全绿。
2. 运行期 `loop` 的状态名：`stdlib/array.cml:188` 的 `return loop hit = false for x in xs while !hit do pred(x)`。`walkStaticBodyNames` 原来只认识 `static loop`（SLOOP），不认识 LOOP → `hit` 被判未解析。
3. 被**元组模式**解构的形参：`stdlib/array.cml:300` 的 `zip(xs, ys)->map<((a, b)) => f(a, b)>`。名字在 `GCT::Tree::tupleParamsOf`（`include/camel/compile/gct/tree.h:224-231`）里，不在 `FunctionType::argNameAt` 里 → `a`、`b` 被判未解析。

顺带补齐的绑定：嵌套函数（lambda、体内声明的函数）的形参、`with`/`constas`/rest 形参、`case` 模式绑定的名字（`case v`、`case error e`、`case Shape::Rect { w, h }`）、`match type` 各分支、`static for`/`static loop`。字段访问是 `ACCS`（不是 `DREF`），所以字段名天然不进这条检查。

## 要作者定的

1. **前向引用算不算「声明处可见」？** 顶层值是按依赖序构建的（`program.cpp` 的 `buildTopLevelValueNamed`），函数是按需构建的，所以「写在后面」不影响可用性：`test/cases/compile/generics/features.cml` 里 `add_base::<T>` 读后声明的 `let base = 100`，今天编译得过。我的检查因此对**本模块源码里出现过的任何名字**（FUNC/DECL/NREF，含 `a::b`）都不报，宁可放过也不假报。代价要说清：把 `no_such_name` 拼成**模块里另一个真名字**（例如 `nope` 拼成 `node`）时，声明处查不出来，仍要等实例化那一次调用点报错。要裁的是：声明处的名字检查该按「整模块一个作用域」（现在的做法）还是按「词法顺序 + 依赖序」（更严、也更接近 C++ 两阶段查找的字面含义）。
2. **未导入的谓词该报哪一条？** `func describe::<T>(x: T): string when is_compound::<T>()`，`is_compound` 声明在**没导入**的模块里：现在（和裁定前一样）报 `WhenPredicateNotImported`，点名 `when_import_predicates`；唯一变化是**位置从实例化提前到声明处**。也就是说我的实现把条件上的名字错分成了两半：「不是任何名字」的（规则的 `when` 走体）归 `UnresolvedReference`，「是名字但声明处看不见」的归 `when` 自己的那几条报告（`WhenPredicateNotImported` / `WhenPredicateNotCompileTime` / `WhenUnknownName` / `WhenConditionNotBool`）。逐字的两条实测（`/tmp/h427/`）：

   - 规则：`rule add_zero::<T>(x: T) when is_integerr::<T>() { x + 0 => x }` → `error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'is_integerr'.` @ `5:31`，退出 1。
   - 泛型函数的 `when` 子句：`func only_int::<T>(x: T): T when is_integerr::<T>() { return x }`（从未被调用）→ `error[SemanticDiag::WhenConditionNotConstant]: The 'when' condition of 'only_int' cannot be evaluated when the declaration is instantiated: no compile-time function of that name is visible here; ...` @ `5:50`，退出 1。

   两条都是「声明处、模块不加载」，但**报告码不同**：前者和函数体一样，后者和非泛型函数在声明处已有的那条一样（`src/compile/gir/builder.cpp:730-735`，`func bad(x: int): int when is_positive(1)` 早就是这样报的）。要统一成一条请作者定；我保留现状是因为 `test/plans/feat/compile/when_import.plan.toml` 明确写了「rather than calling the name unknown」，而规则的 `when` 在脱糖后确实就是函数体。**顺带一条既有文案债**：`WhenConditionNotConstant` 的正文写「cannot be evaluated when the declaration is instantiated」、help 写「checked at instantiation」，但非泛型声明今天就在声明处报它（本车道把泛型声明也拉齐到同一处），文案与位置对不上；改文案会碰到 `test/plans/feat/compile/when*.plan.toml` 的断言，本车道没动。
3. **类型名不在本次检查里。** `func f::<T>(x: T): T { let y: Missing = 0; ... }`（`Missing` 是未声明的**类型**）在声明处不报，仍留到实例化（`UnresolvedTypeReference` 一侧）：类型节点不走值名字遍历，而裁定说的是「名字」（谓词/普通名）。若「不依赖类型参数的名字」按字面也含类型名，需要另开一条（TYPE 节点的名字解析，连结构体字面量的字段类型一起），影响面比本次大。
4. **`when` 的另外两半。** (i) 泛型声明**自己的** `when` 我按「非泛型声明在声明处会报的那一半」报了（不是 bool、不是编译期表达式、两个导入同名、不是编译期函数、`when_predicate_named` 找得到声明模块），但**类型变量开放**导致的 `WhenTypeQueryNotConcrete` 仍留实例化——否则 `when is_numeric::<T>()` 这种（`type_predicates.cml`、`applied.cml:33` 等十余处）会在声明处全炸。(ii) **嵌套函数自己的 `when`** 没走这条路（只有被检查的那个声明自己的 `when`；嵌套函数的 `when` 仍只在它的块被构建时报，也就是本泛型体被实例化时）。若要一律前移，(ii) 是纯增量、(i) 需要先给「开放类型参数上的查询」定一条声明处语义。