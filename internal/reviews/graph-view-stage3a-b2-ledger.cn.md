# graph-view 阶段 3a 第二批：变换、重写、执行、工具

**这一路的账：`1772` → `1586`（−186），新增 0 处。**（变换 `192→159`、重写 `40→32`、执行侧五个目录 `439→436`、工具 `434→292`。）

四个类各一笔，共四笔：`aaa2f8a90`（变换）、`8de478cee`（重写）、`f9189ff32`（工具）、`5066fa6fc`（执行）。

## 0 怎么数

```bash
git grep -n 'GCGraph\|GraphDraft' -- 'src/stdlib/**'
```

按行数，与第一批同一口径：注释也数；子串与整词都记；数的是"点名的处数"，不是"读它的处数"（读它而不写名的行不计）。基点 `234dfb0a4` 上 `1772`。

## 1 表

| 提交 | 类 | 这一笔 | 本类 | 全树 |
|---|---|---|---|---|
| 基点 `234dfb0a4` | — | — | 192 / 40 / 439 / 434 | 1772 |
| `aaa2f8a90` | 变换 | 三十三处借来的图改 `const auto *` | 192 → 159 | 1739 |
| `8de478cee` | 重写 | 七处借来的图改 `auto`；`portIndexIn` 改问视图 | 40 → 32 | 1731 |
| `f9189ff32` | 工具 | 一百四十五处声明丢掉类型名改 `auto`（其中九行仍写着另一个以 `GCGraph` 开头的类型名——`GCGraphBuildAccess`、`GCGraphPayloadArena`——所以按行数只掉 142）；六个纯形状谓词改问视图；控制台谓词改问算子，两处按编号的搜寻改 `nodes()` | 434 → 292 | 1599 |
| `5066fa6fc` | 执行 | 三处借来的图改 `auto` | 439 → 436 | 1586 |

## 2 剩下 1586 处按"这个名字在这里做什么"分

按优先级独占分类（一行只进一类），所以这些数相加正好是 1586：

| 类 | 处数 | 留的原因 |
|---|---|---|
| 形参 / 返回 / 成员声明 | 945 | 这是"把这张图传下去"的地方，不是"读它"的地方。上游是 `Program::root()`、`Program::calleeOf`、`Program::graph`、`RunScope`、遍与观察者回调，改这条链是 3c 的引用模型（引用一律按值、`Graph` 只剩一个实现） |
| 容器与键 | 183 | `unordered_map<GCGraph *, …>`、`std::vector<GCGraph *>`、`std::span<GCGraph *const>`：图指针是**身份**，不是形状 |
| 空指针初值 | 84 | `GCGraph *x = nullptr`：`auto *x = nullptr` 推不出类型，名字必须写出来 |
| `const_cast` / `static_cast` | 58 | 模板参数里必须写类型；其中 `const_cast<GCGraph *>` 是把"借来的只读图"交给要写的会话，属于 3c |
| 注释 | 51 | 句子的主语就是这个类型自己的行为 |
| `using` | 34 | 同文件另有必须写全名的地方 |
| 载荷与边记录 | 25 | 见第 4 节：`nodeBodyAs`、`staticSlots`、`syncEdges`、`orderInputs`、`dataIndex` 之类，视图按设计不答 |
| 其它 | 206 | 单行 local 之外的形式（lambda 形参、模板实参、`for (auto *g : …)` 的容器元素类型等） |

## 3 每一笔做了什么

- **变换 `aaa2f8a90`**：十二个文件、三十三处 `const rt::GCGraph *x = <借来的图>` 改 `const auto *`。全是"读它但不拥有它"的指针：被调用的 callee、`group.encoded()` 的答案、规则右侧的 body。没有一处签名、正文或读数改变。
- **重写 `8de478cee`**：`module.cpp` 七处同上；`portIndexIn` 收 `const camel::graph::GraphView &`，问 `ports(PortGroup::Normal)`，替掉 `normPorts()`。答案同一串端口、同一个次序。
- **工具 `f9189ff32`**：`commit_checks.cpp` 六个纯形状谓词（`sameShape`、`sameParameterCount`、`sameParameterTypes`、`declaresNothing`、`readsValue`、控制台谓词）改收视图：函数类型问 `funcType()`，普通输入问 `inputs(ref, Normal)`，算子问 `operatorOf()` + 算子表。控制台那一条原来是三处各读一次 `nodeBodyAs<GCOperBody>(ref)->uri()`，现在是一处 `writesTheConsole`，三处共用。其余一百四十五处是丢掉类型名的声明；**这一笔的提交说明写的是「一百三十二」，比实测少**：一百四十五处里有九行仍写着 `GCGraphBuildAccess`、`GCGraphPayloadArena` 这类以 `GCGraph` 开头的另一个类型名，按计数口径仍然算一行，所以行数只掉 142（`434 → 292`）。
- **执行 `5066fa6fc`**：三处同上。**这一类没有可迁的读**：执行侧剩下的全是执行计划自己的槽（`dataIndex`）、存下来的 sync/order 边对、`kernelArgumentsOf` 按算子声明重排过的实参次序，以及视图折成一个的四个结构算子种类——视图对这四样都有意不答。

## 4 循环的遍历方式（`C-1123-17`：视图编号有空号）

迁过的循环，逐个报：

| 循环 | 遍历方式 | 为什么这样是对的 |
|---|---|---|
| `commit_checks.cpp` `findPrint` | **`nodes()`** | 找"写控制台"的节点；视图里不是节点的位（删掉的、草稿占位、BRCH 的 JOIN）都不写任何东西，少看它们不改变答案 |
| `commit_checks.cpp` 分支体里找那条写（`commit_ancestor`） | **`nodes()`** | 同上，另加 `regionOf(ref) != 0` 跳过臂内的节点；同一理由：JOIN 不写 |
| `portIndexIn`（重写） | **端口列表 `ports(Normal)`**，不是节点走 | 问的是"这是第几个形参"，端口表按定义没有空号 |

**没迁的循环，逐个报理由**（都是"这条走要不要看见 JOIN"或"视图不答这个"）：

| 循环 | 方式 | 留下的理由 |
|---|---|---|
| `transforms/node_order.cpp` `dependencyOrderOf` | 按编号 `0..nodeCount` | **必须看见 JOIN**：JOIN 是拷贝序里的一项，视图里没有它；搬过去会静默少掉它，而 `vmap` 的 `asked=/found=` 读数与拒绝都会变（`C-1123-17` 点名的第一条） |
| `transforms/split.cpp` `branchNodeOf` / `walkRegion` | 按编号 | 前者读 `GCBrchBody::join`（视图不交 JOIN 这个载荷）；后者数臂内成员数，JOIN 占位时也算一个成员 |
| `test/commit_checks.cpp` `findChainedPrints` | 按编号 | 问的是 `waitInputsOf`——一个块的链是**控制输入**，视图不把控制输入当边答；谓词本身已改问视图 |
| `rewrite/module.cpp` `stepsOf` / `declaredUlpsOf` / `axisPortsOf` | 按编号 | 前两个数"点了算子记录的节点"，第三个要 `kernelArgumentsOf` 的实参次序（按算子声明重排过）；视图没有这两样查询，硬搬会把 `CAST/COPY/FILL/ACCS` 数成算子、或把实参次序读错 |
| `rewrite/module.cpp` `programValueTypes` | `GCGraph::nodes()` | 老 `nodes()` 只跳墓碑、不跳 JOIN；视图的 `nodes()` 会跳 JOIN。这里集合的是"程序里出现过的值类型"，换面就换了集合 |
| `test/impl_body.cpp` 的逐节点打印 | `GCGraph::nodes()` + `ctrlInputsOf` | 打印的就是存下来的形状（含控制输入），换面打印的就不是同一个东西 |

## 5 没迁的读者，按名点出

- **`transforms/split.cpp#calleeGraphOf`、`answersAPairAt`**：它们问的是"这个调用点名的图是哪一张"，答案要 `Program` 按 `GraphId` 取，不是一张图的形状。视图答 callee 要适配器带 `CalleeGraphOf`，而 split 整条链拿的是 `GCGraph *`；搬过去每个调用点都要多一个 `graph.view()`，比现在啰嗦而没有任何一处读变简单。
- **`transforms/split.cpp#exitHalvesOf`**：它走 GATE 用内核那一个 `valueBehindGates`，那个函数收冻结图；在 split 里再写一份门走法就多了一个实现。
- **`rewrite/fact.cpp#compilePattern`、`rewrite/module.cpp#RefEvaluator`**：读的是静态区（`staticSlots`）与算子记录以外的种种载荷（`dataIndex`、`GCOperBody`）。视图按设计不交静态区、不交槽位。
- **`test/commit_checks.cpp#findChainedPrints`**：见第 4 节，控制输入不是边。
- **`test/impl_body.cpp#inputList`**：打的是 `normInputsOf + withInputsOf + ctrlInputsOf` 三段，第三段视图没有。

## 6 读数

`-Plan smoke,feat/parse,feat/trans,feat/compile -Jobs 8`，`[SUMMARY] total: 2917, pass: 2905, fail: 7, known failures: 5`：

| 计划 | total | pass | fail |
|---|---|---|---|
| `smoke` | 10 | 10 | 0 |
| `feat/parse` | 126 | 126 | 0 |
| `feat/trans` | 112 | 106 | 6 |
| `feat/compile` | 2669 | 2668 | 1 |

七条红逐名：`catalog.doc_is_generated`、`agree_with_fvm`、`every_expect_program_links_no_runtime`、`dump_after_each_pass`、`print_and_recompile`、`print_then_run_agrees_with_the_origin`、`numbering_is_the_graphs`。

**这批红不是这一批带来的**：同样五个计划文件（`trans/cpp`、`trans/pass_dump`、`trans/cml`、`trans/canon_order`、`compile/diag_catalog`）在基点 `234dfb0a4` 上重跑，`[SUMMARY] total: 73, pass: 66, fail: 7`，七个名字逐个相同。两边都是同一个主机、同一份 ccache。

`gpu-plans` 那一段是观察层（`report only`，不计入闸门），红 14 条里 12 条在 `internal/tools/gpu-plans.exceptions.txt` 里记过，`module_lease` 与 `async_native_submissions` 两条是本机没有 CUDA 的老红，与本批无关。