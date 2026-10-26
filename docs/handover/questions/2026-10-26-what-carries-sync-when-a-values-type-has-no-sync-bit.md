# 类型上没有 `Sync` 位时，Q9 那句"值的函数类型带 `sync`"该落实成什么？

**问谁**：语言作者（负责人）
**问什么**：`f<w>` 的 `var` 上下文参数那条裁定（Q9）写着"部分应用的值其**函数类型带 `sync`**"，但今天量到的实现事实是：**具名 `func` 的 `sync` 是函数体块的修饰**（`src/parse/syntax/parser.cpp:1019`），**值的类型上没有 Sync 位**；效果来自函数体派生的 effectful 图，资源签名是 `Generic`。所以这句话按字面**做不出来**，而我们又不敢把要求删掉了事——请您定它应当落实成什么。

## 量到的事实（一次运行、stderr 空、无任何诊断）

1. 把 `add<t>`（函数体里写 `t`，`t` 是闭包捕获的单元）传进**函数类型形参**、在**非 `sync` 体**里丢掉结果：

   ```
   func apply(f: (x: int) => int): int { f(1); return 0 }
   let k = some<var t>(...)      // 写 t 的那个值的部分应用
   apply(k)                      // 结果不取
   ```
   ⇒ **调用没跑，`t = 0`，静默丢失**（连诊断也没有）。

2. 同一个实参传进**同形的 `sync` 体** ⇒ 跑了，`t = 1`。
3. **直接调用** `k(1)` ⇒ 跑了，`t = 2`。
4. `pure` 只是**声明子句**，写不到函数类型上：`func applyPure(f: (x: int) => int): int pure { return f(1) }` 编译通过并打印 `2`——**调用方今天无法用形参要求纯度**，`pure` 也不会因为"调用了形参"被判违反。

⇒ 结论：**直接调用看得见效果（函数体派生的图），经函数类型形参看不见**；Q9 要的"值的类型带 `sync`"补的正是这个缺口——但"类型"这一层今天没有承载它的位置。

## 需要您定的

- **甲**：给函数类型（或函数值）加一个**效果/资源摘要位**去承载这件事（例如把"写捕获"写进值的资源签名，而不是留 `Generic`），调用点据此判定——那么"把带写的值传给不 `sync` 的形参"应当**被拒**还是**照旧静默**？
- **乙**：不加类型位，改成**调用点的图规则**：调用函数类型形参时**按值的实际形状**决定要不要接链/算一次写（即效果由值所指的图决定，不由类型决定）。这样"静默丢失"就消失，但"调用方不能从类型上知道"这点保留。
- **丙**：别的划法（例如只要求 `var` 上下文参数这条路的**直接调用**可见，经形参那条路**不算**效果，因为它本来就不该被这样用——若如此，请说明应当**拒**这种传法还是**允许但不管**）。

## 现在的状态（不自行决定）

- 计划里那句已被改成**如实描述**（效果由函数体派生的 effectful 图承担、具名 `func` 的 `sync` 属于函数体块、值的类型没有 Sync 位、资源签名是泛型），**没有**把要求留下来，也没有实现替代机制。
- 改前逐字：`…so the calls accumulate. sync stands on the value's type because calling it writes the captured variable, and the function the value makes writes the capture the way the forwarder does.`
- 改后逐字：`…so the calls accumulate. The write is carried by the graph the body derives -- a call of the value is recorded as a write of the captured cell -- and not by a sync modifier on the value's type: on a named func the sync belongs to the body block, so the value's type has no Sync bit and its resource signature is generic. What the reads after the calls show is that the write landed; a caller sees it when it calls the value itself, and not through a parameter of function type.`
- 全树只有这一处在宣称"类型带 sync"（`test/plans/feat/consistency/partial_application.plan.toml` 的 `var_parameter` 描述）；`test/cases/consistency/partial_application/var_parameter.cml` 的头注释与本批新用例的描述都没有这句。

## 证据位置（都在 develop 上）

- 值的绑定与调用：本批 `f<w>` 绑 `var` 上下文参数＝绑那个单元本身、调用算一次写（`test/cases/consistency/partial_application/var_parameter.cml`、`var_parameter_effect.cml` 把"`sync` 块内运行、块外不运行"钉成 `1 0`，四后端一致）。
- `sync` 是函数体块的修饰：`src/parse/syntax/parser.cpp:1019`。
- 资源签名：`Generic`（未写子句的函数值按其推断/泛型口径，见同一天的《资源承诺的检查与 `when` 求值》一节）。