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

**一条待验**：②a 之后要再验一次——那时 ctrl 停了，**链必须真的独自答**；今天 `|| isSyncStatement` 是等价的，明天它要独自成立。

## 六、附注：形状一的补丁

形状一的实现（`valueVersionOf` 返回调用节点）**只存在于补丁里**：`D:\Temp\version-edge-a.patch`，**5674 字节**，内含 `builder_expr.cpp` 的 10 行与枚举两态化的改动（枚举两态化已单独上车，见 `821185288`）。**它是形状一的唯一记录**，重跑本页第二节的形状一读数时用它。

## 七、这一页堵着谁

- **① 的块外那一半**（`builder_operators.cpp:4403`、`:4427`，补丁 `D:\Temp\edges-block-outside.patch`）：门不再造之后，这个变量族正是靠版本边活着的；
- **③ 停造 ctrl**：`orderAfterLastWriter` 里那条 ctrl 边是这一格的临时替身，它必须在版本边成立之后才拆；
- **②a**：`hasWaitEdge` 恒假之后，`bypassValueGate` 依赖的前提没了。

**所以这一页是 ① 的唯一堵点。**