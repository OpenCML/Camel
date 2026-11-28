# 卡：a captured closure that the export path never calls should defer

- 出自 `task-383`（`lane/loop-tape-values`），由 `lane-block-hash` 在读完形状 A 的第二个拒绝点后开。
- 位置：`src/stdlib/onnx/exporter.cpp`，`Evaluator::evalFill` 的**闭包分支**（`4762`-`4789`）与它后面的拒绝（`4790`-`4796`）。
- 判据（落不落）：`export_loop_carried_array_field` 由红转绿，**或**它的失败理由换成更靠近语义真因的一条；其余红一个不许变。

## 形状：同一件事只做了一半

`evalFill` 对**聚合元素里**的闭包已经有 defer：一个 tuple/struct 元素若类型是 `Function`、模板是常量，
而简化没把它折掉，就把这一格填成 `Value::constant(NullSlot, elemType)`——注释写得很直白
（`4715`-`4720`）：

> A closure element of a tuple or struct that simplification did not fold captures a value that depends
> on the model input (e.g. a branch arm's (value, pullback) tuple whose pullback is never called on the
> exported path). It has no ONNX representation and emits nothing, so it is deferred instead of failing
> the whole aggregate: the element is evaluated only if an access reads it back.

对**闭包本身**（也就是那个"从不被调用、只是被带着走"的函数，正是它自己成了被填的那个聚合）没有这条：
`4762` 的分支要求

1. `closuresWanted_ > 0`（线性递归需要闭包时），或
2. 被填的值里**已有**一个闭包（`std::ranges::any_of(values, isClosure)`），

两者都不成立时，落到 `4790` 的拒绝：

> a value that depends on the model input is stored in an array or a closure; pass it as a function
> argument instead

这不是特例，是**同一个判断在两层上只落了一半**：元素层的闭包可以 defer，成为整格的那个闭包不行。
所以修法是把元素层已有的 defer 判据推广到闭包这一层，而不是给某个程序开洞。

## 读数（`task-383` 形状 A 上撞到的那一次，留档）

```
[t383t] evalFill refused: type=(float) => (float[4], float[4]) pure  sourceConstant=1
[t383t]   value[0] = sym tensor[]      from tensor:sum        # 秩 0 标量 = loss
[t383t]   value[1] = sym tensor[4]     from tensor:multiply
[t383t]   value[2] = const float[4][4] from <non-oper>        # xs
[t383t]   value[3] = sym tensor[4]     from tensor:reshape
```

- 被填的东西**是一个闭包**（类型 `(float) => (float[4], float[4]) pure`），不是数组、不是元组实例；
- 四个 value 是它的**捕获**，其中三个依赖模型输入（`sum`、`multiply`、`reshape`），一个常量（`xs`）；
- `sym tensor[]` 是**秩 0 标量**（`describe` 打印的 `[]` 是秩 0），不是"空形状的数组"——这一条曾被我误读过一次，留在
  `task-383` 的卡里作为更正。

## 影响面（不止 383）

- 任何"逆向里有个从不被调用的 pullback 闭包被元组带着走"的图都会撞上：分支臂的 `(value, pullback)` 元组、
  收集循环的逆向、任何 `with_grad` 里被简化留下的闭包。`4715`-`4720` 的注释里已经点了这个形状
  （"a branch arm's (value, pullback) tuple whose pullback is never called on the exported path"），
  只是当时只堵了元素那一层。
- **与 `:struct/append_arr`、sequence 都无关**：那条线（`task-383` 的四段补丁）解决的是"数组被携带成
  sequence"，四段落完之后撞到的正是这张卡；四段本身**不是**这个缺口的解（见下）。

## 为何四段补丁先不落（留档，`4014ad0e7c`）

`task-383` 的四段落（`internal/collab/task-383-four-segments.patch`）能让 append 那关通过，但通过之后
撞到的就是这张卡的缺口。先把四段落上，只会把 `export_loop_carried_array_field` 的失败理由从
"append 没有降级"换成"闭包捕获不能物化"——**拒绝挪了地方，语义真因没靠近到能判绿**。所以四段留在枝上作补丁，
等这张卡解决、两者一起落，才谈得上把那条红改绿。

## 先读再动

动手前先读 `closuresWanted_` 的语义：谁设它、为谁设、为什么"闭包本身被捕获"这条路径不设它。
判据只有一句：**导出路径不调用它 ⇒ 没有值要物化 ⇒ defer 是对的**。这句话写不进提交正文，就不要改。
## `closuresWanted_` 的语义（先读，已读）

声明在 `exporter.cpp:418`：`int closuresWanted_ = 0; // > 0 while functions are kept as values instead of deferred`。
它只**六处**被拉开，而且每一处都是"**马上就要调用/探测这个值**"的现场：

| 位置 | 是什么 |
|---|---|
| `3041` `linearBody` | 线性递归的体：递归函数本身要被当作值保留 |
| `3411`/`3423` `probeSpecialClosure` | 探测：先试着评估看它是不是（或含）特殊闭包，失败则视为 defer（注释：*"not evaluable: stays deferred, and fails only if it is ever needed"*） |
| `3597` `evalEagerBranch` | 两支都算、择一，臂里可能有闭包 |
| `3776` `applyClosure`/调用 | 被调用者 |
| `3620` `evalIndirectCall` | 间接调用的 callee |

⇒ 语义是**调用现场**的标记，不是"这个值被谁带着走"的标记。所以一个**只是被聚合捕获、导出路径上从不调用**的闭包，
在 `evalFill` 里看到的 `closuresWanted_` 恒为 0 —— 这不是遗漏，而是这个变量本来就不回答这个问题。
修法因此不是"给它加一处 `++`"，而是"在 `closuresWanted_ == 0` 时把闭包 defer"，与元素层的 defer 同一口径。

## 实测（读数，三次运行）

1. **四段 + defer**（`feat/modules/onnx` 全跑）：`total: 160, pass: 148, fail: 11, known failures: 1`
   —— 与 pristine tip 的红集**逐名相同**（`as_refusal.constant_fails.{operator,pass}`、`export_impure_op`、
   `export_loop_carried_array_field`、`export_loop_for`、`export_unsupported_op`、`ort_core_slice`、
   `ort_loop_loaded_weights`、`pass_export_equals_export_model`、`pass_export_refusal_located.request_in_arm_json`、
   `resnetbn_train_step_export`（超时，属既有红）），**没有新增红，也没有把别的红改绿**。
2. **只打 defer（四段已回退）**跑 `export_loop_carried_array_field`：
```
...cml:22:9: [Error]: An error value reached the exit of 'main' without being handled:
  ExportUnsupported: in 'walk': a recursive call changes argument 2 of a recursion whose depth depends
  on the model input: it is the leaf 'xs', an int array, whose length changes from one round to the next
  (the body appends to it or concatenates it), and a Loop carries a state whose shape is the same on
  every round.
```
⇒ **defer 单独就把这条红的失败理由从"类型打印器拼出来的说法"换成了用例 `desc` 要的那条语义事实**（还点出了 leaf `'xs'`）。
3. 但**位置变了**：用例断言 `line 22, column 81`（进入递归的那次调用 `walk(x, { xs: [0], k: 0 })`），
   现在报 `22:9`（`main` 里 `export_model` 调用处）⇒ `located` 那层"把整段递归的拒绝定位到入口调用"的包裹没生效。

## 判据结果：**仍不落**，但缺口缩小了一句

- `export_loop_carried_array_field` **没有转绿**；
- 失败理由**变成了更靠近语义真因的一条**（这一半满足判据）；
- 但用例仍红在**位置**这一项（22:9 vs 22:81），且这是导出器诊断质量的**退步**（拒绝不再定位到入口调用）。

⇒ 现在真正剩下的只有一件事：**为什么 `located` 的包裹在这条路径上不生效**（defer 之后拒绝从 loop 体内部抛出而不是从
进入递归的调用处抛出）。读 `located` 的包裹条件、以及拒绝是在哪一步被抛出的，就能判断是"defer 之后本就该换定位点"，
还是"defer 让错误在包裹外产生"。这一条读明白后，defer（或它 + 四段）才谈得上落。
## `located` 只读（三处，file:line 原文）

### ① `located` 的机制：**抛出点外侧的 lambda，只给"没有位置"的错误补位置**
`exporter.cpp:1758`-`1762`，在 `evalCall`/调用处理里：
```cpp
    const auto located = [&](const ExportError &e) {
        // A refusal about the recursion as a whole is reported at the call.
        const uint64_t origin = act.graph->nodeOrigin(site);
        return ExportError(e.what(), locationOf(origin), origin, e.kind());
    };
```
它不是栈、不是 RAII，而是 `catch` 里重新包一层；`site` 是**调用者里那个调用节点**。
两处使用都带同一个前置（`1768`、`1779`）：
```cpp
        if (!e.location().empty()) {
            throw;          // 已经带位置的不动
        }
        throw located(e);
```

### ② 这次拒绝是从哪儿抛的：`Evaluator::evalLoopBody`
`exporter.cpp:2033` `Evaluator::evalLoopBody` 里，参数求值循环的 catch（`2082`-`2084`）：
```cpp
                try {
                    out.next.push_back(eval(act, argNodes[k]));
                } catch (const ExportError &) {
                    if (k < loop.ports.size() && k < loop.inputs.size() &&
                        growsItsExtent(g->view(), argNodes[k])) {
                        unsupported(act, ... "a recursive call changes argument {} ... whose length
                            changes from one round to the next (the body appends to it or
                            concatenates it), and a Loop carries a state whose shape is the same on
                            every round" ...);
```
调用链：`evalCall`（`1758` 的 `located`）→ `translateLoop`（`1765`）→ `evalLoopBody` → `2082` 的 catch 重新抛。
**关键**：`unsupported`（`816`-`822`）抛的是 `ExportError(text, kind)` —— **不带位置**：
```cpp
    throw ExportError(
        isSourceName(graph) ? std::format("in '{}': {}", graph, what) : std::string(what),
        kind);
```
所以这条错误回到 `evalCall` 的 catch 时 `location().empty()` 为真 ⇒ **`located` 会生效**。

### ③ 22:81 是谁给的：**没人能给 —— 那是过期的钉子**
`located` 把位置钉到 `nodeOrigin(site)`，`site` 就是闭包体里那次入口调用 `walk(x, { xs: [0], k: 0 })`。
用例现在的第 22 行是：
```
len=34 text=[        walk(x, { xs: [0], k: 0 })]
```
**第 22 行只有 34 个字符，列 81 不存在。** 而 `expect_diagnostic_line = 22` / `expect_diagnostic_column = 81`
是 `4accf10fb7` 写下的；此后这个用例被十几笔改过（`8ad073ec0a`、`eb20efab7b`、`9622bd21c9`、`42eb50c992` …），
调用从"内联在一行里"变成了独立成行（列 9）。⇒ **22:81 是旧版式下的列号，现在不可能复现；22:9 才是那次调用的真位置。**

## 判词（三选一）

**第二种**：位置本来就该是 **22:9**（入口调用 `walk(...)` 的真位置），22:81 是旧实现/旧版式算出来的过期钉子。
证据是算术：当前第 22 行 34 字符，列 81 不可能存在；且 `located` 钉的正是这个节点。
⇒ 这条用例的 `expect_diagnostic_column`（可能还有 `line`）应当**重钉**，但**先报 Lead，不自己改期望**。

**同时更正我上一条**：先前说"位置从 22:81 退到 22:9、是诊断质量的退步"——**撤回**。`located` 一直在生效，
22:9 是入口调用的真位置；"退步"是我拿旧钉子当基准读出来的，不是读数本身。defer 没有破坏定位。

## 与 defer 的关系

defer 之后：理由 = 用例 `desc` 要的那条语义事实（`in 'walk'` + leaf `'xs'` + "length changes from one round to
the next" + "a Loop carries a state whose shape is the same on every round"），位置 = 22:9（真位置）。
⇒ **defer 本身是对的，`located` 没有被它破坏；这条用例剩下的唯一红在过期的列号钉子上**。
若 Lead 决定重钉，defer（或 defer + 四段）就能让这条用例转绿——但那要 Lead 拍板，不自己改。