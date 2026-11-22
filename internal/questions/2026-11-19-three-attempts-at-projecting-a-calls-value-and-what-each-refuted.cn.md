# 对一次调用交出的值做投影：三次尝试各自否掉了什么

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `9e296efc0`）
**状态**：待裁（**本页不填默认**，理由见 §3）
**触发**：作者 `376e9c86b` 催「一条调用点规则」；三次落点各量一次，**三次全撤**。这一页只交否定结果。

## 1. 三次尝试各自否掉了什么

**(一) 入口图也打包**（`builder.cpp:148/152` 加两行 `withExitType`）——撤。否掉的是**这个方向的用例基础**：全树 `test/cases/**/*.cml` **3924 个文件、3685 处 `func main(`，没有一处声明 `var` 形参**，而 `packsVariableParameters` 对没有 `var` 形参的签名不打包 ⇒ **入口永不打包**，那两行空转。六计划一个数没动。作者 `58e739dcf` 随后改口为「入口呈现结果、不在入口上 `withExitType`」，与本条的读数同向。（本页初稿曾写「250 处」，那是搜索回显被截到 250 条的显示数，已更正为上面这组可复现的数。）

**(二) builder 的决定处**（`builder_operators.cpp:5050` 的 `handsBackParams`，+10/−1，让经函数值的调用从签名派生）——撤。否掉的是**那个落点**：`compile` **2610 pass / 9 fail + killed 2**、`linear` **734 / 130 + killed 12**（基线 `2614/5` 与 `751/113`，killed 0）。

**(三) 让 `fvm` 接 CALL 的 ACCS**（不改代码，只读）——停。否掉的是**「放松一个校验」这个前提**：`std::fvm` 报 `Unsupported ACCS source node type`，而落点是 `fastvm/compile.cpp:402-406` 的 `default:` —— 它**前面**的 `GUARD(srcRecord->dataType()->isComposite())`（`:368`）**已通过**，所以源是**复合但既非 Tuple 也非 Struct**（数组）；放开它就是让 VM 在运行期索引一个不是元组的值。（二）带进树的 14 条红也全在数组/outline 一族：`nullable_assign.enters.*`、`outlined_execution.*`、`outlined_resident_execution.*`、`preferred_resident_execution.*`、`compile.state_escape.read_escape_array`、`resident_graph_transfers`。

## 2. 结构性结论（这一页真正要交出去的）

**（二）改的那个决定处看不到要改的调用**：`var.parameter_write_is_the_callers_value` 在带改与不带改下**逐字相同**（`... (0, 5) 5`），`var_parameter.write_outside_sync` 也仍是 `(0, 1)` ⇒ `visitLinkNode` 的 `handsBackParams` **看不见** `store(x)` / `h(e)` 这类**经函数值的调用**，改它的时候读数不变，变的只有别处。

⇒ 所以那 5 条红与 `linear` 的 113 条红落在**另一个构造点**，而**那个点至今没有被指名**：三次尝试没有一个落到它上面。

## 3. 要作者答的一句（本页不填默认）

> **「对一次调用交出的值做投影」在 Camel 里属于运行期语义，还是构造点选择？**
> ——若是**运行期语义**，那它是一个**语言能力的缺口**（一次调用交出的元组要被按序取份），不是一次修复；
> 若是**构造点**，请指出那个点：`store(x)` / `h(e)` 的值在构造里由哪一处给出。

**本页不填默认**：三次尝试三次被撤。再填一个默认，第四次也会被撤——而这一页把三次否定结果交出去，比再赌一次有用。