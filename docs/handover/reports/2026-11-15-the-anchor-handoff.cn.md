# 锚的交接：还差三步，和两件不许顺手做的事

日期：2026-11-15。范围：`lane-refcount`。tip `2272e8fd9`，工作树干净，`values` `144/144/0`。
这一页是给下一个人照着做的：三步、今天的确定结论、两件必须等的事。**每一步一次构建**，不要一次改完再编。

## 1. 三步

### 第一步：让旁表活到执行期

- **文件**：`src/passes/sched/fastvm/compile.cpp`，函数 `compileRuntimeGraph`。今天它在
  `anchorSitesWanted`（即 `camel::core::mm::allocatorChecks().printForeignResources`）为真时建一个
  局部 `std::unordered_map<size_t, camel::runtime::gc_node_ref_t> anchorSites`，在两处
  `localPcNodes` 填充点（紧挨 `localPcOrigins` / `nodeOrigin` 的那两行）写入，并在
  `return bytecodes;` 之前打印 `PLAN_REF_SITES` 然后销毁。
- **做什么**：把这张表的**归属**移到运行期持有者里（按 `const GCGraph *` 索引，值是该图「pc → 节点」的
  向量或表），仍然只在 `--gc-check` 打开时建；关掉时一个字节都不建。打印可以保留，也可以改为从持有者
  打印，但不能因此多一次写。
- **构建后看什么**：`values` `144/144/0`（关掉时零字节、stdout 逐字节相同）；打开时上一轮的
  `PLAN_REF_SITES` 行一字不变。

### 第二步：一个只报数的 helper

- **文件**：`src/execute/exec_plan.cpp` 已经装了锚的判定回调
  （`countedValueIsPlanned`，注册在 `camel::core::rtdata::setCountedCreationCheck`），helper 放它旁边。
- **做什么**：`bool noteCountedCreationAt(const void *graph, size_t pc)` —— 用第一步的持有者把 pc 换成
  节点，经 `plan.topoIndexByRef` 换成位置，读 `plan.refProducedAt[position]`，然后把结论交给
  `camel::core::rtdata::recordCountedCreation(named)`。核心侧还要补一个 `checks` 计数（三条断言要求
  `checks == creations`）。**只报数，不拒绝、不改变执行路径**；`--gc-check` 关掉时第一步就返回。
- **构建后看什么**：`values` `144/144/0`；`tools.plan_refs` 2/2。

### 第三步：两处调用，一处实现

- **nvm**：`src/passes/sched/nodevm/nodevm.cpp` 的算子派发分支（`GCNodeKind::Oper`），用**节点**形式调用
  （nvm 没有 pc，它有的是 `nodeRef`）：helper 需要第二个入口 `noteCountedCreationForNode(graph, nodeRef)`，
  与 `noteCountedCreationAt` 共用同一段判定。
- **fvm**：`src/passes/sched/fastvm/casevm.cpp` 的 `OpCode::OPER` 处理分支，用 **pc** 形式调用。
- **另外两个后端（今天查出来的，别丢）**：`jit` 与 `fvm` 走**同一条字节码路径**，OPER 分支是共用的，
  不需要第二处；`prl`（taskflow）走的是**和 nvm 一样的节点循环**，因此用节点形式的那一处。
- **构建后看什么**：四个后端的锚读数（`creations / checks / found / missing`）逐用例给出，
  `creations=0` 的用例单列（那是「这条用例没有盒子」，不是锚失败）。

## 2. 今天的确定结论

- `counted`/`created` 从两侧钉住（`2272e8fd9`）：`counter_closure` `created=2 counted=2`；
  `gc.foreign_resource.selftest` 造非 `Ref` 块时 `created +1` 而 `counted` 不变。
- 旁表的生成（`d8967f3f6`）与读取（`b98e4bb62`）：挂在既有 `localPcOrigins` 旁、同两处填充，
  「两张表不可能描述不同的位置」是构造保证。
- 逐位置的 `+/-` 表（`24957fe73`）：`PLAN_REFS <graph> at <position> count +<taken> -<given back>`。
  注意它**在构造上不可能发现漏掉的产出**——两个数出自同一批行；漏与不漏由锚回答。
- `std::fvm` 上的第一条节点归属读数（`b98e4bb62`）：
  `PLAN_REF_SITES main at 22 node 9 +0 -2 produces` 与
  `PLAN_REF_SITES __root__ at 0 node 1 +0 -0 produces`。
  反向验证也做过：把节点故意偏 7，读数随之改变（`__root__` 整行消失）——这个仪器能失效。

## 3. 还没做，而且不许顺手做

- `Frame::currentNode_`（`include/camel/core/context/frame.h`，`f01be9f19` 加的字段与两个访问器）
  和 `src/passes/sched/nodevm/nodevm.cpp` 里那次 `currFrame->setCurrentNode(...)` 写入：**必须等锚立住、
  且四后端锚读数逐字节相同之后再删**，并且**与锚分两笔提交**——第一笔证明锚成立，第二笔证明产品干净。
  若四后端读数在删除后不同，说明锚还依赖那个字段，**不要删**。

## 4. 判据（负责人的线）

- 关掉 `--gc-check` 时程序产物逐字节相同、且不多做一次工作 ⇒ 它是**仪器**；有任何不同 ⇒ 它是**钩子**。
  `--gc-check` 下的只读计数器与 `--gc-check` 本身同类。
- 锚的三条断言：`creations > 0`、`checks == creations`、`missing == 0`。**接线之前它们必须一直是红的**，
  不许为了让它们绿而先接锚。