# 标准库评审：规则的调度组合子与 `rewrite::run`（决策者 2026-10-22）——返工

"规则是值、规则集是数组"通过。调度组合子与运行结果的写法**返工**，它们是标准库里最该体现 Camel 写法的地方之一（`docs/stdlib-conventions.cn.md` 6.5）：

1. **结果是匿名元组 `(Program, int, int, int)!`**：读者要记住第二、三、四位分别是什么。改为一个命名结构体：
   ```camel
   type Run::<F> = { result: F, rewrites: int, rounds: int, visited: int, erased: int, fixpoint: bool, seconds: float }
   ```
   字段直接读：`r.rewrites`。
2. **七个取值函数 `run_program(r)`、`run_rewrites(r)`……是 Java 式的访问器**，而且各占一个 `#op`。`Run` 是普通的 Camel 结构体后，它们全部删除。
3. **用尾递归加累加参数模拟循环**（`seqFrom(p, stages, at, strict)`、`repeatFrom(…, left, rewrites, rounds, visited)`）：用 `loop … for … while` 写，辅助函数随之消失。例：
   ```camel
   with <stages: Rule[][]>
   export func seq::<F>(f: F): Run::<F>! {
       return loop r = Run::start(f) for s in stages do r ?-> then<s>
   }
   ```
4. **每处都写 `match … { case error e => { throw e } case x => … }`**：这正是设计原则里 `x ?-> f`（出错原样传下去，否则继续）要解决的样板。改用 `?->`；若 `?->` 尚未实现，作为本项的语言缺口补上。
5. **签名**：按 H233（遍就是函数变换）与 H235（`with` 参数与默认值），规则集是配置，放尖括号；`strict` 不再一路手传，作为带默认值的 `with` 参数（或取自所在的数值策略）：`f->seq<[algebra, fusion]>`、`f->repeat<algebra, rounds: 8>`。
6. 组合子返回 `Run::<F>`，需要的只是结果时用 `.result`；遍本身（`rules::simplify`）仍是 `F => F!`。

## 第二轮（决策者 2026-10-22）

通过：命名的 `Run`、`loop … for … while`、`?->`、规则集作 `with` 参数都已落地，五个组合子各一个循环，辅助函数与访问器都没了。剩下三处，随相关任务收尾：

1. `(x: Run<Program>) =>` 的参数注解是同一个推断缺口（`array` 第二轮第 1 条）：`?->` 右边的 lambda 应从左边的类型推出参数类型，修推断后写成 `r ?-> (x) => stage(x, s)`。
2. 字面量 `16`（`seq`、`first` 每个阶段的轮数）是隐藏的配置：作为带默认值的 `with` 参数（`rounds: int = 16`）。
3. 签名仍是 `Program`：随 H233 改为 `::<F>(f: F): Run<F>!`。
