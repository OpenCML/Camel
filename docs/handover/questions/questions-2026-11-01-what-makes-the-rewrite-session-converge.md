# 重写会话为什么会一直"有变化"

> **问什么**：S1 的 `124`（删掉 `builder_control.cpp:2864` 的 BRCH→臂入口 ctrl 边后，`export_loop.cml`
> 在 `std::macro` 下跑不完）已经定位到函数了：不是导出器，也不是"某处在求值一条臂"，是**优化器的运行时
> 重写会话不收敛**。这一页问的是：**让这个会话收敛，靠什么？**
> **堵着谁**：S1 的收尾（这条边删不删）。
> **为什么我不自己定**：这是"每轮都算有变化"的口径问题，改法有两种，代价不同。

## 一、现场（实测，删边的树，`-vv`，`timeout 25`）

`camel -vv test/cases/modules/onnx/export_loop.cml std::macro`：25 秒内

- `Runtime rewrite session started` **214 次**；
- `Runtime rewrite collected reachable closure with 4 graphs` 428 次，每次解码 `iterate`、`halve`、`model`、
  `__g_52__` 四张图；
- 每轮都 `Graph '…' collected 0 inline candidates`（**0 个**）、`Specialize FUNC … bindings=0`（**0 条**绑定）、
  `Devirtualized`（**0 次**）；
- `constructInPlace … name='__g_52__'` 反复重建同一张图，`__g_52__` 的 FUNC 节点号一轮加 1
  （`2038 → 2039 → …`），即**每轮多一个节点**。

**函数与行**：

| 位置 | 做什么 |
|---|---|
| `src/passes/opt/inline/engine.cpp:1301`-`:1335` `applyOptimizeRewritePass` | 最多跑 `kMaxSessions = 32` 个会话，每个会话返回非空就继续；注释自己写着"the bound only guards against a rewrite that never settles"——**今天就是那个 never settles** |
| `src/passes/opt/inline/engine.cpp:780` `applyRuntimeOptimizeRewrite`、`:802` 的 `while (true)` | 每轮 `collectReachableRuntimeGraphs()`（`:804`）后把整张闭包重新解码、重扫 |
| `src/passes/opt/inline/engine.cpp:1124` `if (!roundChanged) break;` | 会话内退出的唯一条件 |
| `src/passes/opt/inline/engine.cpp:919`-`:957` | 置 `changed` 的三处：特化（`:927`）、去虚拟化（`:949`）、内联（`:1083`） |

**作者点名的假设（"宏求值器把分支的递归臂当成无条件求值"）：没有成立。** 读数里没有任何一处求值一条臂——
整段是**解码 + 重写**，图从未被执行；每轮的内联候选、特化绑定、去虚拟化全是 0，而图仍在长。所以那条边
今天撑着的不是"要不要算这条臂"，而是"这一轮算不算有变化"。

## 一之补：谁在说"这一轮变了"——已指名（临时标签已删）

在三处置 `changed` 的语句旁各挂一个标签，并在会话每轮结束前打印一行：

```
REWRITE changed=<specialize|devirtualize|inline|none> roundChanged=0/1 inlines= gains= graphs= nodes=
```

`export_loop.cml std::macro`，同一台机、同一条命令、`timeout 25`，两个状态各一次：

| 状态 | 结果 | 轮数 | 标签 | inlines / growth / nodes |
|---|---|---|---|---|
| **边留着（今天）** | **exit 0** | **5** | 前两轮 `inline`，之后 `none`（收敛） | 2 / 30 / 39→82 |
| **边删掉** | **exit 124** | **230+** | **全是 `inline`** | 每轮 1–2 / 4936→5046 / 4078→4184 |

**谁在说变了：内联那一处（`engine.cpp:1091` 的 `appliedInlineCount++` 旁）。** 它每轮都能内联一次，
是因为一个把"这条臂的调用"判成普通调用的判断——`isDraftBranchArmHead`（`engine.cpp:325`-`:338`）：

```cpp
for (gc_node_ref_t pred : draft.waitInputsOf(nodeId)) {   // 就是被删的那条 ctrl 边
    if (predHeader && predHeader->kind == GCNodeKind::Brch) return true;
}
```

它的返回值 `isArm` 同时喂给 `shouldInlineTarget(isSmall, isArm, …)` 和
`suppressNonRecursiveArmInlining`（`engine.cpp:1047`-`:1064`）。边一没，臂里那个调用就不算"臂"，
于是**每一轮多内联一层递归**：`growth` 每轮 +22、`nodes` 每轮 +34，`kMaxSessions = 32` 被顶满，
`applyOptimizeRewritePass` 便认为程序变了、再来一轮，直到超时。

**作者那句"某处判断'这条臂要不要算'读的还是 ctrl 边"，在 `isDraftBranchArmHead` 上成立**——只是它判的
不是"要不要算这条臂"，而是"**这条臂里的调用要不要内联**"。

## 一之再补：改了 `isDraftBranchArmHead` 之后——**它不是那根撑杆**

按上一节的结论把 `isDraftBranchArmHead` 改成问区域（`controlRegionOf` 是 `Arm`、且没有同臂的节点排在它
前面），删边的树上重跑：

| 判据 | 读数 |
|---|---|
| ① 边删掉 + 改判断 → `exported` | **没有**：`exit 124`，尾部仍是那两行张量 |
| ② 轮数 ≤ 5 | **没有**：**536 轮**（改之前是 230+） |
| ③ 边留着 + 改判断 → 与今天一致 | **有**：`export_loop.cml std::macro` `exit 0` / `exported`，onnx 计划 **156/156**（含 `ort_loop_loaded_weights` 的 `Loop`） |

**所以那根撑杆不是臂的判断。** 再往上问一层"到底在反复内联谁"，`-vv` 的 `Inline probe` 给出了名字：

```
Inline probe 'model' -> 'iterate$spec217': small=true acyclic=false calls=1 closure=false.
Inline probe 'model' -> 'iterate$spec219': ...
Inline probe 'model' -> 'iterate$spec221': ...
```

**每轮内联的是一个新造出来的特化副本 `iterate$specN`**（20 秒里出现 113 个不同的 `iterate$specN`），
造它的地方是 `specializeGraphWithBindings(…, "spec", session.nextSpecializationNonce())`
（`src/runtime/draft_inline.cpp:1135`-`:1140`，由 `specializeDirectFuncInDraft` 在 `bindings` 非空时调用）。
副本的名字每轮带一个新 nonce，所以"这张图我见过"的判断永远不成立；而本该拦住它的那个定点守卫
（`kTypeSpecializationFixedPointGuard`，`:1120`-`:1131` 的报错）只对**类型**特化生效，`"spec"` 这一路不在其中。
**这才是"每一轮算不算有变化"里的那个'变化'。**

## 二、两条路的代价

- **甲：让会话真的收敛。** 现在范围已经收窄到一处：**每轮新造一个 `iterate$specN` 特化副本**。要让会话
  收敛，就得让"同一个绑定组合"认得出自己已经做过一次（nonce 换成按绑定组合去重），或者让
  `kTypeSpecializationFixedPointGuard` 那条定点守卫把 `"spec"` 这一路也算进去。代价：一笔动的是特化的
  身份判断，会牵动所有特化过的调用点，判据是"`export_loop.cml std::macro` 打出 `exported`、轮数 ≤ 5、
  `iterate$specN` 的数量不再增长"。
- **乙：接受这张图每轮长一个节点，把超时变成一句诊断。** 会话撞上 `kMaxSessions` 时给出明确的
  "这个程序的重写不收敛"而不是挂死。代价：那条边仍然删不掉（`Loop` 还是变 `If`），只是不再挂死。

## 三、我方默认建议

**甲**，下一刀：**在 `specializeGraphWithBindings` 之前把这次特化的"键"打出来**（`calleeGraph->name()` +
每个绑定的 `kind/index/静态值`），看两轮之间的键到底哪里不同——今天只知道名字不同（`$spec217` →
`$spec219`），不知道**内容**是否相同。键相同就说明缺的是"这个组合我做过"，键不同则要再问一句
"为什么这个绑定组合每轮都会变出一个新的"。

## 请作者答一句

**甲（把特化的键打出来，让它认得出自己做过一次）还是乙（把它变成诊断、先不删边）？**
若选甲，请确认判据是"`export_loop.cml std::macro` 打出 `exported`、会话数 ≤ 1、`ort_loop_loaded_weights`
仍打出 `Loop`"。