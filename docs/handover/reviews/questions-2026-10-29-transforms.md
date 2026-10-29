# 程序变换（第 1 步）留下的两个问题

施工分支：`lane-transforms`（程序变换第 2–5 步的第 1 步：`#jvp`/`#transpose`/`#vjp` 按结构读出 + 标量规则）。
本页只写两件需要作者定的事，每件都给出**现在的行为**（含实测诊断）、**为什么卡住**、**两种做法的代价**、**我的倾向与依据**。

---

## 问题 1：`rule` 作为标签实参怎么绑定

### 现在的行为

设计稿写的是把规则挂在原语的声明上（`docs/design/program-transforms/README.cn.md` 第 3 节）：

```camel
#jvp(mul_jvp) #transpose(mul_t)
func mul::<T>(x: T, w: T): T when is_float::<T>()

rule mul_jvp::<T>(x: T, w: T) when is_float::<T>() {
    x * w => (dx: T, dw: T) => dx * w + x * dw
}
```

今天这样写不通。实测（`out/latest`，本分支）：

```
error[SemanticDiag::TagArgumentType]: The parameter 'rule' of the tag 'jvp' is declared 'F',
but the argument is 'mul_jvp' (not a function of the program).
help: A tag argument is a compile-time constant of the kind the parameter declares: ... and the
name of a function for a parameter that carries one (the fact is the graph reference of that function).
```

同一个标签换成函数实参就通：`#jvp(scale_jvp) func scale(x, k)` 能读出规则图（`test/cases/modules/autodiff/transform_rules.cml`，本步的用例）。

### 为什么卡住

标签实参是**编译期常量**，参数类型是 `F` 且约束是 `is_function::<F>()`；`rule` 声明既不是函数、也不是常量——它是一条改写规则（模式 + 右边），今天没有任何"值"能表示它。于是设计稿第 3 节的写法在编译器里差一个实参种类。

### 两种做法

**A. 让标签实参可以是一个规则的名字。** 绑定器已经为函数名做了一件事：取那个函数的 `GraphId` 记成图引用；规则声明的右边本身也是一个图，所以同一条路可以取规则的图（`Decl` 侧多接受一种声明种类，`is_function` 之外加一个 `is_rule` 之类的约束）。标签存下的仍是图引用，`graph::rule_of` 照旧把它交回给遍。
代价：`tag` 的实参种类多一种（词法/解析/绑定/类型检查/`incremental` 的标签事实各一处），`graph::rule_of` 的返回类型要么仍是"规则右边的图"，要么新增一个 `Rule` 句柄把模式也带出来（本步只需要前者）。没有新的语言值类型。
依据：这是设计稿直接写的形状；一条规则本来就是一条声明，把它当标签实参用不需要在语言里再加一个可传的值。

**B. 标签只收函数，规则改成传给遍的规则集值。** 即 README 第 7 节的 `H`：库构造一个规则集值，遍按原语去查它。
代价：得先定 `H` 的值类型与 `Lift`（README 第 7 节列的那条，要求第 3 步前定），还要定"按什么查"（算子 URI？模式？），多一个值类型、多一层传递；
好处：原语那边不用打标签，规则可以按需拼装。
依据：如果作者认为"规则即数据、遍拿到的是一份可组装的规则集"才是长期形状，那 B 是终点，A 只是中继。

### 我的倾向

**A**。理由是问题 2 的落地形状就卡在这里：设计稿把规则挂在**声明**上，而 `#jvp(mul_jvp)` 是它唯一的写法；先把这条实参种类补上，标量原语的规则才有地方挂（并不妨碍以后把 A 的规则集再包成 B 的值——A 是 B 的输入，不是它的替代）。

---

## 问题 2：标量规则长期住在哪里

### 现在的行为

本步把标量一族（加、减、乘、除、取负）的规则写成 `stdlib/autodiff.cml` 里的 10 条 `export rule`，模式直接是算子表达式（`x + w`、`x * w`、`x * c` …），右边是一个具名函数（`add_tangent` 等 10 个 `export func`，都有 `@example`）。遍读到的方式：

- 一个函数自己的规则＝它声明上的标签（`#jvp(rule)` …），按结构读（`graph::declaration_tags` / `rule_of`）；
- 原语的规则＝库里的 `rule` 声明，模式就是那个原语。

这带来两件事：

1. 设计稿第 3 节的形状是"把规则挂到原语的**声明**上"（`#jvp(mul_jvp) func mul…`），而今天标量算术是**内置算子**（`:op/add_l`、`:op/mul_l` …），**没有声明可以挂**。所以本步只能让规则去匹配算子表达式，绕过了标签。
2. `import autodiff` 现在会把 `graph` 与 `rewrite` 一起编译（规则机制在这两个模块里），每个程序的启动前奏因此多 ≈16 ms（`cmo_load graph` 12.5 ms 占大头）。实测数据与用法见 `docs/handover/reports/prelude-cost-of-autodiff-rules.md`，已作为决策者对账清单第 8 条（前奏预编译缓存）的输入。

### 为什么卡住

"规则住在哪里"取决于"规则怎么被找到"：挂标签要求原语有声明（今天没有），规则集值要求 `H` 与 `Lift` 先定（README 第 7 节，第 3 步前）。两条都不通之前，本步的写法是唯一能跑通、能测的写法，但它不是设计稿的形状。

### 两种做法

**A. 维持现状：规则是库里的 `rule` 声明，遍按模式匹配。**
代价：`import autodiff` 的前奏变重（≈16 ms/次，见上）；设计稿第 3 节的标签形状用不上；"缺规则必须拒绝"（原则 9.9）要靠模式匹配的覆盖性来保证（今天靠单测逐族覆盖，不是类型层的保证）。
好处：不阻塞，本步就能测；规则是可读的 Camel，能被规则系统改写。

**B. 给标量原语补声明，按设计稿挂标签。** 即 `func add::<T>(x: T, w: T): T when is_float::<T>()` 这样的声明（执行仍落到 `:op/add_l`，即 `#impl` 那条已存在的"声明 + 内核站点"机制），上面写 `#jvp(add_jvp) #transpose(add_t)`。
代价：需要问题 1 的 A 先落地；需要给十来个标量算子各写一条声明（或是让 `+`、`*` 一类从内置算子改成"库函数 + 内核"），这会动到算子的解析与内联路径；`autodiff` 之外还得决定这些声明住哪个模块（`math`？`autodiff`？）。
好处：这才是设计稿第 3 节的形状；规则跟着声明走，遍不需要模式匹配，缺规则在类型层就看得见；`import autodiff` 不必再拉 `graph`/`rewrite`（规则住哪由声明决定，标量声明可以由变换的遍库自己 import）。

**C. 规则集值（`H`）由库构造，规则按需传给遍。**
代价：README 第 7 节的 `H` 与 `Lift` 必须先定；还要定规则集怎么查（算子 URI、名字、模式）。
好处：`autodiff` 面最小，规则可组合、可裁剪；B 的规则也可以装进 C 的值里。

### 我的倾向

**先 B、后 C**：B 让"规则挂在声明上"这条设计稿的主线先成立（也就是问题 1 的 A 先定），标量的十条规则改写成挂在声明上的标签；C 作为规则来源的进一步抽象，等 `H` 与 `Lift` 定了再叠加（B 的规则可以直接被 C 的规则集收录，不会白做）。
**不建议把 A 当成终点**：它能跑，但把"规则"和"原语的声明"拆开了，而设计稿与原则 9.9 都要求规则跟着原语走。
如果第 3 步之前不能定 `H`/`Lift`，那第 3 步就用 B 的形状（标签 + 规则）推进，规则集值留到第 5 步之后——这是我读 README 第 6、7 节的理解，请作者确认这个次序。