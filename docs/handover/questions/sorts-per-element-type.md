# 排序族按元素类型分臂：交给语言作者的问题（`handover/h393-sorts-per-element`）

本文件是裁决请求，不是实现说明。实现者已经按裁定原文
（`docs/handover/reviews/questions-2026-10-23.md` 第 369 行起「复核：归约按元素类型分臂」，与第 288 行起
「impl-declaration-arms」）把 `array` 的排序族改成"一个元素类型一条 body-less 声明、一条声明一个算子名"，
并删掉了运行期按元素类型码分支的 `switch (codeOf(...))`。落地过程中碰到三处裁定原文没有覆盖的空白，
每一处都只能由语言作者裁决；实现者按下面的"建议"已落地，并把实际做了什么写进同分支的实现报告。
每一条按「问题 / 选项与代价 / 建议 / 依据」写。

---

## 问题 1：body-less 声明的 `when` 在**调用处**不被使用，算缺陷还是算有意？

### 现象（逐字证据）

两个同名、`when` 互斥的 body-less 声明，各自带自己的 `#op`；同一个调用换个书写顺序，编译到的算子就跟着换：

`probe_when_order_int_first.cml`（整数臂在前，调用 `float[]`，只有第二条声明接受它）：

```cml
#op<':op/add_i'>
func probe_kernel::<T>(xs: T[]): T when is_integer::<T>()

#op<':op/add_l'>
func probe_kernel::<T>(xs: T[]): T when is_float::<T>()
```

`camel probe_when_order_int_first.cml std::gir` 给出：

```
N01 [label=":op/add_i", type="OPER", value_type="float"]
```

`probe_when_order_float_first.cml`（两条声明对调，调用 `int[]`，只有 `is_integer` 那条接受它）给出：

```
N01 [label=":op/add_l", type="OPER", value_type="int"]
```

两次都是"先写的那条赢"，`when` 没有被读。算子的极性由声明顺序决定，不由条件决定。

### 代码位置

- `OperatorGroup::resolveForCall`（`include/camel/core/operator.h:153-160`）：把实参类型交给一条规则去解析
  （`include/camel/core/type/resolver.h:80-94` 的 `resolveWith` / `resolveForCall`），规则只按类型接受与否
  回答，任何一条规则都不看 `when`；组按声明顺序取第一个接受的。
- 调用点的候选筛选读的是 `whenClauses_`（`src/compile/gir/builder_operators.cpp:2049-2084`），而
  `whenClauses_` 只对**有函数体**的函数登记（`src/compile/gct/tree.cpp:161`；
  `src/compile/gir/builder.cpp:653, 724`）。body-less 声明在 `visitDeclNode` 里只走
  `declareNativeOperator`，没有图、不在 `whenClauses_` 里，于是它根本不进那个循环。
- body-less 声明的条件存在 `Builder::nativeRuleConditions_` 里，只在两处被读：`#impl` 采纳内核
  （`src/compile/gir/builder_modules.cpp:428, 579-637, 754-755`）与声明期的重叠检查
  （`WhenDeclarationOverlap`）。

### 影响

裁定里"同名声明靠互斥的 `when` 分臂就是重载"这一条，对**有函数体**的函数成立（图里筛选），对
**body-less 原生声明**目前只在声明期成立（重叠会被 `WhenDeclarationOverlap` 拒绝），调用期不成立。
归约族的 `sum_kernel` 之所以正确，是因为它走 `#impl` + `declaredRuleApplies`，而不是走调用点的重载解析。
换句话说：今天写"一族一臂"的原生算子，靠 `when` 是**不成立**的，只能靠固定签名逐一列出。

### 选项与代价

1. **算缺陷，让调用处也读 body-less 声明的 `when`**：把 `nativeRuleConditions_` 纳入 `resolveForCall`
   的候选筛选。代价：算子组解析在编译热路径上；要区分"条件不满足"与"类型不匹配"两种拒绝（诊断文案
   与 `NoMatchingFunction` 的候选列表都要动）；`when` 的求值需要实例化上下文，泛型声明还要处理
   未定类型变量；所有模块的解析结果都可能变化，需要整层回归。
2. **算有意，维持现状**（`when` 只用于 `#impl` 采纳与重叠检查）：代价是"一族多类型一臂"的原生算子
   只能写成固定签名（本分支的选择），或者等 1 落地后才好写。
3. **折中**：调用处把 `when` 当候选优先级（条件成立的先试），现状作兜底。代价：同一份程序在不同
   候选交集下的行为仍取决于书写顺序，语义不好描述。

### 建议

先按 2 记档（本分支即按固定签名落地，写法不依赖 `when`，因此 1 若将来落地，本分支仍然合法，只是不再
必要）。但请裁决**调用点是否该读 `when`**；若判为 1，本分支的固定签名写法应保留为"原生算子分派"的
推荐写法，直到 1 实现。

### 依据

`include/camel/core/operator.h:153-160`；`include/camel/core/type/resolver.h:80-94`；
`src/compile/gir/builder_operators.cpp:2049-2084`（候选筛选只读 `whenClauses_`）；
`src/compile/gct/tree.cpp:161`、`src/compile/gir/builder.cpp:653, 724`（`whenClauses_` 只登记有体的函数）；
`src/compile/gir/builder_modules.cpp:428, 579-637, 754-755`（`nativeRuleConditions_` 的两处读）；
探针 `/tmp/h393probe/probe_when_order_int_first.cml`、
`/tmp/h393probe/probe_when_order_float_first.cml`（正文逐字给出，两者互为对照）。

---

## 问题 2：排序族没有 Camel 函数体，没有声明的元素类型只能在调用处被拒绝——这是要的最终形态吗？

### 现状

- 排序族（`sort` / `insert_sort` / `quick_sort` / `merge_sort` / `merge_sorted_arrays`）各有 **i32 / int /
  f32 / float** 四条 body-less 声明，每条自带算子名（`array:sort_i`、`array:sort_d`，……）。
- `argsort` 另有自己原有的六个键元素类型（i32 / int / f32 / float / bool / string），本分支未增未减。
- **bf16 / f16**：编译期 `SemanticDiag::ReducedPrecisionUnsupported`（"The call of 'sort' cannot take a
  value of the reduced-precision element type 'f16'."），带"先读成 f32"的 help。
- **bool / string / c64 等**：编译期 `SemanticDiag::NoMatchingFunction`，消息列出那四条声明。

### 与归约族的差别

归约族的 `sum`/`min`/`max` 是模块自己写的 Camel **函数体**，内核是它的实例，所以"没有臂"的元素类型由
函数体回答（裁定原文：「要么有自己的臂，要么在编译期就没有匹配的内核而走函数体」）。排序族**没有函数体**：
那二十几条 body-less 声明就是算子本身，"没有声明"就只能在调用处被拒绝——本分支的
`reduced_sort_refused`、`array_sort_no_declaration` 两个负例钉住的正是这一点。裁定要求的"没有声明的元素
类型回到函数体"在排序族**字面上无法成立**，只能做成语义上最接近的"按名字的编译期拒绝"。

### 选项与代价

1. **保持编译期拒绝**（本分支现状）：`sort(bool[])` 等从"运行期 fault"变成"编译期错误"（诊断码随之
   改变），bf16/f16 仍然排不了。代价：行为变化本身要被接受；好处是不再有运行期元素类型分支。
2. **给 bf16/f16 各一条声明 + 执行器**：代价：要定义比较语义（比较当前是元素类型自己的 `<`；
   归约族的 bf16/f16 走 IEEE 754-2019 的加宽规则，见 `modules/math/minmax.h`）与槽宽（2 字节）；
   是否加宽到 f32/f64 再比、`-0`/NaN 的次序都要写清楚，否则会与 `min`/`max` 的既有语义不一致。
3. **给 bool/string 各一条声明 + 执行器**：代价：`string[]` 重排要写屏障（`Array::set` 只有知道元素
   类型才走屏障），是新的 C++ 工作；`bool`/`string` 的序要与 `min_b`/`min_s`（按字节）一致。
4. **给排序族写 Camel 函数体兜底**（照归约族的形状）：代价：Camel 里没有对任意 `T` 的 `<`，等于要
   发明"任意元素类型的排序语义"；超出本分支范围。

### 建议

本分支保持 1。若要 2 或 3，请同时给出比较语义（bf16/f16 是否加宽、string 是否按字节、是否要写屏障）
与是否要新增元素类型；本分支可以另开一条车道做，不影响已落地的分臂结构。

### 依据

裁定原文第 369 行起；`stdlib/array.cml` 的新声明段；`src/builtin/operators/array.cpp`（比较用
`numberLess`/`keyLess`，即元素类型自己的 `<`）；`modules/math/minmax.h`（归约族的比较与加宽）；
`test/plans/feat/modules/array_sort_kernels.plan.toml` 的两个负例；
`test/plans/feat/modules/stdlib_reduced.plan.toml` 的 `reduced_sort_refused`。

---

## 问题 3：编译器引入的 in-place 形能不能按元素类型改名？

### 现状与原因

- in-place 形由 `OperatorTable::setInPlaceForm(uri, uri + "_in_place")` 建立；`v = v->sort` 在
  `Builder::inPlaceRebindingOf` 里查 `inPlaceForm(target->uri())`；运行期由
  `ExecutorManager::find(uri)` **按 URI 找执行器**。
- 一个执行器只吃一个元素类型。所以共享的 `array:sort_in_place` 必然要在运行期读元素类型码才能在
  i32/int/f32/float 之间选执行器——**正是本分支要删的那个 switch**。
- 因此本分支把 in-place 形也按元素类型起名（`array:sort_i_in_place`、`array:insert_sort_d_in_place`，……），
  并改了钉住旧名字的两个计划：`test/plans/feat/modules/resources.plan.toml:62` 与
  `test/plans/feat/consistency/new_values.plan.toml:341`。
- 这与派工单里"`array:sort_in_place` 这个名字已经被别的计划钉住，别动它"冲突；实现者按"删掉运行期按
  元素类型分支的 switch"这条更硬的要求取舍，并在此记档。

### 选项与代价

1. **接受改名**（现状）：in-place 形与返回形同名同后缀一一对应。代价：两处计划钉子被改；in-place URI
   会出现在 `std::profile`、GIR、资源报告里，若有外部工具按旧名匹配需要同步。
2. **保留共享名，执行器内部读元素类型码**：代价：本分支的核心要求（删 switch）不成立。
3. **保留共享名，加一层按元素类型分派的 in-place 执行器**：等价于 2，仍要运行期读码。
4. **保留共享名，改 `OperatorTable` 的映射形状**（在 URI 之外多一个元素类型维度）：代价：新机制，
   超出本分支范围，且 in-place 形本来就是编译器生成、程序从不书写的名字。

### 建议

取 1。`array:sort_in_place` 这类名字只由编译器生成，程序里写不出来（程序写 `v = v->sort`），改名不改
任何程序的语义，只是把"元素类型"从运行期挪进名字；若语言作者要保留旧名，请指明由谁承担那个运行期分支。

### 依据

`src/core/operator_table.cpp:238, 244`（`setInPlaceForm` / `inPlaceForm`）；
`src/compile/gir/builder_operators.cpp:933, 976, 2440`（`Builder::inPlaceRebindingOf` 与两处前提检查）；
`include/camel/core/operator.h` 的执行器查找；`src/builtin/operators/array.cpp:561`（本分支的注册）；
`test/plans/feat/modules/resources.plan.toml:62`；`test/plans/feat/consistency/new_values.plan.toml:341`。
另外 `docs/design/graph-store/plan-roundtrip.cn.md:54,69` 把旧名当作实测 URI 写在正文里（本分支按车道规则
未改 `docs/design/**`，留给裁决者决定是否同步）。

---

## 附：本分支没有做的事

- 没有给 bf16/f16/bool/string/c64 增加排序声明或执行器（问题 2）。
- 没有动 `argsort` 的元素类型集合（仍为 i32/int/f32/float/bool/string）。
- 没有改调用点的重载解析（问题 1），也没有改 `#impl` 采纳时的 `when` 语义。
- 没有改 `flatten`、`unique`、归约族（`sum`/`min`/`max`/……）的任何声明或执行器。

---

## 追加（`handover/h399-when-at-call`，2026-10-24）：问题 1 按已有裁定落地，调用点读 `when`

本节只追加，不改上文一行。结论：**问题 1 不需要新的语言裁定**。它是"代码与原则或已有裁定不一致"的实施缺口，
按 `docs/handover/reviews/questions-2026-10-23.md` 第 384 行已有的做法（"协调者直接按原则改，不必走问题通道"）
直接改代码。上文"问题 1 / 选项 1"即本车道落地的形态，且已落地；上文问题 3 的 in-place 名与问题 2 的
bf16/f16/bool 拒绝都保持 h393 的现状，未动。

### 依据（三处，都在本仓库）

1. `docs/design-principles.cn.md` 4.4「适用条件 `when`」（第 272 行，规范性）："条件在实例化时求值；不成立时
   **这个重载不适用**，重载选择去试同名的其他候选；没有一个适用时**在调用处报错，列出每个候选没满足的条件**。"
   重载选择就发生在调用处，所以调用点必须读 `when`——不读，这句里的"不适用"与"报错列出条件"都无处成立。
   这是**写明了的语义**，不是新语义。
2. 语言作者 2026-10-24 对 `impl-declaration-arms` 的裁定（同文件第 288–294 行）："**选 1。**……`when` 是规则、
   函数重载与 `match` 守卫唯一的适用条件——同名声明按 `when` 互斥分臂就是重载，`signaturesCanAgree` 忽略
   `when` 是缺陷"；"**规则统一**：同一检查适用于所有带 `when` 的同名重载（有函数体的普通函数也一样），不是
   `#impl` 声明的特例。" 该裁定改的是**声明期**检查（`WhenDeclarationOverlap`），而调用期仍是"先写的那条赢"：
   这正是同一条裁定的实施缺口。
3. 同文件第 384 行：「都是已有裁定的实施缺口，不是新的设计决定。以后同类问题（"代码与原则或已有裁定不一致，
   按原则改代码"），协调者直接按原则改，不必走问题通道。」

### 最小复现（两次输出逐字，均在 `origin/develop` 基线 `cf813dfd0` 上量）

同一程序、两条同名 body-less 声明、`when` 互斥，只把两条声明的**书写顺序**对调：
`test/cases/compile/native/when_order_at_call_i32_first.cml`（i32 臂在前）与
`test/cases/compile/native/when_order_at_call_int_first.cml`（int 臂在前）。i32 臂是减法、int 臂是加法，
所以两行数字自己说出每次调用走到了哪个算子。

改前（`camel <case>`，两个文件各一次）：

```
i32_first:  i32=-4    int=4294967292      # 两行都是 :op/sub_i；int 那行把 i32 减法跑在 int 上（回绕）
int_first:  i32=42    int=42              # 两行都是 :op/add_l；i32 那行把 int 加法跑在 i32 上
```

改前的 `std::gir`（逐字，`value_type` 与算子不符）：

```
i32_first: N02 [label=":op/sub_i", ... value_type="i32"];   N08 [label=":op/sub_i", ... value_type="int"]
int_first: N02 [label=":op/add_l", ... value_type="i32"];   N08 [label=":op/add_l", ... value_type="int"]
```

改后（同一棵树、同一命令）：

```
i32_first:  i32=-4    int=42        exit=0
int_first:  i32=-4    int=42        exit=0     # 两次逐字相同
i32_first: N02 [label=":op/sub_i", ... value_type="i32"];   N08 [label=":op/add_l", ... value_type="int"]
int_first: N02 [label=":op/sub_i", ... value_type="i32"];   N08 [label=":op/add_l", ... value_type="int"]
```

哪一次是对的：**改后**。依据是设计原则 4.4 的第 272 行（`when` 不成立即"这个重载不适用"）与第 288–290 行的裁定
（同名声明按 `when` 互斥分臂就是重载）。改前的两次都不是"对的"，它们是同一个缺陷的两个投影：被选中的算子只由
书写顺序决定，条件根本没被读，于是出现 `value_type="int"` 编到 `:op/sub_i` 这种类型与算子不符的节点，而且
**不报错**（`4294967292` 是 i32 回绕，不是拒绝）。

不适用时的调用点诊断（改后，逐字）：

```
error[SemanticDiag::WhenConditionUnmet]: No candidate of 'arms_kernel' applies to (<> (float, float)):
every candidate's 'when' condition is false. Candidates: (a: T, b: T) => T — when is_arm_i32::<T>();
(a: T, b: T) => T — when is_arm_int::<T>()
```

（改前同一次调用编译通过并编到 `:op/sub_i`、`value_type="float"`——即上文探针里的形态。）

### 落地（`src/compile/gir/`）

- `Builder::ruleVerdictForCall`（`builder_modules.cpp`）：把 `declaredRuleApplies` 的问法拆成三答案
  `Applies / ConditionUnmet / NotThisRule`；`declaredRuleApplies` 改为其中一个答案，`#impl` 采纳路径的行为不变
  （那两处调用者都已先检查参数个数与签名，`NotThisRule` 不可能出现）。**归约族走的就是这条路径，未改。**
- `Builder::resolveGroupedCall`（`builder_operators.cpp`）：调用点逐条试候选——"签名收下这组实参 →再问它自己的
  `when`"；条件为假的不是候选，调用继续试下一条。`visitLinkNode`（写出来的调用）与 `overloadValueFor`（算子名当
  值用、期望类型已知）都走它。顺序仍在，但只在**都适用**时才是兜底。
- 一条都不适用时报已有的 `SemanticDiag::WhenConditionUnmet`，候选逐条列出。列表**不能按签名去重**：两条臂的签名
  文本相同（`(a: T, b: T) => T`），只有 `when` 不同；本车道第一版按签名字典去重，把两条压成了一条——探针查出来的，
  现按"签名 + 条件"成对去重。
- 回归：两个用例文件 + `test/plans/feat/compile/native_declarations.plan.toml` 三条（两次行为、一次 `std::gir` 钉
  `:op/sub_i`=i32 与 `:op/add_l`=int，并断言不存在"算子与元素类型不符"的节点）。

### 一处仍未决，本车道没有自己定：两条 `when` 对同一次调用**同时成立**时取谁

调用点现在按"算子组给出的顺序"取第一条适用的规则（`exportNativeOperators` 的 `stable_sort`：固定签名在前，
其后是书写顺序）。设计原则 4.4 只写了"不成立时不适用"，没写"都成立时取谁"；**请作者裁定**：

- **选项 A（现状：书写顺序兜底）**。代价：声明期的 `WhenDeclarationOverlap` 只对编译器**登记**的类型逐个求两条
  `when`（基本类型 + 每种复合形状一个样本），一个没抽样到的类型（例如用户结构体）可以同时满足两条，这时调用点又
  回到"先写的赢"。语义不好描述。
- **选项 B（调用点报歧义）**。像同名函数那样报 `rejectAmbiguousCall`（`builder_operators.cpp` 已有）。代价：无条件
  的重载（`fixedSignature()==nullptr` 的计算签名）今天也允许"两条都收下、先写的赢"，一律报错会动到这一大片既有
  行为，超出本缺陷范围。
- **选项 C（声明期检查用封闭类型集）**。把"程序里出现的类型"并进互斥检查（`check` 的 `program` 参数已有先例）。
  代价：仍然是编译期近似，且要新接口。
- **建议**：若判 B，只对"带 `when` 的候选"报歧义，无条件的既有重叠留给单独一条车道。

另一处**继承**的行为（不是本车道新定）：条件在调用点问不出来时（例如读一个 `with` 值），
`reportWhenProblem` 按既有约定把"声明的条件本身"报在声明处，不当作适用。`declaredRuleApplies` 一直如此。

### 本车道没有做的事

- 没有动 h393 已落地的排序族写法（仍是一元素类型一条固定签名声明），也没有给它补 bf16/f16/bool/string/c64。
- 没有动 `#impl` 采纳（`sum`/`min`/`max` 的臂）、`std::opt` 的规则解析、`OperatorGroup::resolve`/`resolveForCall`
  本身（核心不读 `when`，筛选在编译器的调用点做）。
- 没有改声明期检查（`WhenDeclarationOverlap` / `separateWhenConditions`）与 `signaturesCanAgree`。
- 没有裁定"两条 `when` 同时成立"的优先规则（见上一节）。