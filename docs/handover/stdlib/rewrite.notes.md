# `rewrite` 重写说明

稿件：`rewrite.cml`（仍是 C++ 模块 `modules/rewrite` 的声明文件，Camel 写的组合子在 `rules.cml`，见 `rules.notes.md` Q1）。**未编译**。目标态：每个引擎都是 `::<F>(f: F): F! when is_function::<F>()`，规则集与选项在尖括号里。

## 函数一览

| 名字 | 处理 | 理由 |
|---|---|---|
| `rewrite(p, rules)`、`rewrite(p, rules, strict)` | 合为 `with <rules: Rule[], strict: bool = false> rewrite::<F>(f: F): F!` | 两条按元数的重载并成带默认值的一条；规则集是配置 |
| `saturate(p, rules, rounds)`、`saturate(…, strict)` | 合为 `with <rules, rounds: int = 6, strict = false> saturate::<F>` | 同上；`rounds` 默认 6（与 `std::opt::saturate` 一致） |
| `extract(p, cost: string)` | 改为 `with <cost: (o: Op, inputs: float[]) => float> extract::<F>` | 设计稿 v2 第 5 节：代价是 Camel 函数，字符串形式删除（Q1） |
| `run(p)` | 改为 `run::<F>(f: F): Run<F>!`，`with` 参数不变 | |
| `Run<Program>` | 改 `Run<F>` | 结果是同类型的函数 |
| `check(rules, samples, seed, program: Program? = null)` | 拆成两条按 `with` 区分的重载：`rules->check<samples, seed>` 与 `rules->check<over: f, samples, seed>` | 可空的 `Program?` 位置参数是“有没有这个实参”的伪装；`with` 参数参与重载选择后两个形式各有清楚的签名 |
| `CheckReport` 及九个读取函数（`check_row_count`、`check_rule`、`check_instance`、`check_verdict`、`check_proposition`、`check_evidence`、`check_reason`、`check_coverage`、`check_text`） | `CheckReport` 改成 Camel 结构体 `{ rows: CheckRow[] }`，`CheckRow` 的字段 `rule`、`instance`、`verdict`、`proposition`、`evidence`、`reason`、`coverage`；八个读取函数删除，`check_text` 保留 | 与上一轮对 `Run` 的评审同一条（`stdlib-rewrite.md` 第 2 条）：Java 式访问器；`IndexError` 的八处 `@fails` 随之消失（越界是数组下标的故障） |
| 判决与命题的字符串 | `Verdict = enum { Proved, Refuted, Sampled }`、`Proposition = enum { Equality, RealEquality, ErrorBound }` | 文档里写着“这些词是封闭的”，那就是枚举 |
| `check_coverage_table()` | 改为值 `coverage_table`（`// value` 记录，值仍由 C++ 导出） | 常量写成值，不加括号（`math` 稿同样） |
| `explain(r, a, b)` | 改为 `r->explain<a, b>`，`why_not(r, rule, v)` → `r->why_not<rule, v>` | 被查询的 run 是数据，其余是配置，与 `m->get<key>` 同一类（设计原则 11.4）；`explain_all(r)` 不变 |
| `rule_name` `rule_two_way` `rule_needs` `rule_consts` | 保留 | `Rule` 是不透明类型，没有字段可读；访问器在这里是对的 |
| `Proof` | 不变 | |

`@effect`、`@pure` 注释全部删除。

## 需要的 C++ 改动

`modules/rewrite`：
1. 所有入口按函数而非 `Program` 取参：入口处把 `f` 转为快照，出口处把结果快照包回同类型的函数（闭包的捕获随新函数带上）；`Run` 的 `result` 同。
2. `rewrite`、`saturate` 的两种元数合并；`extract` 接 Camel 函数值作代价（代价模型注册表里原来按名字登记的 `nodes`、`weighted` 保留给 `std::opt::extract` 的字符串形式，见 Q1）。
3. `CheckReport` 由 C++ 对象改成按上面字段构造的结构体值，枚举两个新增；删除八个读取算子。
4. `explain`、`why_not` 的参数顺序改为 `with` 在前（`r` 仍是第 0 个普通参数）。

## 调用点

- 声明机制用例：`test/cases/compile/native/{rewrite_arity,rewrite_declaration_import,rewrite_type,rule_reader_arity,rule_reader_declaration_import,rule_reader_type}.cml`，随签名变化重写（以另一个无体声明模块为样本）。
- `test/cases/modules/rules/` 约 40 个文件用 `rewrite`/`saturate`/`run`/`check`，其中 `check_*.cml`（读取函数）改字段访问：`check_verdict(r, 0)` → `r.rows[0].verdict`（枚举比较，不再比字符串），`check_row_count(r)` → `len(r.rows)`。
- `test/cases/modules/rules/accuracy_schedule.cml`、`fusion_schedule.cml`、`schedulers.cml`：`rewrite(p, rules)` → `f->rewrite<rules>`。
- 文档：`docs/language/09_rules_and_facts.md` 里的示例 `rewrite(p, [sub_self, fold_scales])`；`docs/stdlib-reference.md` 重生；`docs/design/rewrite/v2.cn.md` 8.5 的代码块与 `extract` 一节。

## 开放问题

**Q1. 代价函数的签名。** v2 第 5 节只说“`cost` 是 `(op: Op, args: Shape[]) => float` 一类的 Camel 函数，具体签名随 `graph` 模块定”。草稿取最小的一种：`(o: Op, inputs: float[]) => float`——一个写法的代价是它的算子与各输入写法的代价（egg 的 `CostFunction`）。这不够表达“按形状估算”（`flops`）。推荐等 `graph` 有了 `Shape`/节点类型视图之后，再把第二个参数换成含各输入类型的结构；先上这一版，`rules::nodes` 可用。`std::opt::extract` 的 `cost: string` 同时改成同样的函数参数。

**Q2. `check` 的两条重载。** 若“带 `with` 参数个数与名字的重载选择”尚不能区分只差一个 `with` 名字的两条，则合并为一条，`over` 取 `F? = null` 并要求显式 `::<F>`——不推荐。

**Q3. `Run` 的写法。** 评审稿写 `type Run::<F>`，现有文件写 `Run<F>`（类型函数用尖括号，设计原则 5.6）；草稿用后者。
