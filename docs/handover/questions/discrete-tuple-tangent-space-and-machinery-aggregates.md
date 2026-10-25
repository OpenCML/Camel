# 元组那一半的切空间，与机件自己的聚合（实测，2026-10-25）

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的行号与读数以那条线为准。


（`docs/handover/reviews/questions-2026-10-24-evening.md` §discrete-gradient-and-rank-one-slice-ownership
第 1 条说"结构体与元组有切空间，当且仅当每个字段都有"。**结构体那一半已按裁定落地**（提交
`da51570c3`、`4156333be`）。元组那一半照字面实现后，机件自己的聚合一族立刻变红，所以这一半没有落地（这一页只报读数与做法，不替作者定），
）

## 是什么

**一句话**：元组是不是也照"每个成员都有切空间才有切空间、每个成员都留在切空间里"来定？我照字面实现后，
循环状态里的元组（机件自己的聚合，不是模型）开始被拒或被反向图断言打断，所以我把元组改回"丢掉没有切空
间的成员"，只让结构体走新规则。

**最小复现**：`test/cases/modules/autodiff/loop_grad_tuple_state.cml` 的循环状态是一个元组，里面有一个
`int`（轮数）。照字面实现 = `keepsInTangent` 对元组也认离散成员（`int` 的切空间是它自己），并把
"有一项没有切空间就整条元组没有切空间"应用到元组。

## 逐字证据

照字面实现（元组也认离散成员 + 有一项缺切空间则整条没有）后，`test/plans/feat/modules/autodiff.plan.toml`：

```
[FAIL] loop_grad_tuple_state                                222.40 ms
    [INFO] expected exit 0, got 1
    [stdout] ():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Runtime graph 'loop16.tape.reverse' breaks its edge invariants (1 violation(s)):
    [stdout]   graph 'loop16.tape.reverse' node 31 (5): element 0 of a (int, float) is a int, the node is a float.
[FAIL] loop_grad_struct_state.nvm / .fvm / .jit / .prl         219.61 ms
    [stdout] (loop_grad_struct_state).../loop_grad_struct_state.cml:44:13: [Error]: autodiff: a value of type '({ a: float, b: float }, float[], float[], float[], floa...
[FAIL] loop_grad_nested.nvm / .fvm / .jit / .prl               234.35 ms
    [stdout] (loop_grad_nested).../loop_grad_nested.cml:12:12: [Error]: autodiff: a value of type '(float, (float) => (float, float)[])' depends on a differentiated parameter but has no gradient channel
```

元组不认离散成员、也不做"缺一项就整条没有"（今天的实现，只让结构体走新规则）：

```
[SUMMARY] total: 1463, pass: 1457, fail: 1, skip: 1, known failures: 4   （test/plans/feat/modules -Tier fast）
```
唯一的红是 `registry.docs_complete`，与 autodiff 无关。

## 今天的读数

| 类型 | 切空间（今天的实现） | 照字面实现 |
|---|---|---|
| `{n: int, flag: bool}`（模型） | `{n: int, flag: bool}`，全零 | 同左（已落地） |
| `{s: string, n: int}`（模型） | 无（报文点名 `s` 与 `string`） | 同左（已落地） |
| `(int, float)`（循环状态） | `(float)`：离散成员被丢掉 | `(int, float)`：反向图的边不变式被打破（逐字见上） |
| `({a: float, b: float}, float[], …, float[])`（循环状态） | 有切空间，`loop_grad_struct_state grads=24.0 24.0 24.0 24.0 1.0 24.0 2.0 6.0 …` | 整条被拒（逐字见上） |
| `(float, (float) => (float, float)[])`（捕获元组） | 有切空间（函数成员被丢掉） | 整条被拒（逐字见上） |

`std::gir` 读数（`test/cases/modules/autodiff/grad_struct_all_discrete.cml std::macro std::gir`）：
形参端口是 `value_type="Plain"`，`.n` 是 `type="ACCS" value_type="int"`，`.flag` 是
`value_type="bool"`——结构体这一半的切空间确实是"逐字段、离散字段留在自己的类型上"。

## 选项与代价

1. **元组保持机件规则（今天的实现，我倾向这条）**：元组只留真有切空间的成员，离散成员与函数成员一并丢掉；
   结构体走裁定。"有切空间当且仅当每个成员都有"只对模型（结构体）成立。
   代价：裁定第 1 条字面上的"元组"没有落地，需要一个例外句（"机件自己的聚合不算模型"）。
   好处：`test/plans/feat/modules` 全绿，9 条 `loop_grad_*` 不动。
2. **元组也照字面实现**：`keepsInTangent` 对元组认离散成员，且缺一项就整条没有。
   代价：反向图的 tape/边不变式要把离散元素带过整条反向链（`loop16.tape.reverse` 那条断言今天过不去），
   捕获元组里的函数成员也要有"没有切空间却不算丢梯度"的说法；`loop_grad_nested` 那条要改成"整流被拒"，
   而它今天是一条绿用例。工作量在 pullback 与反向 tape，不在类型层。
3. **只认离散成员、不做"缺一项就整条没有"**（两个半边拆开）：元组的 `(int, float)` 变成 `(int, float)`，
   函数成员照旧丢掉。代价：同样要先修 `loop16.tape.reverse` 的边不变式（选项 2 的第一半），
   读数上没有比选项 2 少任何工作。

## 卡住什么

`test/plans/feat/modules/autodiff.plan.toml` 今天全绿，所以**没有红的用例卡住**；卡住的是
**裁定第 1 条的元组那一半没有落地**——今天的实现与裁定不一致，而照裁定实现会让
`loop_grad_tuple_state`（1 条）、`loop_grad_struct_state`（4 条）、`loop_grad_nested`（4 条）共 9 条变红。
在裁定给出方向之前，这 9 条按今天的读数保持绿，元组按选项 1 处理。