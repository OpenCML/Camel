# 出口类型对不上，判决该在封存层说，还是该退回一个能报行号、面向程序的诊断码？

## 问什么

`src/runtime/graph_edges.cpp#checkReturnValueType` 现在对**每一个已定的声明出口类型**都判可赋值
（2026-11-04 之前只判 `T?` / `T!`，其余一律放过）。判决本身没问题，问题是**它在哪儿说**：

- 它只能 `violations_.push_back(...)`，由 `validateRuntimeGraphEdges` 抛 `std::runtime_error`；
- 封存发生在某个 pass 的提交里，`src/execute/pass.cpp#reportPassFailure` 把任何 pass 异常一律变成
  `RuntimeDiag::PassInternalError`，其 help 写的是"**这是编译器的缺陷而不是程序的错**……请把程序和
  pass 列表一起报上来"；
- 而这个判决说的恰好是**程序的错**：程序自己的规则把被返回的算子换成了另一个类型的值。实测原文
  （2026-11-04，`test/cases/runtime/exit_type/float_declared_array_returned.cml`）：

  ```
  error[RuntimeDiag::PassInternalError]: Pass 'std::nvm' failed with an internal error: Runtime graph 'target' breaks its edge invariants (1 violation(s)):
    graph 'target': it hands on node 3, whose value has type 'float[]', and the graph declares 'float'; a return is a value of the declared type, and nothing is converted on the way (design principles 5.1).
  help: The pass gave up without a diagnostic of its own: it built or was given a graph it cannot handle, which is a defect of the compiler rather than of the program. ... report the program together with the pass list.
  ```

  第一行（本车道写的那句）说清了图名、节点、值类型、声明类型；第二行（框架的）说反了方向。
- 同一个原因带来第二个缺口：封存的输入是冻结图，`GCGraph::nodeOrigin` 只答一个 `origin_id_t`，
  而这里没有 `camel::core::source` 的上下文能把它翻成 `file:line:col`，所以**报不出行号**——虽然
  程序作者写的 `return` 语句在语义检查里本来报得出（`SemanticDiag::ReturnTypeMismatch` 带行列）。

一只手能改的地方只有 `graph_edges.cpp`；换通道要动的两份文件（`src/execute/pass.cpp`，以及给
`src/runtime` 一个可识别的异常类型）都不在本车道的可写清单里。

## 读数（可复核）

- `src/core/error/diagnostics/messages/runtime.cpp` 里 `PassInternalError` 的 help 是**所有** pass
  内部错误共用的一条，封存的九条不变量（环、类型、链、纯节点顺序……）全走它。今天 develop 上，
  一条"程序写坏了图"和一条"pass 自己崩了"在读者眼里是同一句话。
- 同一个库里已经有面向程序的同族码：`src/execute/pass.cpp#passBoundaryViolation` 会抛
  `RuntimeDiag::PassBoundaryResources` / `PassBoundaryPorts` / `PassBoundaryNodes`
  （`diagnostics/registry.tsv` 里 `0x00000D/0x000013/0x00000F`），它们的 help 是"pass 交回的图
  违反边界约定"，读者被指向**图**而不是编译器。
- 反证：`test/cases/runtime/exit_type/legal_exits.cml` 的八种合法形状在本轮前后逐字相同，说明
  "判错"与"说错"是两件事——这一页只问后者。
- 行号那条是实测的，不是推的：`std::jgir` 里节点带着 `outputType`，`std::gir` 里也画得出来，两处
  都能指出节点，但都没有"第几行"；有行号的那次拒绝来自语义检查，而语义检查看不见规则库在之后把
  算子换掉。

## 三条路，各带代价

**A. 留在封存层（本轮就是这样）**
- 一行不改框架：判决句自带图名、节点、值类型、声明类型。
- 代价：读者仍会读到"这是编译器的缺陷、请上报程序"；报不出行号，只能给节点，要自己
  `std::gir` 去找。对"程序写错了"这一类判决，这是**说错方向**。
- 好处：九条不变量一条通道，一处改动不动其他八条的措辞；本车道不需要动别人的文件。

**B. 给封存层一个可识别的异常类型，由 pass 边界翻成面向程序的码**
- `src/runtime` 侧：抛一个 `GraphInvariantViolation`（带"这是程序交回的图"的标记）；`src/execute`
  侧：`reportPassFailure` 认出它就报一条新码（例如 `RuntimeDiag::GraphExitTypeMismatch`），help
  指向图与声明，不指向编译器。四处齐全的规矩照 `diagnostics/README` 走（registry 行 → 用例 +
  计划条目 → `--write` → `camel-for-ai --write`）。
- 代价：两个文件不在本车道；新码要重新框定**九条**不变量的读者，其中八条本轮没有测过；`camel-for-ai`
  的文档与 AI 提示词会跟着变，那是一次面向全仓库的措辞变更。
- 好处：程序作者的错被当成程序的错报，且以后可以在这里补上 `nodeOrigin` → 行列（封存前把源上下文
  带进 `EdgeChecker`）。

**C. 折中：判决句不变，只把 help 改成不诬告**
- 只动 `PassInternalError` 的 help（"pass 交不回一张能用的图"），不新增码。
- 代价：所有 pass 内部错误共用这一句，真的 pass 崩溃就不再指向编译器，那是把另一类读者推错方向；
  且这是 `src/core/error/diagnostics/messages/runtime.cpp`，同样不在本车道。

## 默认（本轮按这个开工，不等回复）

**走 A**，并把 B 记成后续车道：理由是 A 的代价是**措辞**（读者多读一行会被误导一次），B 的代价是
**九条不变量的统一通道**和一份面向全仓库的 AI 文档改写，而本轮只测过其中一条。A 落地之后 B 仍然
值得做，且 B 做的时候能顺手把行号补上（`nodeOrigin` 已有，缺的是把源上下文带给 `EdgeChecker`）。

作者若裁 B：本车道这一笔不用回退，`checkReturnValueType` 的判据与句子照用，只是外面那层换成新码。
若裁 C：`PassInternalError` 的 help 改完，本页可整体挪进 `reviews/`。