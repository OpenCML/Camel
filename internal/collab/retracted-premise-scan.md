# 已撤回前提的复述扫描：`a Loop carries a state whose shape is the same on every round`

只读取证（未 build、未 test、未改文件、未提交、未推）。树 `D:\Projects\Camel-wt\witness`，
HEAD `c12c06f527`（`wip: the replayed round count, as a starting point for task-419`）。
扫描范围：`test/**`、`docs/**`、`src/**`。撤回决定：`D-1128-39`；本页只做取证，不判对错。

## 0. 用的正则与命中数

```
-i "same on every round"                      test docs src  ⇒  6   条（见 §1 §2）
-iE "shape (is|are) the same"                 test docs src  ⇒  9   条（与上一条大量重叠，见 §2）
-iE "carries a state|shape are the same|not the same on every" test docs ⇒ 2 条（1 条真命中 + 1 条无关工具文案）
-i "same shape"                               test docs src  ⇒ 233 条（过宽，绝大多数与循环状态无关，不逐一列）
-i "every round"                              test docs src  ⇒ 157 条（同上，过宽）
-i "whose length changes from one round"      src test       ⇒  1 条（只在 test，见 §1）
```

`same shape`／`every round` 的 233/157 条里绝大多数是别的意思（数组形状、`extent_of`、triage 工具文案等），
本页只收**断言这条前提**的。

## 1. A 类：用例断言（test/**）—— **总数 1**

```
test/plans/feat/modules/onnx.plan.toml:2320
  assert(text.includes('whose length changes from one round to the next (the body appends to it or concatenates it),
  and a Loop carries a state whose shape is the same on every round'), 'the diagnostic must state the fact: ' + text)
```

- 形式：`assert(... text.includes(...))`（不是 `expect_diagnostic*`，也不是 golden 比对串）。
- 它断言的**是整句拼接文本**，其中后半句正是被撤回的前提原文。
- 该断言期望的那段文本在 `src/**` 里**搜不到**（`-i "whose length changes from one round"` 在 `src` 命中 0），见 §4 的"孤立"一条。

## 2. B 类：诊断文本（src/**）—— **总数 5（其中 2 条是注释、3 条进消息）**

`src/stdlib/onnx/exporter.cpp`：

```
:1363  /// the same on every round. The fact is checked where the body's next state        ← 文档注释（与 C 重叠）
:1419  /// the *fact* the rule asks for is that its shape is the same on every round, so both must state  ← 文档注释
:1433          why = "its shape is not the same on every round of the recursion";          ← 进入 :3211 的消息
:1873              "shape are the same on every round",                                    ← 进入 :1867 unsupported(...) 的消息
:3210              "the same on every round",                                              ← 进入 :3205 unsupported(...) 的消息
```

进消息的三处，组装后的完整句子（同文件）：

```
:1867-1876  unsupported(act, std::format(
              "a recursive call changes argument {} of a recursion whose depth "
              "depends on the model input (counting from 1, `with` parameters "
              "first): it is {}, and {}; a Loop carries a state whose dtype and "
              "shape are the same on every round", ...))
:3205-3211  unsupported(act, std::format(
              "a value carried by a loop changes from one call of the recursion "
              "to the next: {}; a loop carries a state whose dtype and shape are "
              "the same on every round", why))
            其中 why 来自 :1433（由 :3204 的 shapeIsKept(state[k], next[k], why) 填入）
```

**被 `exporter.cpp` 那条拒绝复用的情况**：`:1433` 的 `why` 被 `:3205-3211` 复用（同一条拒绝的第二半句）；
`:1873` 与 `:3210` 各自只在**自己那条** `unsupported(...)` 里出现一次，没有互相复用。

## 3. C 类：文档与注释（docs/** 与源码注释）—— **总数 3**

```
src/stdlib/onnx/exporter.cpp:1363   /// the same on every round. The fact is checked where the body's next state
src/stdlib/onnx/exporter.cpp:1419   /// the *fact* the rule asks for is that its shape is the same on every round, so both must state
src/stdlib/onnx/exporter.cpp:3200-3202
        // The fact the rule asks for: the shape of the value
        // the body hands the recursive call is the shape of the state it came from.
        // A state input of a Loop has one shape for all its rounds.
```

`docs/**` 里**没有**该前提的复述：`-i "same on every round"` 与 `-i "same dtype and shape"` 在 `docs` 命中均为 0。
`src/kernel/diagnostics/registry.tsv` 里也没有这句（命中 0）——那句"the same on every round"只在 `exporter.cpp` 内拼出来。

## 4. 两条读数

**① 每类总数**

```
A 用例断言 : 1   （onnx.plan.toml:2320）
B 诊断文本 : 5   （exporter.cpp:1363, 1419, 1433, 1873, 3210；其中 1363/1419 是注释）
C 文档/注释: 3   （exporter.cpp:1363, 1419, 3200-3202；1363/1419 与 B 重叠计入两类的交集）
docs/ 与 registry.tsv : 0
```

**② A 与 B 的对应关系**

```
孤立（对不上）: test/plans/feat/modules/onnx.plan.toml:2320
  它期望的串是 "whose length changes from one round to the next (the body appends to it or concatenates it),
  and a Loop carries a state whose shape is the same on every round"
  而 src 里两条真实消息的措辞是
  ":1867-1876 … a Loop carries a state whose dtype and shape are the same on every round"
  ":3205-3211 … a loop carries a state whose dtype and shape are the same on every round"
  ⇒ 三条特征都对不上：① test 的 "whose length changes from one round to the next (the body appends to it or
    concatenates it)" 这段在 src 中不存在；② test 写 "shape is the same"，src 写 "dtype and shape are the same"；
    ③ test 写 "a Loop"（大写 L），:3205-3211 写 "a loop"。
  ⇒ **这条用例断言在当前 src 上没有任何产者**：要么它断言的是已被删/改写的旧消息，要么它断言的是别处
    （`onnx` 模块之外的）拒绝文案。取证到此，不判对错。

对得上的: 无
```

## 5. 边界

- 只读：未 build、未 test、未改代码、未提交、未推远端。
- 本页只列位置与原文，不判这些位置是否"该删"；修法由协调者/决策者定。
- `same shape`（233）与 `every round`（157）两个过宽正则有命中的文件未逐一列；若需要全量清单，可另开一格做。