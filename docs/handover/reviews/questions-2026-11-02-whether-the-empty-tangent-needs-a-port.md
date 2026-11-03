# 待裁：`()` 在端口层面怎么表示——沿用"没有槽"，还是要一个真正的空元组端口？

> **一句问句**：一棵提升过的图里，一个**没有切空间**的形参（`int`、`bool`、`string`）在端口上是"少一个端口"，还是"有一个类型为 `()` 的端口"？
>
> **默认建议：沿用"没有槽"**，与 M3 已落地的 `Tangent<bool> = ()` 同一条规则。若这条规则在走图层沿用不了，就**保持在签名层拒绝**，并在页里写明"端口层面 `()` 还没解决"——**两条都可以，不许发明一种新的端口**。

## 今天堵着谁

M4 的数组三步。第 3 步（签名层）已经把 `jvpSignature` / `transposeSignature` 改成问 `tangentSpaceOf`：

```
jvpSignature        (float[], float[]) -> (float, (float[], float[]))
transposeSignature  ((), float[])      -> (knowns, float[])
```

`float[]` 的切空间是它自己，所以它有端口；但一个参数**没有**切空间时，签名层今天的选择是**直接拒绝**：

```
jvpSignature( (xs: float[], n: int) => float )  ->  nullptr        // 没有切空间的那个形参
```

理由写在代码里：把 `()` 当端口塞进运行时图，需要一种空元组端口，而签名层不该发明它。

## 已有的两条读数

1. **M3 的做法是"没有槽"，不是"空端口"**：`Tangent<bool> = ()` 落地时，`contribution=0` 那条读数就是它——一个不产生余切的槽，不是一个装着零的端口。
2. **走图层已经在用同一条规则**（M4 第 4 步这一笔）：提升一条数组函数时，`xs[i]` 的 `i` 没有切空间，规则拿到的是"有切空间的那几个操作数的切向，按它们本来的顺序"，`i` **不占一个槽**：

```
REINTERP jvp function=f graph=64 liftedFrom=55 type=(float[], float[]) => (float, float) pure nodes=6 kinds=6/3 exit=Tuple{chain,tangent} tangent=computed source=ruleset rules=jvp_idx_arr,jvp_mul
```

也就是：**走图层已经答了"没有槽"**，签名层还答"拒绝"。两处答案不一致，这就是这一页要一个裁定把它合起来的地方。

## 三个选项（都不发明新端口）

- **甲（默认建议）**：签名层也照走图层——没有切空间的形参**不给切向端口**，函数照常提升。`(xs: float[], n: int) => float` 的提升签名是 `(float[], float[])`（`n` 只有一个值的端口），规则拿到的切向数与"有切空间的形参"对齐。
- **乙**：签名层保持拒绝，直到有谁来定"空元组端口"。代价是 `f(xs, n)` 这类函数今天不能求导，而走图层其实已经能走。
- **丙**：造一个真正的 `()` 端口。**这一条违反"不许发明新端口"**，列在这里只是为了把它排除。

## 需要作者答的一句话

选甲还是乙。选甲，我就把签名层与走图层对齐（两边都是"没有槽"）；选乙，我就把走图层那条"没有槽"的分支改成"整条拒绝"，并在这页记下差距。
## 2026-11-02 补一句：这一页的结论可能随复核改变

作者 17:30 把"变换挪进前端实例化"这一线**暂停**了（`c8bc3d87f`、`c6b7104ae`、`split_instantiate.cpp`、`transpose` 同类，以及把"实参必须编译期已知"的拒绝推广到更多入口；**已进树的不撤**），理由是 `#macro` + `std::macro` 本身就是"动态构造静态图"的机制，9.1 写明宏阶段做还是运行期做是**实现的选择**。

**17:50 又叠了一层**：负责人否决了"前端变换表 + 调用处拒绝"整套（`fsh-dev` 的 `a5377eebe`），`compile_time.*`、`jvp_instantiate.cpp`、`split_instantiate.cpp`、`staticFunctionArg`、`lifted-at=`/`halves-at=` 正在被撤。所以今天有三件事要分清，别拧成一件：`staticFunctionArg`（被撤）与"这个类型没有切空间"（仍在，见下）是两件事；"实例化那一线被否决"也不等于"这一页的结论已被裁"。**这一页的两种可能都还开着**：签名层与走图层要不要对齐、对齐之后 `()` 走哪条路，都等复核。

**还要分清的一件（别让下一个人读错）**：签名层今天仍然会拒绝一个**没有切空间**的类型（`int`、`string`）——那是"这个类型不能求导"，与**被撤的**那套"实参必须编译期已知"（`staticFunctionArg`、调用处拒绝）**不是一件事**。签名层并没有放行一切：放行的只有"有切空间的类型，切向按 `tangentSpaceOf` 给"。

## 一条今天量出来的边界：库面声明的形状由算子注册决定，不由调用处决定

想给 `:tangent/scatter_add_arr` 一个库面名字 `scatter_add::<T>(xs: T[], i: int, delta: T): T[]` 时，**两处同时红**：

```
调用处  error[SemanticDiag::ImportNameNotExported]: Reference 'scatter_add' is declared as imported but is not exported by its module.
registry [FAIL] registry.signatures_match / registry.result_types_match / registry.parameter_types_match / registry.docs_complete
```

根在这一句（算子自己写的）：`src/builtin/operators/derivative.cpp:386` ——
`Derivative of '{}' expects 3 or 4 inputs (base, index, delta, axis)`：**这个算子取三个或四个操作数，`axis` 可选**，三参声明与注册形状对不上。

**所以"想怎么用"不能决定声明怎么写**：声明必须与注册的真实形状一致，四红就是那张注册表在说"你写错了"。同一件事的另一个读数：**规则右侧能名的只有函数**（`expandRuleRightSide` 的 `Func` 分支），所以在规则集里写 `:tangent/scatter_add_arr` 也得先有一个能名的函数——"不新造库名"并不等于"不用声明"，只是把声明挪进规则集、不放进公共库面。

## 规则集能做什么、不能做什么（三条今天量出来的）

| 想做的事 | 能不能 | 证据 |
|---|---|---|
| 规则右侧直接写一个算子（`=> (ct) => :tangent/scatter_add_arr(...)`） | **不能** | 展开只走 `Func` 分支（按 `calleeIdOf` 取被调图），`Oper` 分支只处理**函数体里已有**的算子节点：规则右侧只能名一个函数 |
| 规则集里写一条**不 `export`** 的函数，用 `#op(...)` 绑内建算子 | **能**，且**不复现** `registry` 四红 | 四红只在把公共名加进 `stdlib/signatures/autodiff.cml` 时出现；同一条声明搬进 `stdlib/rulesets/transpose.cml` 且不 `export`，`transforms 46/46` 绿 |
| `#op` 的写法 | `#op('tangent:scatter_add_arr')` **不认**；`#op(':tangent/scatter_add_arr')`（**带前导冒号**）才认 | `NativeDeclarationNotLoaded: The declaration 'scatter_into' names the operator 'tangent:scatter_add_arr', which no loaded module implements` |

第一条与 `derivative.cpp:386` 的"3 或 4 个操作数（`base, index, delta, axis`，`axis` 可选）"是并列的两条：**一个说右侧只能名函数，一个说声明要按注册的真实形状写**。两个都是"注册表说了算"，不是调用处想怎么写就怎么写。

## 三条红的归属（`transposeSignature` 那一支）

放开 `transposeSignature` 的多参数分支后红了三条：`transpose_many_parameters`、`transpose_many_parameters.is_linear_in_the_cotangent`、`transpose_unread_parameters`。**逐条查过它们的签名，形参全是 `float`**：

```
transpose_many_parameters   func mt(x: float, w: float, dx: float, dw: float): float
transpose_unread_parameters func at(x: float, w: float, dx: float, dw: float): float
```

**每个形参都有切空间**，所以这三条红**不是"期望该改"，是我那次改动自己删了一个种子参数**（把返回签名少了一个位置）。按"全部参数都有切空间 → 红说明改动错了"这条判据，这一支到此为止：**要重做的话，正确改法是保留原有的种子参数、只放开循环里的类型判断**，而不是改期望。

（`transpose_unread_parameters` 尤其说明既有不变量：**不被读的参数仍然要有一份余切**——这与"没有切空间的形参不产出余切"是两件事，别混。）
## 三个 vjp 用例的对照表（引用读数必须带用例名）

| 用例 | 程序 | 期望数 | 今天状态 |
|---|---|---|---|
| `vjp_api.analytic_gradient`（`test/plans/feat/compile/transforms.plan.toml`，`vjp_analytic_gradient.cml`） | `f(x,y) = x*x*y` 在 `(3,2)` | `18.0 / 12.0 / 9.0`，四后端 `nvm/fvm/jit/prl` 一致（`backends_agree = ["exit_code", "output_normalized"]`） | **绿** |
| `vjp_api.gradient_when_the_steps_land`（同计划，`vjp_gradient.cml`） | `f(x) = x*x*x` 在 `x=3` | `vjp(f)(3.0)` 读 `(27.0, 27.0)`、`grad(f)(3.0)` 读 `27.0`；计划里 `backends = ["nvm"]`，`expect_stdout_contains = ["27.0"]`（**只有 nvm 一遍**，没有 `skip_unless`） | **绿** |
| `probe_library_vjp`（`test/tools/have-library-vjp.mjs`） | 同 `f(x,y)=x*x*y` | **无期望数**，只证"装得起来" | **绿** |

**为什么列这张表**：今天有人（我）引用过两组没有用例名的数（`4.0 / 4.0`、`-6.0 / 3.0`，来自 `stdlib/vjp.cml` 落笔时的对照），它们**查不到出处**，所以不算证据。**引用任何读数必须带用例名或计划名；没带名字的数就是假数**——它看起来像证据，但和"跨空间比较"是同一类错。

**关于 `mt`/`at` 的归属，原样记一句**：`transpose_many_parameters` 与 `transpose_unread_parameters` 的形参全是 `float`、都有切空间，所以那三条红**错在我的改动（删了一个种子参数），不是期望该改**；`unread_parameters` 钉住的是"**不被读的参数仍要有一份余切**"，它与"**没有切空间的形参不产出余切**"是两件事。
## `skip=rule` 有两种，别混（今天差点为它去改 `export`）

```
TRANSPOSE … skip=rule uri=:op/idx_arr reason=no-such-family   ← 规则集没加载（家族不在索引里）
TRANSPOSE … skip=rule uri=:op/idx_arr reason=unbound          ← 键没绑上（transposeBindings() 里没有这个 uri）
          （第三种：reason=no-rule —— 家族在、键在，索引里没有这条规则）
```
三个 reason 出自 `modules/transforms/transpose.cpp:1296–1301` 的同一个三元式：
`!rule.has_value()` → `unbound`；`NoSuchFamily` → `no-such-family`；否则 `no-rule`。

**`gs6` 那天量 `transpose` 时格子全是 `no-such-family`，根因是测量程序漏了 `import rulesets.transpose`**——**"测量程序的 import 也是被测量的一部分"**。看到 `skip=rule` 先分这两类：**`no-such-family` 去看 import，`unbound` 去看绑定表**；**别去改规则名的可见性**（`export` 与否不在这条链上：`index_into` 私有与公开，`unbound` 都会是 `unbound`）。

`transposeBindings()` 是 `src/core/forward.cpp:26` 的**函数内 `static`**（`static std::map<std::string, ForwardRuleBinding> table;`），所以"另一个实例"不成立——进程内只有一张表。
## 「按形状展开」这一步今天撞到的是缺一个读者，不是缺一个想法

`transpose.cpp:1312–1363` 先把规则名记进读数、再按名字分派：
```
1313  noteRule(rule->rule);          // `rules=add_t,mul_t` 就是它记的
1314  const std::string &law = rule->rule;
1315..1358  add_t / sub_t / neg_t / mul_t / div_t 五条分支
1359  else → skip=law rule=…
```
**读数是"按名字分派"的证据，不是"按形状"的证据**：换成形状路径时，读数也得跟着换说法（`law=` → `rhs=`），否则读数会说着旧机制。

但"按形状展开"要用的是**规则右侧那个函数的参数签名**，而 `include/camel/parse/ast/rule_head.h` 今天只给四个读者：
```
rhsCalleeOf(family, rule)   // 右侧调用的函数名（`add_tangent` / `scatter_into`），没有参数表
lookupRule(family, head, arity)
ruleFamilies()
RuleEntry{ head, arity, rule, variables }   // variables 是左侧绑定的名字
```
**没有"右侧的参数表"这个读者**。所以"从右侧签名读出余切位置与已知值位置"**今天表达不出来**——要加一个读者（不是字段，是从规则内容里读出来的东西），按原则 2.3 属于"能从内容推出"，可以做，**但要先把它写成一句话证明，而不是顺手加**。

**这一步的可验收标准（作者已裁）**：五条规则各打印一行 `law=<名字> cotangentAt=<i> knownAt=<j…> sizeDependent=<yes/no>`，**必须与五条分支语义一一对上**；对不上就说明这套词汇表达不了现有五条，**那也是结论**，要写清差在哪一格。**今天这一条没有交出，因为读者不在。**
## `rhsParams` 的落点（下一步照着做，四个锚都在这儿）

作者已裁：**`rhsParams` 是 `RuleHead` 里"同一份文本的第四样摘要"**（与前三个同类），**但只记"右侧签名"，不记"解释"**——存 `{name, type}` 序列，**`cotangentAt`/`knownAt`/`sizeDependent` 一律不进索引**（那是 `transpose` 对签名的解读，属于消费者）。

四条锚：
```
src/parse/ast/builder.cpp:899-916   右侧 callee / 左侧 variables / outerUses 都在这儿读，第四样加在这里
                                    （`rightCalleeOf(sides[1])`、`variableNamesOf(sides[0])`、
                                     `outerNamesUsedInRight(...)` 是现成的同族函数，新加一个 `rightParamsOf(sides[1])`）
include/camel/parse/ast/rule_head.h:62-78   `variables` / `outerUses` 字段与 `RuleHead` 构造参数，
                                            `rhsParams` 加在 `outerUses` 后面
src/parse/ast/rule_head.cpp:280-291         构造函数的形参列表（同名同序加一项）
src/parse/ast/rule_head.cpp  `indexRule`     索引时记的两张表 `callees[family][rule]` / `variables[family][rule]`
                                            旁边加第三张，并加读者 `rhsParamsOf(family, rule)`
```
打印（下一步的验收）：**一行里分清"记的"与"推的"**——
```
law=<名字> params=[(x: T), …] → cotangentAt=<i> knownAt=<j…> sizeDependent=<y/n>
     └─ 索引里记的 ─┘            └──── transpose 现场算出 ────┘
```
`transforms 51/51` 必须逐字不变，`if (law == …)` 不动。

**这一轮没有再往前，是因为 `rightParamsOf` 要写一个新的 CST 走读**（同族的 `rightCalleeOf`/`variableNamesOf` 各是一小段），**不是加一个字段就完**；写它需要与 `cst::Expr` 的形状对齐，**改错了会红在解析层**——那比 `transpose` 那层红更难查。
## 19:10 的读数是**待复现**的，不是已复现的

作者给的读数：`f(xs, n) = xs[n] * xs[0]` 在 `([3.0, 2.0], 1)` 处 `grad` 对 `xs` 得 `[2.0, 3.0]`，`n` 没有余切。**今天没有复现出来**，卡在**两层前置**：

1. **`stdlib/vjp.cml` 里 `grad` 只有 `float` 那一份声明**，没有数组/多参 arm。`grad(f)([3.0, 2.0], 1)` 在**重载解析**这一步就被拒：
   `grad and with_grad differentiate the first 'with' parameter of a function that returns a float …`
2. **即使补上 arm，它会停在 `transpose` 的五个 `law` 名字分派上**（`skip=law rule=…`）。

**这两件事是一件事的两半**：arm 只有在 `law` 分派解决后才有意义，否则只是把墙往前挪一格。

**写这一段的规矩**：作者给读数是**要求复现**，不是**允许引用**；复现不了时写"待复现 + 卡在哪一层 + 前置是哪两件"，**不许写成"已按裁定落地"**。

## 一处最容易被下一个人搞错的地方：没有端口要占位，不能少一个元素

```
modules/transforms/jvp.cpp
  tangentPorts.push_back(tangent);                 // 有切空间：真端口
  else tangentPorts.push_back(rt::kInvalidNodeRef); // 没有：位置占位
  …
  tangentOf[ref] = tangentPorts[parameter->second]; // ← 按形参位置索引
```
那份 tangent 列表是**按形参位置索引**的，**列表缩短会把后面几位静默错位**。所以"没有切空间"必须写成**位置占位**。

**判据仍然只有一个**：`camel::core::tangentSpaceOf`（签名层与遍历层共用）；`kInvalidNodeRef` **只是索引对齐，不是第二个判据**。