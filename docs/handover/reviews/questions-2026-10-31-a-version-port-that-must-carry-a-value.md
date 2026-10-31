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

## 九、判空用的查询必须只答不造

**一条判据**：**判空用的查询必须只答不造；造的查询会留下没人依赖的节点，而那条节点会被正确地说成"从不执行"。**

今晚两个实例：

1. **`Builder::orderAfterLastWriter`**：它只想知道"这个读有没有自己的版本"，却调了 `valueVersionOf`——而后者在写者是调用时会**造**一个 `:var/after`。探针造出的节点没有用户，诊断随即报 `the call of '__var_after__' has side effects but is never executed`。**诊断是对的：那条边确实没人用。** 修法是另立 `hasValueVersion`（只答不造）。
2. **`nodeDraftOf`**（`namenature` 那条车道）：同样的形状——判空用了一个会建节点的查询。

**为什么值得记**：这类错误不会崩，也不会错值；它只是**多出一条没人用的边**，然后被一条正确的诊断说穿。**看见那条诊断时，先问"是谁造了它"，而不是"这条诊断是不是太吵"。**

## 十、三类红，以及它们各自的账

**一条判据**：**一条新边会同时改三个地方的账；但先分完类再动手——今晚 7 条红里只有 2 条是我们的，2 条等作者，2 条是别人的 footprint，1 条是重录。**

发出一条边之后，整闸的读数是这样分的（`total 8040 / pass 8024 / fail 7`，合并 `develop` 之后）：

| 类 | 条数 | 名字 | 谁的 |
|---|---|---|---|
| **(a) 检查器该跟着 3.1 改** | 2 | `source.declared_resources`、`poison:…:source.declared_resources` | **等作者**：这一页在 `questions/` 里（随第 61 趟上线），**作者下轮读，不催** |
| **(b) 我们改错了** | 2 | `pass_export_var_param_inlined`、`ort_var_parameter` | **我们的**，但**修在 `writers` 那半边**：`bump(acc)` 的形状写者会登记，可那个两参重载固定 `throughCallee = false`（`builder.h:2255-2256`），而 `writeBeforeCall_` 只在真时填（`builder_expr.cpp:620`）——所以 `:var/after` 的第一实参拿到了裸单元 |
| **(c) 黄金/期望该重录** | 1 | `docs.tutorial_images.current` | **我们的**：`10_var_order` 重画（`84516a3d1`），只换 `.dot`/`.svg`，渲染器自己的时间戳噪音不提交 |
| **(别人的) footprint** | 2 | `core`、`poison:modules.resources:core` | **`writers`**：`:var/after` 多一条 `read variable(operand 0)` 声明 |

**这一分法的用处**：**`fail 7` 里我这一半只欠 2 条，而且已经定位到 `writers` 的一行**。不分类的话，7 条会一起算在"发出这条边的人"头上，下一个人就会去改检查器和 footprint。

**附一条今晚的更正**：有的"搬一半"不是搬的时候漏了，是**递进去的那个 id 从一开始就不属于这一族**——`variableAfterCall` 在没有 `writeBeforeCall_` 登记时退回单元本身，而单元可能声明在另一个函数里（见前一轮的定位：第一实参是 `:var/new`，不是版本）。

## 十一、附注：形状一的补丁

形状一的实现（`valueVersionOf` 返回调用节点）**只存在于补丁里**：`D:\Temp\version-edge-a.patch`，**5674 字节**，内含 `builder_expr.cpp` 的 10 行与枚举两态化的改动（枚举两态化已单独上车，见 `821185288`）。**它是形状一的唯一记录**，重跑本页第二节的形状一读数时用它。

## 十二、这一页堵着谁

- **① 的块外那一半**（`builder_operators.cpp:4403`、`:4427`，补丁 `D:\Temp\edges-block-outside.patch`）：门不再造之后，这个变量族正是靠版本边活着的；
- **③ 停造 ctrl**：`orderAfterLastWriter` 里那条 ctrl 边是这一格的临时替身，它必须在版本边成立之后才拆；
- **②a**：`hasWaitEdge` 恒假之后，`bypassValueGate` 依赖的前提没了。

**所以这一页是 ① 的唯一堵点。**