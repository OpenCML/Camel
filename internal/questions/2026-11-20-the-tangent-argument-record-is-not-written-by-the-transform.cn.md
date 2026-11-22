# 「哪个实参是切向」这张位图，`jvp`/`vjp` 造的调用一张都没写

**问句（一句）**：`GCGraph::callTangentArgsOf` / `isTangentArgument`（`include/camel/runtime/graph.h:953-963`）的语义正是「第 `i` 个普通实参是不是切向」，位置无关，注释写明它由**规则展开**在造调用时写入 —— 而 `jvp` 与 `vjp` **直接造出来的调用一个都不写**（实测全是 `0x0000`）。于是凡是要「知道哪个实参是切向」的读者（`split` 的割首当其冲）只能退回按位置猜，**而位置不固定**。**这一笔该由造图那一步（`jvp`/`vjp` 写调用时填位图）承担，还是由读者侧另找判据承担？**

## 默认建议（本轮按此结案）

**由造图侧承担：`jvp`/`vjp` 写调用时把 `callTangentArgsOf` 填上，读者一律改读它，删掉按位置/按种类的猜法。** 理由见 §二：读者侧的三条路都已被量倒，而它们被量倒的**原因完全相同** —— 被调方的位置不固定、位图又是空的，读者没有任何位置无关的信息可用。

## 一、位图的语义是现成的（逐字）

`include/camel/runtime/graph.h:953-963`：

```
/// The arguments of FUNC node `ref` that are tangents, one bit per argument position (bit `i`
/// set when the i-th normal input is a tangent): the record a rule's expansion wrote when it
/// produced the call (`GCNode::tangentArgs`), 0 when the node is not such a call.
uint16_t callTangentArgsOf(gc_node_ref_t ref) const;
/// Whether argument `index` of FUNC node `ref` is a tangent: bit `index` of ...
bool isTangentArgument(gc_node_ref_t ref, size_t index) const;
```

⇒ 这张位图**就是**「按身份认、不按位置认」的现成件，而且是图自己写的。问题只在于：**它没被填。**

## 二、实测：这一格的三次尝试，三次都破同一对用例

读者侧改动落在 `split.cpp` 的 `halfInputsOf`（值半取哪些实参）。三点读数，全部在 `feat/compile/transforms` 上量：

```
① 条件 = 「签名来自一个函数值」⇒ +1        ⇒ 199 pass / **fail 2**
② 条件 = 算术「有槽」                      ⇒ 199 pass / **fail 2**（在 PASS 用例上巧合成立）
③ 条件 = `kind == GCNodeKind::Call`        ⇒ 199 pass / **fail 2**
```

三次破的都是同两条：`transforms.split_of_a_call_through_a_value` 与
`value_call_with_a_function_parameter`（基线 `201 pass / 0 fail / known 13`）。**三次都已撤回，树上无残留。**

## 三、为什么三次都错：被调方的位置不固定，而位图是空的

`dataInputsOf` 的 `CALLSHAPE` dump（`feat/compile/transforms` 全跑）：

```
CALLSHAPE graph=jvp_of_use ref=5 kind=Call named=0 norm=4 with=1 mask=0x0000 data=
   slot=0 kind=Port type=float
   slot=1 kind=Port type=float
   slot=2 kind=Oper type=(f: (y: float) => float) => (float, float) => (float, float)   ← 被调方在**末尾**

CALLSHAPE graph=jvp_of___g_359__ ref=5 kind=Call named=0 norm=4 with=1 mask=0x0000 data=
   slot=0 Port · slot=1 Port · slot=2 Oper                                              ← 同上

CALLSHAPE graph=jvp_of_f_at__first_with_as_param ref=2 kind=Func named=1 norm=2 with=0 data=
   slot=0 Port · slot=1 Port                                                            ← **没有被调方槽**
```

而 `nested` 的那一条（`split_of_jvp_of___g_361__…`）里，`dataInputsOf` 的第 0 项**就是** `Oper`
（`slot=0 kind=Oper`，被调方在**首位**）：

```
CALLSHAPE ref=4 kind=Call ... slot=0 kind=Oper · slot=1 kind=Port(index=0 VALUEHALF)
```

⇒ 三条事实，缺一条都不足以否掉这一路：

1. **被调方的位置不固定**：`jvp_of_use` 在末尾 `2`，`nested` 那条在 `0` ⇒ 按位置取**不可依赖**。
2. **`Func`（`named=1`）根本没有被调方槽** ⇒ 实参从 `0` 起数在它身上是对的，于是「一律 +1」会跳过真实参。
3. **两条例据的 `callTangentArgsOf` 都是 `mask=0x0000`** ⇒ **唯一位置无关的判据没被填**。

⇒ 所以「偏移」这一路**已三次被量倒**（签名来源 / 算术有槽 / `kind == Call`），**不要再试第四次**。
根因是**图没记「哪个实参是切向」**，不是 `split` 找不到它。

## 四、上游那一件有多大（估）

`GCNode::tangentArgs` 的记录位已存在（`graph.h` 的注释点名了它），所以缺的是**写**的一处：
`jvp`/`vjp` 造调用节点时（`modules/transforms/jvp.cpp` 造 `Call`/`Func` 的那几处、以及 `vjp` 一侧的对应处），
把「第 `i` 个实参是切向」按它自己的构造知道的东西写进那个字段。难点不在写，而在**每一处造调用的地方都要知道
自己的实参里哪些是切向**：`jvp` 造提升后的调用时，前一半是值、后一半是切向，这是它构造时就知道的；
`vjp` 一侧同理。**估一格以内**：三到五处造调用点 + 一条「读者改读位图」的收尾，且每一处都能被
`split` 现有的那两条用例直接判（`split_of_a_call_through_a_value` 不许偏移、`kind=Call` 那条要偏移）。

## 五、这一格不派代码

`split` 侧的偏移已撤回三次，树上干净（`f07f7e3c2`）。上游那一件（让 `jvp`/`vjp` 写位图）**不在这里开工**，
理由：它是**另一条车道的造图侧**，且会同时改动 `jvp`/`vjp` 两处的调用构造，与 `split` 的判据改动必须一次落地、
一起量三条基线 —— 半做会留下「位图写了一半、读者还按位置」的中间态，而那正是这一格三次红的形状。

## 六、这一格走完七步之后的五行结论（2026-11-20 晚，`45369b138` 之后）

1. **位置不可依赖**：`dataInputsOf` 里被调方在 **0**（`nested` 的 `split_of_jvp_of___g_361__…`）、在**末尾 2**
   （`jvp_of_use`，`[Port, Port, Oper]`）、或**根本没有槽**（`Func`，`named=1`）三种都有；三次「按位置加偏移」
   的尝试（签名来源 / 算术有槽 / `kind == Call`）三次都破同一对闸，已全部撤回，**不要再试第四次**。
2. **位图存在但生产者不填；填上之后墙不动** ⇒ **不是缺记录**。`45369b138` 让 `jvp` 写 `tangentArgs`、让 `split`
   在位图存在时改读它（三闸全绿、两条例据 PASS、读者有回退路径），而 `nested` 的拒句**一字未变**。
3. **调用是满的** ⇒ **不是少实参**。曾读到的「`norm=4` 而 `data=3`」是拿**图的签名**比**节点的实参**两个层的数；
   `dataInputsOf` 是「普通输入在前、`with` 在后」（`split.cpp:100-102`），末尾那个 `Oper` 是 `with` 里的被调方。
4. **那句拒绝的数字来自图签名端口号**（`split.cpp:771-780`：`portIndex[ref]` → `parameterOfPort(funcType, index).first`），
   与调用实参无关 ⇒ **`halfInputsOf` 那一路永远改不动它**。
5. **那个切向入口 Port 不在值半的起点集合里**（`portOf[index] == kInvalid`）⇒ **起点划分不是根因**；它是被**一条边**
   接进来的 ⇒ 要量读取方。

## 七、第八步：读它的那个节点是谁

探针读三个失败图形（`nested`，逐字）：

```
TANGENTREADERS graph=jvp_of_split_of_jvp_of_square__first_with_as_param_forward port=1
   reader=3 kind=Func atInput=2 copiedYet=0 type=float
   reader=3 kind=Func atInput=3 copiedYet=0 type=float
TANGENTREADERS graph=jvp_of_split_of_jvp_of_quartic__first_with_as_param_forward port=1
   reader=5 kind=Func atInput=3 copiedYet=0 type=float
   reader=7 kind=Func atInput=3 copiedYet=0 type=float
```

⇒ **读者就是那个提升调用本身**（`Func`），切向入口落在它的第 2 / 3 号实参上 —— 正是
`(v0, v1, d0, d1)` 的后半。按「它在值半里的理由」分岔，命中的是**第一支**：**它产出的东西值半要用，
这是正常的**；缺的不是可达性，而是「值半不该把切向交出去」这一条 —— 值半要复制这个调用，就必须把它
复制成**未提升**的调用（`v0, v1` 两个实参，答案是值而不是对），而不是照抄四个实参。
⇒ 于是这一格的问题定在：**`split` 复制一个提升调用时，是照抄它的实参表，而不是把它还原成原调用。**