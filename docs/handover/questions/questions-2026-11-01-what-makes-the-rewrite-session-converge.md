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

## 一之三补：去重**已经在了**，但键里有一样每轮都变的东西

`specializeGraphWithBindings` 开头就是按绑定组合查表（`src/runtime/draft_inline.cpp:824`-`:827`）：

```cpp
RuntimeSpecializationKey cacheKey = makeRuntimeSpecializationKey(baseGraph, bindings);
if (GCGraph *cached = session.findSpecialization(cacheKey)) { return cached; }
```

它是**真在跑**的，只是**从来没有命中**。把每次未命中的键打出来（删边的树，`export_loop.cml std::macro`，
20 秒，121 次未命中，`-vv` 里 113 个 `iterate$specN`）：

```
SPECKEY base=iterate tag=spec nonce=0 n=1 [k0 i0 c4 v0 f2]
SPECKEY base=iterate tag=spec nonce=1 n=1 [k0 i0 c4 v1 f2]
SPECKEY base=iterate tag=spec nonce=3 n=1 [k0 i0 c4 v2 f2]
SPECKEY base=iterate tag=spec nonce=5 n=1 [k0 i0 c4 v3 f2]
```

`k`（绑定种类）、`i`（形式参数序号）、`c`（类型 canonical id）、`f`（flag）**四轮完全一样**，只有 `v`
一路 `0,1,2,3` 往上走。`v` 是 `binding.staticValue.value`——**一个 slot 号**，即"这个静态值在哪一格"，
不是它是什么值。所以：**同一个绑定、同一个常量、同一个类型，只要这一轮把它放进了新的一格，键就不同，
副本就再造一张**。这不是"每轮多绑一层递归"（`bindings` 大小一直是 1），而是**键里记了一格会变的格子号**。

## 二、两条路的代价

- **甲：让键里那一格变成"它是什么值"，而不是"它在哪一格"。** 具体是把
  `RuntimeSpecializationBindingKey::value` 从 slot 换成值本身（内容寻址：类型 + 那几个字节），于是第二轮
  查表就命中、副本复用，会话收敛。代价：`specializations.h` 的键与比较要改，`value` 还兼作
  `NullSlot` 的"这一项不是值绑定"标记，要分开；判据是"`exported`、轮数 ≤ 5、`iterate$specN` 停在个位数"。
  **顺带要答**：复用同一张副本会不会让两处不同位置的调用共享一张图——同一组绑定（种类、序号、类型、值）
  的副本按定义应当逐字节相同，所以复用安全；若这点不成立，就把去重范围收窄到同一个 `baseGraph` 之内
  （今天的键已经带 `baseGraph`，即已收窄）。
- **乙：把 `"spec"` 这一路也纳入 `kTypeSpecializationFixedPointGuard`**，让它撞上限制时给出编译错误而不是
  挂死。代价：`export_loop.cml std::macro` 会从 124 变成一条明确的"特化没到定点"报错，**仍然不是
  `exported`**，所以它不能替甲，只能当甲的护栏。

## 三、我方默认建议

**甲**，而且这一刀已经小到一行：**把键里的 `value` 从"slot 号"换成"值本身"**。理由就是上面四行读数——
同一个绑定、同一个类型、同一个常量，只有格子号在变；键比的是格子号，所以永远不命中。
判据：`exported`、轮数 ≤ 5、`iterate$specN` 停在个位数、边留着时与今天逐字节相同。

## 请作者答一句

**甲（键按内容寻址，slot 只作取值的入口）还是乙（同时给 `"spec"` 加一道定点护栏）？**
若选甲，请确认判据是"`export_loop.cml std::macro` 打出 `exported`、轮数 ≤ 5、
`ort_loop_loaded_weights` 仍打出 `Loop`"，以及**"复用同一组绑定的副本"是被允许的**（同一组绑定的图逐字节
相同，两处调用共享它没有可观察差别）。