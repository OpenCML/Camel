# 规则看不见"谁还在读这个节点"、也看不见 pass 的权限默认值：库重写得比旧 pass 多，这算基线吗？

## 问题

H226 把规则库与**被删掉的** C++ 法表（`e8c290554:src/passes/opt/generic/algebra.cpp`，把该文件临时恢复回工作树后实测，基线 `c638d55a0`）逐图对量。四个语料、三十三条改写里，**库重写、旧表拒绝**的有两条形状族，原因都不在"律"上，而在旧表能看见、规则**看不见**的两件事：

1. **使用者与控制边**。`combine`：`let a = x * 3 + x * 5; println(a)`（`sync` 函数里两个乘的顺序被控制边钉住，图上 `x*3` 有一条 CTRL 边指向 `x*5`）。
   - 旧表：9 节点 / 10 边，`add_l` 1、`mul_l` 2，**不动**。原因实测：`exclusive()`（`algebra.cpp:237-247`）要求被消去的乘积**除本消费者外没有别的使用者（数据、WITH、CTRL 都要没有）**，而 `x*3` 还有一个 CTRL 使用者 `x*5`（插桩输出 `EXCL node=2 consumer=5 replaceable=1 user=5 user=4`），于是 `term()` 退化成"没有乘积"，加法分支直接放弃。
   - 库：6 节点 / 5 边，`x * 8`，两个乘积都被抹掉（`int_combine`）。规则没有办法问"这个节点还有谁读"。
2. **权限默认值**。`combine_d`（`x*3.0 + x*5.0`）与 `tensor_combine`（`t*3.0 + t*5.0`）：旧表 9/10、8/10 不动，库 6/5、5/5。
   - 旧表：浮点加法分支要求 `kReassociate`（`algebra.cpp:388-393`），而 `std::opt::algebra` 的默认策略 `NumericPolicy::relaxed()` **不给**这个授权（同族的 `double_d` 因为走 `exactDoubling` 的精确路径而**不需要**授权，所以两边都做）。
   - 库：`rules::simplify` 的默认就是"给出规则所需的授权"（`simplify_strict` 才是全不给），`#requires<reassociate>` 因此成立。同一个默认开关，两边给的权限不同。

两处都不改变程序的值（`same_values` 条目在四个语料上逐行比过），也都没有丢掉会被执行的效应（被抹掉的乘积是纯的）。但它们意味着：**同一个 `std::opt::algebra` 名字下，库比旧表多改写**，而这多出来的部分来自"规则语言表达不了的旧 pass 条件"和"两个实现各自选的默认权限"。

## 选项与代价

- **(A) 承认这就是新基线**（库比旧表宽）：文档里写明"条件类守卫（使用者/控制边/锚点）与预算类授权（`reassociate` 等）随 C++ 法表一起消失，规则库按 `simplify` 的默认授权工作"。代价：9.7 那条（右侧丢效应必须纯）变成唯一的安全网，而它今天还没落地——见 `rule-drops-effect.md`；旧表里那两条"因为看不见而更保守"的行为没有等价物。收益：不新增语言特性，测量里两处差异作为"库更宽"的已知事实被钉住。
- **(B) 给重写语言补条件**：让规则的 `when` 能问"这个节点还有别的使用者吗""它被控制边钉住吗""它是图的命名节点吗"（后者是 H160 已经记下的同一条）。代价：语言/引擎的新工作，而且条件一旦存在就必须有语义与测试；`exclusive()` 的"数据、WITH、CTRL 三种使用者都要为空"这一条要原样搬过来才等价。收益：库可以精确复刻旧表在条件上的保守，锚点那条也能一起解决。
- **(C) 让 `simplify` 的默认授权对齐旧表**（不给 `reassociate`）：代价：库会少做浮点 combine/mixed 家族，多个计划期望要改，且与"库是唯一实现"的现状冲突（没有人再需要那个保守默认）。收益：与旧表的行为在默认开关上一致。

## 施工方建议

**(A)**：测量里这两处差异都是"库多做了、值不变、效应不丢"，而重写语言加条件（B）是为了**安全**才值得付的代价；今天还没有一个"因为看不见使用者而改错"的实例，所以先把基线写成事实。**若**决策者认为锚点那条（`rule-cannot-write-a-tensor.md` 与 H160）必须解决，那 (B) 就是同一条工作，建议一并立项、一次把"条件可见性"补齐（使用者/控制边/命名节点），而不是只补锚点。**(C) 不建议**：它会把库按一个已删除实现的默认值绑住，而那个默认值本身没有任何判词支持。

## 来源

- 测量：`test/cases/opt/algebra_rules/library_only.cml`、`tensor_forms.cml`、`test/plans/feat/opt/algebra_rules.plan.toml`（`library_only.table` / `tensor.table` 的 `differs` 记录）、`test/cases/opt/algebra_rules/tables.mjs`。
- 旧表：`e8c290554:src/passes/opt/generic/algebra.cpp` 的 `exclusive()`（237-247）、`term()`（256-276）、加法分支的浮点授权（388-393）；结论由把该文件恢复回工作树后实测（`combine` 9/10、`combine_d` 9/10、`tensor_combine` 8/10 不动）与插桩（`EXCL node=2 ... user=4`）得到。
- 库：`stdlib/rules.cml`（`int_combine`、`float_combine`、`tensor_f32_combine` 及其 `#requires<reassociate>`）。