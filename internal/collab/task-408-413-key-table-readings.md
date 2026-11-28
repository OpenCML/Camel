# task-408 / task-413 — 键表读数：slice 要 5/6 两条键，`idx_take` 没有键也没有律

**读数来源**：`lane-block-hash`，2026-11-28，树 `D:\Projects\Camel-wt\rule-branch`，枝 `lane/walk-abort-377`，`tip 12902ef014`（`gir: a parameter without a name is spelled by its position`）。源码零改动：413 的探针（`rule_index.cpp` 的 `IDXPROBE` 三行 + `test/plans/probe/idxprobe.plan.toml`）已 `git checkout --` / 删除，本卡是它唯一的留存；408 是纯读源。

## 一、413 结论改判：不是"键对了却取不到"，是这一族里没有律、这个算子也没有键

### 1. `&indexed` 探针：入槽与取槽是同一份实例 ⇒ "两份 `static std::map`"排除

在 `src/kernel/core/rule_index.cpp` 的 `indexRule`（插入侧）与 `lookupRules`/`lookupRule`（取槽侧）各打一行 `&indexed` + `family` + `key`，用
`test/cases/compile/rulesets/vmap_ruleset.cml` + `std::rulesets query=…` 跑一次（`BUILD_EXIT=0`）：

```
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=min@1 slotsBefore=1
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=reshape@1 slotsBefore=0
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=unsqueeze@1 slotsBefore=0
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=transpose@1 slotsBefore=0
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=permute@1 slotsBefore=0
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=concat@2 slotsBefore=0
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=dim@1 slotsBefore=0
IDXPROBE insert indexed=0x755605dfbba0 family=rulesets.vmap key=where@3 slotsBefore=0
IDXPROBE lookup indexed=0x755605dfbba0 family=rulesets.vmap key=index_take@2  absent=1
IDXPROBE lookup indexed=0x755605dfbba0 family=rulesets.vmap key=index_into@2 absent=0
IDXPROBE lookup indexed=0x755605dfbba0 family=rulesets.vmap key=+@2           absent=0
IDXPROBE lookup indexed=0x755605dfbba0 family=rulesets.vmap key=-@2           absent=0
```

两条结论，都是从地址与插入序列里直接读出来的：

- **入槽与取槽是同一个 `indexed`（`0x755605dfbba0`）** ⇒ 同一进程里没有两份 `static` 表，"per-binary/per-module 各一份"这条嫌疑排除。
- **`index_take@2 absent=1` 不是因为键拼错**：插入那一串里**从头到尾没有 `index_take`**，这一族在这条基上就没有这条律。

### 2. 这一族确实没有 take 律

`src/stdlib/rulesets/vmap.cml:234-247` 只有右式是一次取元素的 `idx_batch` 与它的律：

```cml
func index_into::<T>(x: T[], i: int) => T { return x[i] }
export rule vmap_rule_idx::<T>(x: T[], i: int) { index_into(x, i) => func (bx: Lift<T[]>, bi: int[]) { idx_batch::<T>(bx, bi) } }
```

即左式 `index_into(x, i)`、head `index_into`、arity 2 —— 这正是 `kRuleKeyExceptions` 里 `:op/idx_arr → index_into/2` 那一条的对应物。而 **`index_take` 这个名字在这条基上根本不存在**：`git grep -ln 'index_take' -- src` 零文件（`vmap.cml` 里也没有），所以 `lane/enter-shape` 那条 `RULESET rulesets.vmap index_take/2 -> vmap_rule_idx_take` 是**它那条枝上新增的键与律**，不是这条基的读数。

### 3. `:op/idx_take` 连键都没有

- `kRuleKeyExceptions`（`src/kernel/core/forward.cpp:47`，`std::array<RuleKeyException, 9>`）**9 条里没有 `idx_take`**：`idx_arr`×2、`argmax_axis`、`argmin_axis`、`max_axis`、`mean_axis`、`min_axis`、`multiply`、`sum_axis`。
- `:op/idx_take` 是**下标形算子**：`src/stdlib/prelude.cml:299` `#op(':op/idx_take')` + `func operator []::<T>(arr: T[], indices: int[], axis: int) => T[]`。
- `ruleKeyOf` 对它的判定链（`forward.cpp:174-218`）：例外表无条目 → `operationOf("idx_take")` = `"idx"`（第一个 `_` 之前），不在 6 条 `kWrittenOperations`（add/sub/mul/div/neg/pow）里 → `conversionKeyOf` 为 `nullopt` → **`return std::nullopt`（"No key, no guess."）**。

⇒ 对**算子节点**按 URI 问的那条路（`vmapRulesOf(uri)`）连键都拿不到；只有**调用**才走 `vmapRuleByHead(name, operands)`（`src/stdlib/transforms/vmap.cpp:666`：`calleeOf` → `leafNameOf` → `camel::core::vmapRuleByHead`），而 `index_take` 在 vmap 里没有律可问。

### 4. ⇒ 413 按原描述在这条基上不复现

原描述的读数是 `key=index_take/2` 找到了、但 0 槽。要走到那一步，前提是**那条枝上先有了 `:op/idx_take` 的例外条目与 `vmap_rule_idx_take` 这条律**（`lane/enter-shape` 的枝上有，这条基上没有）。所以 413 的正确表述是：

> **前置 = 与 408 同族的"键与律"**，不另立一段；做完 408 与 `idx_take` 的键与律之后再跑一次，若仍取不到，才是"律已有、键已对、仍取不到"那一格——按读数，那一格很可能不存在。

## 二、408 读数：`slice` 是 5/6 两条键，表 9 → 11

### 1. 声明与节点的字数

```cml
#op(':op/slice_sub')                       // src/stdlib/prelude.cml:1268
with <axis: int, start: int, end: int, step: int = 1>
export func slice::<T>(x: T[]) => T[] panic
```

节点的字数由一条权威注释写死（`src/compile/src/gir/builder_call.cpp:2287` 起）：

> The step is the node's **fifth ordinary argument** (the value, the axis, the two bounds, the step; **the sixth is the mask** that says which of the two bounds were written, and a call that stops at five words wrote no mask, so it is read as "both bounds", 3).

判据本身也在同一处：`normInputs.size() >= 5 && normInputs.size() <= 6 && withInputs.empty()`。ONNX 侧同口径：`src/stdlib/onnx/lowering.cpp:479` "a slice node has at least 4 words (value, axis, start, end)"，`:1456` 把它注册在 opset 10。

⇒ **两个键**：

| URI | head | arity | 何时是这个形状 |
|---|---|---|---|
| `:op/slice_sub` | `slice` | **5** | 两个界都写了、无掩码（`normInputs.size() == 5`） |
| `:op/slice_sub` | `slice` | **6** | 带"哪两个界写了"的掩码（`normInputs.size() == 6`） |

### 2. 为什么两条都要、且调用方必须带 counts

`ruleKeyOf` 只在**问的人给了 `operandCount`** 时才按 arity 挑条目（`forward.cpp:174-190`）：

```cpp
for (const RuleKeyException &exception : kRuleKeyExceptions) {
    if (exception.uri == uri) {
        if (operandCount.has_value() && static_cast<size_t>(exception.arity) != *operandCount) { continue; }
        return RuleKey{exception.head, exception.arity};
    }
}
```

问的人不给 count 时**取第一条**——所以只加 6 那条等于白加；两条都要有，且调用方要走带 count 的重载（`forwardRulesOf(uri, operandCount)` / `vmapRulesOf(uri, operandCount)` 都有这条路）。

### 3. 表要从 9 条改成 11 条

`kRuleKeyExceptions` 是 `std::array<RuleKeyException, 9>`（`forward.cpp:47`），加两条 ⇒ **`11`**；`ruleKeyExceptions()`（`:160`）的打印与 `std::rulesets` 的清单会跟着变，两处要一起看。

### 4. 408 之外还要一起改的常量

`:op/idx_take` 的键并入同一张表的同一次改动：**head 名与那条律在这一条基上都不存在**（`index_take` 零命中），要一次造出来，且条目里的 head/arity 必须与律的左式**逐字对上**——律写 `index_take(xs, indices, axis)` 就是 3 个字、写 `index_take(xs, indices)` 就是 2 个。`:op/idx_take` 的算子声明是 `(arr, indices, axis)` 三个字，但 vmap 族是按**问的人给的字数**取的（`vmapRuleByHead(name, operands)` 的 `operands` = `normInputsOf(ref).size()`），所以先定律的左式、再定条目，两边用同一个数。

## 三、交回口径（建枝等闸开）

顺序：**408 先行，`:op/idx_take` 并进同一张表**，413 只剩最后那一格（很可能不存在）。

408 的判据（卡面照收）：

1. jvp 探针拿到切向，贴 `READING` 与数值；
2. 两条新条目 + 库侧 `slice` 的 `#op` 调用形 + 律，键表与库律**分开提交**；
3. "其余 9 条一字未动"的 diff；
4. `feat/transforms` 甲 0；
5. `vjp-parity newDebt 0 / staleCount 0`；
6. AST 单独一笔。

413 的判据（照收）：`lookupRules("rulesets.vmap","index_take",2)` 返回 1 槽且 `vmap` 探针 `READING_LAW_APPLIED`；`feat/trans` + `feat/modules` 新增红 0。

## 四、留在这条枝上的东西

- 本卡（交接文档）。
- `test/cases/modules/onnx/ort_collect_loop_grad.cml` 与 `…/ort_collect_loop_grad.py`：**383 ⑤（`append`→ONNX 序列）的正向用例**，untracked、未提交。`nvm` 侧读数成功（`[5.25]`、`[3.0, 3.0, 3.0, 3.0]`），导出侧被
  `ExportUnsupported: this exporter does not yet lower a collecting loop's reverse into ONNX sequences`（`exporter.cpp:4233-4262`）拒 —— 做 383 ⑤ 时直接用这两个文件。
- 探针全清：`src/`、`test/` 的 `git status --porcelain` 只有上面两个 untracked 文件。