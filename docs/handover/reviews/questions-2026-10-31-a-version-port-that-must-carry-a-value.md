# 一个必须运值的版本口

**结论先行（一句话）**：`:var/get` 的版本口今天**必须运一个值**——形状一说明它不能是"没有类型的节点"，形状二说明它也不能是"有类型但没有值的节点"。而 `void` 调用没有值。所以"**这次调用本身就是那个变量的新版本**"这句话，**在这个口上落不下**。这不是语义不对，是这条边今天必须运一个值，而 `void` 调用没有值。

**边界**：本页只问一件事——**`void` 调用写过的变量，它的读该靠什么后于那次调用**。不涉及 3.1 的判据本身（那条已经裁完：调用是写者，读后于它）；不涉及 ctrl 种类（那是另一条线）；不涉及 `:var/get` 之外的算子。

---

## 一、裁定已经给的那句话

> **"没有掩码时不要让 `valueVersionOf` 返回空：变量最近一次的写者是一个调用时，版本就是这个调用节点，之后的读从它连一条数据边。调用的值即使是 `void`，这条数据边也表示'读用的是这次调用之后的版本'，并按 3.1 让这次调用执行。"**

登记那一半是齐的：`recordWrite(use.variable, call, throughCallee=true)`（`builder_operators.cpp:4308`）已经把这次调用记成该变量的写者，`isWriterWithLaterRead` 就是它。**缺的只有"读怎么拿到那个版本"这一截。**

## 二、两种落法都试过了，读数如下

### 形状一：把调用节点直接当版本口

```cpp
if (nodeKindOf(*modifier) == camel::runtime::GCNodeKind::Call) {
    return *modifier;                       // 版本 = 调用节点本身
}
```

**读数**：**编译期 139**。

```
#0 DraftGraphBuilder::addOperNode(std::shared_ptr<OperatorIndex> const&, int)
#1 Builder::variableOperator(unsigned int, camel::runtime::DraftNodeRef)
#2 Builder::readVariableThrough(camel::runtime::DraftNodeRef, unsigned int)
#3 Builder::visitLinkNode(...)::$_42::operator()(...)
#4 Builder::visitLinkNode(...)
```

崩点在 `variableOperator`：

```cpp
const auto op = group->resolve({}, {nodeTypeOf(input)}, statics);
ASSERT(op.has_value(), "Variable operator rejected its operand.");
node_handle_t node = currGraph_->addOperNode(*op, 0);
```

`nodeTypeOf(调用节点)` 不是值类型（`void` 调用没有值类型），`resolve` 返回空，`ASSERT` 在 `NDEBUG` 下是空语句，`*op` 取空 optional → 崩。（这一行已单独修成一句人话，见 `67f3edb83`。）

**它证明的事**：**版本口不能是"没有类型的节点"。**

### 形状二：照 `callValuePartOf` 造一个取件节点

```cpp
Type *versionType = valueTypeOf(nodeTypeOf(variable));
node_handle_t version = currGraph_->addAccsNode(versionType, 0);
linkNodes(LinkType::Norm, *modifier, version);
return version;
```

**读数**：`effects_syntax` **40 条里 11 条红**；**`callback_write_ignored.cml` 打印为空**（期望 `3`），exit 0——**静默错，不是崩**。整段输出没了，`EffectNotExecuted` 也自然消失，所以"消失"这个读数在这一版里**不算数**。

**它证明的事**：**版本口也不能是"有类型但没有值的节点"**——`void` 调用取不出任何值，取件节点取了个空。

## 三、问题

**`:var/get` 的版本口是"执行时取值"的口，不是"只表示顺序"的口。** 那么"后于这次调用"这件事，今天只有两条路可走：

### 出路 A：为"后于这次调用"新增一个口或一个算子

版本口之外，再有一个"只表示顺序"的口（或一个专门的算子），它的存在按 3.1 让调用执行，但不参与取值。**代价**：新的语言/图面形状，导出格式要跟着动；且它与 5.6 的"版本就是值"这一条并行存在，两套东西说同一件事。

### 出路 B：读仍取 `:var/get(cell)`，顺序用 `Order` 边，凭据是写者登记表

- 读的版本口仍是变量单元（`version = variable`，今天没有写者时的形状）；
- 从调用节点到读节点连一条 `Order` 边；
- **凭据是写者登记表**：`recordWrite(..., throughCallee=true)` 已经登记，`isWriterWithLaterRead` 正是它——不是这条边。

**这与 F2 不冲突**：F2 说的是"**ORDER 不能当凭据**"。这里凭据是登记表，`Order` 只负责排序。**但它与裁定里"这条数据边"的字面不同**，所以要作者点头才能改口。

## 四、我的判断

**出路 B 与现有手册最接近**：`docs/design/effects/edges.cn.md` 5.6 已经让"读在写之前"的写用 `Order` 边（`recordWrite` 的第一段就是这条），而"读在写之后"的读今天靠版本数据口——**唯一的例外正是 `void` 调用这一格**。B 让这一格与 5.6 的写法一致，凭据仍是登记表。

**出路 A 更"纯"**：它让"后于一次调用"成为一条数据边而不是一条顺序边，与裁定原话一致；代价是新的口/算子，以及导出格式的改动。

**两条都不是我能定的。**

## 五、怎么量这一笔是否成立

**一条尺子**：`133` 这个数今晚被量了两次（两态化前后各一次，逐条一致），所以它是一把尺子——**任何改动若让这 133 条不变，就说明它没碰到版本边那一件事**。（这 133 条是"① 补丁 + 两态化 + 四处事实读取，不含版本边"之下的整闸红数，见 `f9e2ecc63` 那次的对照组。）

**一条反向**：链单独答不了、等待单独够（今天）——`inline` 那处把判定只留 `callFacts.isSyncStatement` 时，整闸有 **1 条红：`control_data.graph`**（`6c25b409d` 的反向读数）。所以"链"与"等待"不是同一个问题，两半都得在。

**一条新判据（今晚学到的）**：**一个总数相同的读数可以是两个不同的世界。做尺子的读数必须带成员名单，不带名单的数只能当趋势，不能当验收。**（同一天量到两次 133，第一次以为成员换了，把名单落盘之后才发现 129 个名字逐条相同——差集是空的。）

**一条待验**：②a 之后要再验一次——那时 ctrl 停了，**链必须真的独自答**；今天 `|| isSyncStatement` 是等价的，明天它要独自成立。

## 六、落地的算子，以及一处比话更宽

`:var/after(cell, call) : var T` 已落地（`6891767d1`）：结果是那个单元本身，第二个实参是那次调用，**它的值不被读**。

**比裁定宽的地方**：裁定说"第二个实参接受 `void`"，实现里**接受任意类型的第二实参**。原因是一条读数：`var_param` 那一族的写者是**有返回值的调用**（`int`），只允许 `void` 会让整族编译失败（第一版实测报 `a variable after a call rejected its operands: the variable is var int and the call is int`）。**这处偏离要让作者看到**：它不改变语义（这个算子对第二个实参什么都不读），但签名比话松。

## 七、`133` 那把尺子作废，新尺子是什么

**作废**：两份名单落盘之后（`D:\Temp\ruler-before.txt` / `ruler-after.txt`，129 条 case 名排序），**新红 0、救回 0、仍在 129，逐条相同**——版本边对那 133 条**一条都没碰到**。所以那 133 是 **① 补丁自己那件事**（块外不再造门）的读数，不是版本边的验收。

**新的尺子三条**：

1. `callback_write_ignored.cml` 打 `3`，`EffectNotExecuted` 计数 `0`；
2. `var_reference` 那 259 条仍全绿；
3. ① 补丁之下，报"三类各几条 + 名单落盘位置"，不报总数。

**一条警告**：第 1 条今天**不能单独当证据**——`valueVersionOf` 的调用写者分支在这条程序上**一次都没进入**（临时打印计数 `0`），所以打印 `3` 是原来就有的 CTRL 排序给出的，不是版本边给出的。

## 八、改动之前就成立的读数

**一条判据**：**一个在改动之前就成立的读数，不能当改动的验收。** 今晚有三个例子：

1. **`callback_write_ignored.cml` 打 `3`**：这个读数在本页的改动之前就成立（控制边排序给的），所以它证明的不是"版本边生效"，而是"没被弄坏"。要它成为验收，必须在**拆掉控制边替身之后**仍然打 `3`——**这是 A/B，不是单向读数**。
2. **`startup.cost` 的 wall time**：这个数在改动之前就满足阈值，所以它不能说明改动带来了什么。
3. **`gs2b` 的 `backend_matrix`**：同样在改动之前就是绿的，拿它当"这一笔没坏"的证据可以，当"这一笔有效"的证据不行。

**用法**：验一笔之前，先问"这个读数在改动之前是什么"。若答案是"已经成立"，它只能进"没坏"那一栏；**"见效"那一栏要另找一个改动前不成立的读数**。

## 九、附注：形状一的补丁

形状一的实现（`valueVersionOf` 返回调用节点）**只存在于补丁里**：`D:\Temp\version-edge-a.patch`，**5674 字节**，内含 `builder_expr.cpp` 的 10 行与枚举两态化的改动（枚举两态化已单独上车，见 `821185288`）。**它是形状一的唯一记录**，重跑本页第二节的形状一读数时用它。

## 十、这一页堵着谁
## 十三、五处 `yet`：目标不具备，还是尚未实现

**一条判据**：**看见 `yet` 先分两件事——是目标语言/后端没有这个东西（目标不具备），还是我们有而没写（尚未实现）。两者的收尾不同：前者进"不支持"的清单，后者进"待做"。**

逐处读过，一处一句：

| 处 | 原文 | 判 |
|---|---|---|
| `src/passes/trans/cpp/cpp_types.h:72`（实现在 `cpp_types.cpp:355-`） | `The C++ type of a value of `type`, or nothing when std::cpp does not translate it yet.` | **尚未实现**：映射表是我们自己的，大部分类型已映射到 `rt::Array`/`rt::Cell`/`rt::Opt` 这类运行时类型；没映射的是这张表缺条目，不是 C++ 没有这个类型 |
| `src/passes/trans/cpp/function_emitter.cpp:690` | `… contains {a function bound to its 'with' arguments \| a read through a reference}, which std::cpp does not translate yet` | **尚未实现**：C++ 有引用，也有捕获的 lambda；缺的是这两种节点在发射器里的写法 |
| `:973` | `… uses the operator '{}' on values std::cpp does not translate it for yet` | **尚未实现**：规则在 `operator_rules.cpp` 这张表里，没写就是没写（表里已有的算子不受影响） |
| `:1348` | `… makes a function value of type '{}' std::cpp does not translate yet` | **尚未实现**：函数值就是 lambda，C++ 有 |
| `src/passes/trans/cpp/operator_rules.cpp:453` | `… neither is bf16 or f16 (this back end has no such element type): those are refused` | **目标不具备**：这一后端的元素类型集合里没有 bf16/f16——注释自己写着 `this back end has no such element type`；要它具备是另一件事（换元素类型层），不是把规则补上 |

**一条判据（作者 06:10 的裁定，照抄）**：**"判断一条拒绝该不该写成'目标不具备'，只问一句：语言规定这个目标必须有吗？核心类型和核心运算，每个后端都必须有；只有对运行时本身的内省，可以随目标不同而缺席。"**

**据此，`f4cfd9688` 那一句被打回，理由写明**：`bf16`/`f16` 是**核心值类型**（`reduced-precision.md` b1：字面量、标量算术、比较、转换都有），**C++ 完全能表示它们**——16 位存储、在 f32 里算、就近平局取偶舍入一次，与 `dtype.h` 和解释器同一条规则。**所以缺的是一段代码，不是一种能力**；`rt::BF16`/`rt::F16` 与它们的翻译规则另派一条车道，这一页只记撤回。**"目标不具备"今天唯一的合法例子是对运行时本身的内省**（`std::mem::blocks()`）。

**落地的只有那一处**：`operator_rules.cpp:453` 那族（bf16/f16）的拒绝现在说能力边界——`std::cpp ... has type 'bf16'; std::cpp translates numbers (real and complex), ... : this back end has no such element type -- bf16 and f16 are not among the numbers it writes`，**没有 `yet`**；`refuse_tensor` 那一句**保留 `yet`**（它是没写的规则）。负例钉住这条：`test/plans/feat/trans/cpp.plan.toml` 的 `refuse_narrow_float`，断言 **输出没有 `yet`、且说出 `no such element type`**。

**所以五处里四处是"尚未实现"，一处是"目标不具备"**（`operator_rules.cpp:453` 的 bf16/f16）。**四处的收尾都是写规则表；那一处的收尾是决定要不要给这个后端窄浮点。**

## 十四、这一页堵着谁

- **① 的块外那一半**（`builder_operators.cpp:4403`、`:4427`，补丁 `D:\Temp\edges-block-outside.patch`）：门不再造之后，这个变量族正是靠版本边活着的；
- **③ 停造 ctrl**：`orderAfterLastWriter` 里那条 ctrl 边是这一格的临时替身，它必须在版本边成立之后才拆；
- **②a**：`hasWaitEdge` 恒假之后，`bypassValueGate` 依赖的前提没了。

**所以这一页是 ① 的唯一堵点。**

## 十五、撤回要小，边界要先立

**一条判据（作者 06:10）**：**"撤回一笔要和写一笔一样小：撤的时候只撤那一句，不顺手改注释以外的任何东西。"**

**这一条今天有一笔作证**：`2099d86ed` 只删了那一句"后端没有这种元素类型"（连同给出它的那个辅助函数），**处理器、类型表、另外四处 `yet` 一个字没动**，`feat/trans/cpp` 15/15——**撤回做得小，才有资格说"只有那一句错了"**。

**另一条（效应链第一笔，`lane-effects2`）**：**"一条链要从边界开始建：边界不变量立住了，后面每一类才有一把尺子；先从中间修，永远说不清修好了没有。"** 第一笔只加边界：`RuntimeDiag::PassBoundaryResources`（`0x00000D`），每个遍返回程序后比一次**语言自己的环境资源**集合（`stdout`/`stderr`/`stdin`/`clock`/`env`/`fs`）；**调度一分未改**。拼写 **1 处**（`effectResourcesViolation`），调用者 **1 处**（驱动里紧接次序检查那一段）。

## 十六、第二类先量后动：整树上 0 处，逐图拼法被量掉

**一条判据（作者 14:50）**：**"一条今天清单为空的不变量，价值全在'将来有人破坏它时它会响'；所以它的用例必须是'不该响时沉默'，而不是'响了'。"**

**先把数报到前面**：这条不变量在整树上跑（`lane-effects2`，`22bae9a11`，`total 8073 / pass 8063 / fail 1 / skip 7 / known 2`），**被它逮到 0 处**——唯一那条红是 `capacity.growth_simplify_gc_stress` 的 GC 抖动（串行 17/17）。**它今天一次也没响。**

**第二类试过一版，被数掉**：把同一个"资源集合"从整程序**收到单张图**（"这个遍保留下来的图，自己碰的环境资源不许变；除非这个遍往它里面内联了一个体"）。整树量出来 **9 红，全是 `std::opt` 的 `callback.*_opt` 一族**，报文是同一种形状：

```
Pass 'std::opt' changed the ambient resources ... before graph 1 (one graph), after nothing
```

**原因**：`std::opt` 把效应**搬出去**（搬到别处跑、或删掉没人要的），图的资源集合从 `{stdout}` 变成 `{}`——**这不是破坏，是"删/搬"**。把判据收成"只许增不许减"之后再量，**还是这 9 条**（它们的形状正是"从 nothing 增出来"，属于内联进来的那一类，而我按内联区域计数的豁免没接住它）。**所以逐图这一版今天撤回去了，代码回到 `22bae9a11` 的状态。**

**结论（给作者）**：**逐图这条尺子要成立，先得有一个"这张图跑的效应归谁"的口径**——内联把体搬进调用者图，那张图的资源就多了一项，是"归调用者"还是"归原来那个体"，今天没有这个口径；**没有它，逐图比只会把内联每一次都判成破坏**（9/9 都是这个形状）。**第二类不该是"逐图重复一遍"，而是"归属"本身。**

**还有一条（作者 16:00，第三类之后）**：**"一条今天清单为空的不变量，价值全在'将来有人破坏它时它会响'；所以它必须进闸门，不能只写在文档里。"** 第三条（端口与声明对得上，`RuntimeDiag::PassBoundaryPorts` `0x000013`）就是这样进的：**先量整树 0 处，再挂进闸门**，用例钉的是"不该响时沉默"（`pass_boundary.ports_match_declaration`）。
