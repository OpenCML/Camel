# 第 ②a 步：路径已定死，守卫确实触发了，失在前面 5 厘米

日期 2026-11-15。分支 `lane-varscope`，基线 `b3ea288d3`。临时打印已全部删除，`src/`、`include/`
与基线逐字节相同（`git diff --stat` 为空）。

## 打印原文（`q1`：`var x = 0; if c then { x = 1 }; println(x)`）

```
[DBG-A] buildBranch entered: arms=-1 valueless=0 gctSize=3
[DBG-C] joinsNoValue=1 dropsArmValues=1 takesNoValue=1 absentElse=0
[DBG-B] addArm: joinTypeSet=0 joinTypeCode=-1 dropsArmValues=1 absentElse=0 joinsNoValue=1 withInputs=0 arms=1
[DBG-B] addArm: joinTypeSet=1 joinTypeCode=5 dropsArmValues=1 absentElse=1 joinsNoValue=1 withInputs=0 arms=2
[DBG-E] at capture: joinsNoValue=1 drops=1 absent=1 wits=2 arms=2
[DBG-D] mergeArmVariables: vars=1 variable=3 wrote=1
```

## 结论

1. **语句形 `if` 走的就是 `Builder::buildBranch`**（`arms=-1` 即 `arms == nullptr`，`valueless=0`，
   `gctSize=3`）。不是别的 visitor。
2. `joinsNoValue=1`，由 `dropsArmValues=1`（`if` 无 `else`，GCT 判的）和 `takesNoValue=1` 共同成立；
   `absentElse` 直到第二个臂（空 else）加进来才变 1。
3. 臂的值成为合并点的 `WITH` 输入在 **`builder_control.cpp:2796`**
   （`linkNodes(LinkType::With, armList[i]->output, joinNode)`），**不在** `addArm` 里——
   这也是两行 `addArm` 的 `withInputs` 都是 0 的原因。
4. **`[DBG-E]` 证明上一轮那段守卫全部条件都成立**（`joinsNoValue=1`、`drops=1`、`wits=2`、`arms=2`、
   被写变量 1 个）。所以它不是"没走到"，而是**走到并进入了块内，却没生效**。

## 因此失在块内的类型检查，位置在

`builder_control.cpp` 里 `joinWithInputs` 捕获之后（`:2820` 附近，`2796` 的 `WITH` 链之后、
`rewriteNode` 之前）那段里我写的两句：

```cpp
const node_handle_t armValue = nodeDraftOf(variable)->ref(joinWithInputs[0]);
const node_handle_t previous = nodeDraftOf(variable)->ref(*beforeValue);
if (nodeTypeOf(armValue) == valueType && nodeTypeOf(previous) == valueType) { ... }
```

下一次只需要在同一位置**再打一行**这三个类型（`nodeTypeOf(armValue)`、`nodeTypeOf(previous)`、
`valueType` 的 code），就知道是哪个比较没过；很可能是 `joinWithInputs[0]` 的 GATE 出口类型不是
`int`（闸门节点的类型记为别的东西），而不是槽位顺序错。改动位置本身是对的。