# h461（退役 `sync func` 前缀与 `wait`）：做不到终态的部分与判断依据（2026-10-25）

本文件只记 **h461 lane 改了但没改到底、或改完需要别的车道/语言作者注意** 的事。结论都来自本树
实测（`handover/h461-retire-sync-wait`，host `88zjwei`，计划 `smoke,test/plans/feat -Tier fast -Jobs 8`）。

裁定见 `docs/handover/reviews/questions-2026-10-24-evening.md` §var-parameter-sync-modifier 与
`docs/handover/README.cn.md:779`（H7 第 5 条）：**前缀 `sync func` 与关键字 `wait` 一起退役，解析器不再
认识它们，不留指向新写法的诊断**。本 lane 的终态口径：

- `var` 形参是资源参数（设计原则 3.3），**没有任何声明要求**，原 `VarParamInAsyncFunction`、
  `CapturedWriteInAsyncFunction` 随之删除（注册表号 `0x100003`、`0x100005` 释放，现有 264 个码）。
- **函数体位置的 `sync`（`: T sync`）与 lambda 的 `sync` 前缀保留**：它们才是「顺序来自写它的那个同步块」
  的写法。`wait` 承担的局部顺序一律改写成同步函数体。
- `IgnoredSideEffect` 保留并改写措辞（"… is not in a sync block, and …"）；它现在会给
  `func bump(var x: int)` 的调用点报警——**假阳性的归属仍是 H242a**，本 lane 不为它加诊断。

## 一、写在同步函数体里而不是改期望值的地方（作者需要知道语义没变）

退役后有 4 个用例原本靠 `wait` 建立顺序，删掉 `wait` 就会改输出。本 lane 一律把**函数体改成同步块**
（`func f(...): T sync`，探针实测与 `wait` 时期同一个值），而不是把期望值改成无序结果：

| 处 | 改法 |
|---|---|
| `test/cases/linear/byref/captured_write_order.cml` 11 个 `*_then_*` 函数 | 全部改成 `: int sync`，11 行输出与注释表逐一核对不变（10/10/100/10/111/1011/12/611/40/2508/21） |
| `showcase/d4-mcts/search.cml` `apply_move`/`record`/`grow`/`put_child` | 改成 `: State sync` / `: Node sync`：路径赋值现在写在同步函数体里，16 个 test + `guard.no_playouts` 的「不能有警告」重新成立 |
| `test/cases/linear/byref/wait_direct_user.cml` 的 `run()` | 改成 `: int sync`；图断言随之改写（见下） |
| `test/cases/linear/byref/captured_write_graph.cml` 的 `waits()` | 改名 `orders()` 并改成同步函数体 |

**语义差别（图上是可测的）**：`wait modify(i)` 把控制边从变量的**直接使用者**（`+`）拉到写者；
同步函数体把**相邻语句**串起来。`wait_direct_user.graph` 的断言因此从
「`+ -> modify` 在、`before -> modify` 不在」改成了「`before -> modify` 在、`modify -> 之后的读` 在」。
两者给出同一个值（2011），但没有控制边的等价性——退役后**只有语句级顺序**，这是裁定要的终态。

## 二、刻意没删的东西（需要作者/别的车道决定）

1. **LSP 的 `sync` 语义修饰符还在**：`src/service/lsp/queries.cpp:218,1461`、`syntax_index.cpp:497`
   仍然发 `sync` 修饰符，`editors/vscode/scripts/check-manifest.mjs` 要求清单同时声明 `mutable` 与 `sync`。
   前缀退役后它**在函数声明上不可能再出现**（只剩函数类型/lambda 前缀，而那里没有声明位置）。
   删它会连带改 `package.json`、tmLanguage 作用域与若干 golden，超出本 lane；`docs/editor-support.cn.md`
   与 `.en.md` 已如实写明「随前缀一起退役，保留声明只是为了不动扩展清单」。**要删还是要留，请作者定。**
2. **GCT/GIR 里 `wait` 的内部机器仍完整**：`WaitLoad` 下沉、`NodeKind::WAIT`、语句/表达式的 `waited()`
   标志都保留（本 lane 的任务书把删除范围限定在解析器/语法层）。它们现在**没有任何语法入口**，
   属于死代码，但删它们会动 GIR 结构与多个 golden，需要单独一轮。
3. **死打印机**：`src/parse/ast/stmt.cpp:286,294` 与 `src/service/codegen/source/generator.cpp:836,463,500,572`
   仍在处理 `Wait`/`sync func` 情形（永远不再被构造）。
4. **记录性文档不改写**，只在必须处加一句注解：
   - `docs/technical/18_sync_control_semantics.md`：§2 开头加了「（H461）2.2/2.3 与第 3 节描述退役前的模型」
     的注；§2.4 静态约束按新模型改写；两段 `附：代码示例` 的可编译 fence 已改写成同步函数体。
   - `docs/technical/00_language_reference.md`：**整章仍是退役前语言**（§6.10 wait 表达式、
     关键字表里的 `wait`、`blockStmt : wait? stmtBlock`）。它与 `docs/language/**` 重复，本 lane
     只做最小改动。**问题是这份 `00_language_reference.md` 该整体删除、还是指向 `docs/language/**`。**
   - `docs/technical/30_diagnostics_reporting.md:272`：一次程序测量的记录（`CapturedWriteInAsyncFunction` 33 处）。
     该诊断已删除，这个数字现在必然是 0；作为**当时的测量结果**保留。
   - `docs/language/04_execution_model.md` 的 §4.3.3 标题 `sync func（函数修饰符）` 与 §4.4 标题
     `var、引用与 wait` **故意保留**（标题变了锚点就断，多处交叉引用会 404）；正文已写明退役。
   - `docs/design/*`、`docs/handover/*`、`docs/design-principles*`、`changes.log` 是历史记录，未改。
5. **用例文件名里的 `wait` 保留**（`fault_in_wait.cml`、`wait_direct_user.cml`、`wait_before_after.cml`、
   `wait_order.cml`、plan 里的 `wait_direct_user.*`/`wait_graph.*`/`wait_before_after.*` 约 70 条）：
   改名会同时移动 plan 名、golden 文件名与 poison 列表，收益只是措辞。文件内的注释与代码已按终态写。

## 三、删掉的用例与文件（提交信息里同步说明）

- `test/cases/compile/not_supported/wait_ignored.cml`（及 `not_supported.plan.toml` 里的块）：
  它钉的是「`wait` 被忽略 + `ConstructNotSupported`」这条退役诊断，按「不留诊断」删除。
- `test/cases/compile/diag_json/captured_write_without_sync.cml`（及 plan 条目）：整例就是
  `CapturedWriteInAsyncFunction` 的复现，诊断删除即无对象。
- `test/plans/feat/compile/decorators.plan.toml` 的 `blocks.no_declaration_warning`：它断言
  「装饰块不被当成函数声明报警」，钉的正是被删的声明类诊断；`blocks.cml` 本身保留（别的 test 在用）。
  取而代之的是**没有**新断言——装饰块的写入现在按普通写入报警，见设计原则 3.5 的新模型。
- 教程示例 `docs/tutorial/examples/06_sync_func.cml`、`06_wait.cml` 删除，替换为
  `06_sync_body.cml`（规则：函数体不是同步块时 println 不执行）与 `06_var_order.cml`（顺序来自同步块）。

## 四、实测数字（本 lane 终态 gate）

- `smoke + feat -Tier fast -Jobs 8`：`RUN_EXIT=0`，`known failures: 8`（`edges.*` 4 条、`array_reduction_arms.*`
  与 `array_reduction_complex_reduce.*` 各 2 条后端，与基线同集合），红集只减不增；基线两条红
  （`docs.links.citations`、`docs.snippets_compile`）仍是红，原因与退役无关（`docs/design-principles.cn.md:283`
  的 `...` 片段、`docs/design/public-repo/README.cn.md:66` 的失效引用）。
- `docs/reference/camel-for-ai.md`：110594 / 112000 字符，264 个诊断，四个生成区与来源一致。