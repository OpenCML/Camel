# 一个 `edit`/`view` 块的根，要不要一个模块（它的组与模板从哪来）

**状态**：等负责人回答（默认见末节：块体是“自己的程序”，块根要有自己的模块表）

## 问句

`edit` 块的体已经按“一个自己的程序”处理（论文 A 那条线：块的根是一个程序根）。
那么块根解析算子名（例如块体里写的 `:block/open`）时，它的**算子组与模板**从哪来？
现状是**没有来源**，于是走的每一步都落空。

## 读数（两层原因，各带 `file:line`）

```
① `graph` 这个模块在本程序里是**被载入的、不是被构建的**
   `src/kernel/core/context/context.cpp:690`  `auto [path, isCmo] = resolveModulePath(name, importerDir);`
   `:700`                                    `module_ptr_t module = loadModuleFrom(name, path, isCmo);`
   `:985-991`  `loadModuleFrom` 走 `isCmo` 分支 → `loadCmoModule(...)`（序列化模块，不进 GIR）
   而唯一往 `ProgramExports` 写算子组/模板的地方是
   `src/compile/src/gir/builder_modules.cpp:1373`  `exportOperatorOverloads(ref.ident(), gct);`
   —— 它在“本次构建那个模块”的导出循环里（`:1350 for (size_t i = 0; i < gct->exportCount(); ++i)`）
   ⇒ 本次程序没有 build `graph`，这段循环没为它执行，`ProgramExports` 里一个 `graph` 实体都没有
② 就算 build 了，也没有“组”可导出：`open` / `graph_of` 是**无体的泛型声明**
   `src/stdlib/graph.cml:70`  `export func graph_of::<F>(f: F) => Graph when is_function::<F>()`（`open` 同形）
   —— 无体声明只产生**模板**，模板在
   `src/compile/src/gir/builder_operators.cpp:1002`  `if (auto it = operatorTemplates_.find(group); …)` 里**按名字**记着
   而块根手里的 DREF 只有 id（`builder_operators.cpp:233` `tree_->makeDRef(*oper, ":block/open")`），没有名字可拼
   ⇒ `operatorGroupOf(id)` 答 null，是**这两条同时成立**的结果，不是一条键拼错
```

**运行读数**（`test/plans/probe/editview.plan.toml`，tip `e014003993`）：
```
[SUMMARY] total: 15, pass: 2, fail: 13, killed by a signal: 8
第一条失败首行：(syntax_only)…/syntax_only.cml:6:12: [Error]: Unresolved reference: ':block/open'. …
第一条中止：Camel Invariant Violation: builder_expr.cpp(958) resolveDRefNode … Guarantee : templates.has_value()
绿的两条：`editview.syntax_only`（只解析）、`editview.sec12_escape`；其余 13 条全是“块根拿不到组/模板”这一条链
```

## 候选（三条路，各有代价）

**A. 块根有自己的模块表**（块体是“自己的程序”的直接推论）
```
· 块的 import 决定它的模块表；载入的模块（`.cmo`）要把它的**算子模板表**带进这个表
· 代价：要给“载入的模块”一条按名字查模板的路（现有的 `nativeOperatorGroup`、`internalOperatorGroup`、
  `operatorTemplates_` 三条都不覆盖“无体泛型声明”）
```
**B. 块根继承外层程序的 `ProgramExports`**
```
· 便宜：块根直接看外层程序已经构建过的模块
· 代价：外层**也没构建** `graph`（它是 `.cmo` 载入的）⇒ 这条今天仍然落空；而且块体就不再“是自己的程序”了
```
**C. `edit` 块的根不解析算子名，只接收已解析好的句柄**
```
· 语义最干净，但要求块的写法改成“外部把算子传进来”，那是语言表面变化
```

## 默认（不等回答也照这个）

**按 A**：块体既然是“自己的程序”，块根就要有自己的模块表，且**载入模块的无体泛型声明必须按名字进入这张表**。
理由：
```
① 与“块的体是一个自己的程序”这条已定的事一致；B 会让这一条失效，C 是语言表面改动（更大）
② 缺口的形状是明确的：`builder_operators.cpp:1002` 按名字查模板，块根只有 id ⇒ 需要一条“按 id → 所在模块 → 模板表”的路
③ 修法不动语义：只补一条查表路径，不改块体的执行
```
**可被推翻的条件**：负责人裁定“块的体不需要能解析算子名”（即 C），或裁定“块根用外层程序的导出表”（B）——
那就要重写这一格的目标，而不是补查表路径。

## 影响面

- 影响 13 条探针用例（`test/plans/probe/editview.plan.toml`）与 `lane/editview-gir` 那 4 笔（11 src 文件 +224/−5，未合）。
- 该车道按时间盒已**挂起**（分支保留、不 push、不再 rebase）；定案后由它继续或直接判删。
- 不阻塞 `develop`：探针计划不在快层里。

## 决定（负责人，`D-1127-1` ③）：按 A

```
块头让程序**依赖 `graph` 模块**，走的就是普通 `import` 那一个机制
  —— `import` 不执行代码、不往当前作用域注入名字
⇒ **不新设"只依赖、不进作用域"的登记处**（那会把 import 拆成两个概念）
⇒ 块里能直接写 `nodes(d)` 这类名字，是**块头规则自己的作用域**（块内可见 graph 的导出），
   不是改用户模块的作用域
⇒ 停止条件②（导入 graph 把它的库图挂进入口程序）若触发，那是 **import 机制本身的缺陷**（11-25 那页），
   在那里修：只把用到的图链接进来，**不为块头另开路**
```