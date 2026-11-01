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
一路 `0,1,2,3` 往上走。`v` 是 `binding.staticValue.value`。

## 一之四补：把键换成内容之后——**上一段读错了，`v` 不是"格子号"，它真的是 0、1、2、3**

按上一段的结论，把 `RuntimeSpecializationBindingKey` 的 `value` 从 slot 换成**内容**（类型拼出来的字节），
`slot` 只留作读值的入口与 GC 重定位用，相等性与哈希都改看字节。删边的树上重跑：

```
SPECKEY base=iterate tag=spec n=1 [0,0,0000000000000000]
SPECKEY base=iterate tag=spec n=1 [0,0,0100000000000000]
SPECKEY base=iterate tag=spec n=1 [0,0,0200000000000000]
```

**内容是 `0x00…00`、`0x01…00`、`0x02…00`，不是同一个常量的三个格子——是真的 0、1、2。** 所以上一段那句
"同一个常量、只有格子号在变"**是错的**，错在把 slot 号当成了内容的替身：slot 号确实在变，但内容也在变，
两者一起变，我没有分开看就下了结论。

**改法的读数（四条判据）**

| 判据 | 读数 |
|---|---|
| ① 删边 + 内容键 → `exported` | **没有**：`exit 124` |
| ② 轮数 ≤ 5 | **没有** |
| ③ 命中/新建 | **命中 113 / 新建 114**（改之前是 0 命中 / 121 新建）——去重**真的开始命中**了 |
| ④ 边留着的树一致 | **有**：`exported`、onnx **156/156**、`ort_loop_loaded_weights` PASS |
| 全量闸门 | **红**：`total 8294, pass 8020, fail 262` |

**结论两条**，都写下来：
1. **内容键是"对的事、错的地方"**：它让键稳定了（命中 113 次），但 `iterate` 每一轮绑的值**本来就是新的**
   （0、1、2……），所以它救不了这个 124；而"同内容即同副本"的复用又在别处撞出 **262 条红**，说明两张副本
   即使绑定内容相同也**不是可互换的**。这两点合起来否掉了"按内容去重"这条路。
2. **真正的过程是"特化把递归当循环展开"**：`iterate` 被以 0 绑一次，副本里递归调用拿到常量 1，再以 1 绑一次，
   再 2……**每轮展开一层，值单调递增，永不重复**。所以键、去重、命中率都不是关键，**缺的是一道"展开到哪为止"的界**。

## 二、两条路的代价

- **乙（原方案里被我排在后面的那条，现在是主线）：给 `"spec"` 这一路一道界。** 两种界：把 `"spec"` 也纳入
  `kTypeSpecializationFixedPointGuard`（撞界即报错，`export_loop.cml std::macro` 从 124 变成一句明确的
  "特化没到定点"），或者给它一个"同一条递归只展开一层"的判据（连 `onCallCycle` 的调用不再按值特化）。
  后者才可能让 `exported` 回来；前者只能把挂死变成诊断。
- **甲（内容键）：已试，否掉**（262 条红 + 不解决 124），代码未提交。

## 三、我方默认建议

**乙**，而且要先选哪一种界：我建议**"连 `onCallCycle` 的调用不再按值特化"**（递归调用不因常量而展开），
因为它才可能让 `exported` 回来；定点护栏只把挂死变成报错，可以作为它的副产物一起加。
判据不变：`exported`、轮数 ≤ 5、边留着时逐字节相同、onnx 156/156。

## 请作者答一句

**乙的界选哪一种：①"连 `onCallCycle` 的调用不再按值特化"（可能救回 `exported`），
还是 ②只加定点护栏（把 124 变成明确报错、不救 `exported`）？**