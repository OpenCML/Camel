# 生成器那只格子没人修补：车道 N 五件事里的一件今天已经不用做

- 日期：2026-11-19｜车道 `lane-ssa`，基线 `origin/develop` = `943d97aee`｜依据：`docs/roadmap.cn.md` §0 逐字「生成器旧的单元路径不再修补，随 E 车道换到 GIR 后删除。」

## 1. 结论

「生成器旧单元路径停止修补」是一条**判据**，不是待办：它今天已经成立。机器自己的那只格子
（`__gen_pc__`）由机器直接造，**不经过任何事后修补**——两条修补路径（读折叠、丢单元）的入口都在，
但输入表里没有它。所以五件事里这一件**没有代码要写**，只需要把证据摆成一页，免得下一个人重新怀疑。

## 2. 那只格子是怎么造出来的

`src/compile/gir/builder_generator.cpp#Builder::buildGeneratorBody`，在 `__gen_step__` 图里：

```cpp
const node_handle_t pcCell = newVariable(pcMember);
insertNode("__gen_pc__", pcCell);
```

注释逐字：「The point of the body the pull starts from: the state's first part, taken out and put in a cell of its own, so that an arm which moves the point on is read by the dispatch of the same pull」。三条后果：

1. **它不是声明**：`Builder::scanBodyForSsa` 只看 `VARI`/`NREF` 声明的名字，机器造的格子不进扫描表，`ssa_` 里没有它的键。
2. **它不进 `carriedVersions_`**：那张表只有两个写点，都在 `src/compile/gir/builder_control.cpp` —— 分支 JOIN 一处、循环头 carried state 打包一处。
3. **它不进 `valueCapturedCells_`**：只有一个写点，`src/compile/gir/captures.cpp` 按值捕获一只格子时。

两条修补路径的候选集合就是这两张表（外加 `writesOf_`），所以对 `__gen_pc__` 没有输入。

## 3. 两条修补路径的入口守卫（逐字）

```cpp
// src/compile/gir/builder_expr.cpp#Builder::collapseNonEscapingVariableReads
if (nodeKindOf(cell) == camel::runtime::GCNodeKind::Port) { continue; }
if (escapingVariables_.contains(cell) && !writeOnlyEscaping_.contains(cell)) { continue; }

// src/compile/gir/builder_expr.cpp#Builder::dropCarriedVariableUnits
if (nodeKindOf(cell) != camel::runtime::GCNodeKind::Oper ||
    escapingVariables_.contains(cell) || captureOriginOf_.contains(cell)) { continue; }
```

第一处**跳 `Port`**；第二处**只收 `Oper`，且不收 `escapingVariables_` 与 `captureOriginOf_` 里的**。
`__gen_pc__` 是 `Oper`，但第 2 节第 2、3 条说它不在那两张表里，而第二处的 `cells` 正是由那两张表拼出——**入口对，输入为空**。

## 4. 计数（`943d97aee`，主机 88，`-Check capture-census`，scope `test/cases`）

```
files=3924 listed=3924 counted=2760
var_new=214  var_new_empty=43  cell_typed_nodes=1191
capture_ports=177  capture_ports_generator=111  capture_ports_closure=66
list_sha=264fd76a0f7dc8a8465a5bf0640374f47efc5983e62593a5d4fb647296acedf6
```

`var_new=214` 里 **32 个在生成器机器图**（32 个生成器各一只 `__gen_pc__`），其余 182 个在别的图。
那 32 个**全部存活**：修补路径若碰过它们，读数会变小或它们会改名。同语料 `std::jgir canonical=true`
的逐文件 sha 清单（3924 行）另存树外，清单 sha=`9c10477d8631f156…`。

## 5. 这一页不主张什么

- 不主张生成器状态已是「值」：`__gen_pc__` 仍是格子，下移到 GIR、全部槽 `T?` 是车道 E 的活。
- 不主张折叠与 `dropCarriedVariableUnits` 今天没用：那 182 个非生成器格子正是它们的输入，S4b/S5 之后才清。
- 不主张「停止修补」等于删掉它：判据是**它不再有输入能走到**（第 2–4 节就是这句话的三个读数），不是它被删了。