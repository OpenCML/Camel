# `known_fail` 对不写 `backends` 的条目一个字也没读

**作者的 01:50 抽查点名的就是这个**：`20860eb01` 发现"没写 `backends` 的条目，`known_fail` 标记完全不起作用"，而那一笔的修法是给那一条用例补上 `fvm`。这一页记的是机制：为什么它不起作用、改成了什么、全树扫了几条、以及反向验证的读数。

**先说这一页最要紧的一句**：**一个缺陷可以潜伏很久，不是因为它隐蔽，而是因为没人用过那条路。** 全树 6 条标记全部落在"有 `backends`"的形状里（④），那条读不到标记的路**从来没有一条真实条目走过**——所以它不报错、不告警、也不被任何读数碰到。作者能追出来，是因为他问的是"**这条标了没生效吗**"，而不是"有没有 bug"：前一个问题指向一条具体的、可疑的读数，后一个只能靠随机撞见。

## ① 原文（`test/tools/run-tests.mjs`，改前）

```js
function expandBackends(test) {
    if (!Array.isArray(test.backends)) return [test]      // ← 没有 backends：原样返回，后面一行都不执行
    const knownFail = new Set(test.known_fail || [])      // ← 标记在这里才被读
    for (const backend of knownFail) { ... }
    return test.backends.map((backend, index) => ({
        ...test,
        name: `${test.name}.${backend}`,
        __backend: backend,
        __knownFail: knownFail.has(backend),              // ← 只有这一支会写这个字段
    }))
}
```

读标记的只有 `__knownFail`，两处：

```js
if (entry.test.__knownFail) {
    if (result.status === 'fail') { result.status = 'xfail' }        // 失败按已知失败计数
    else { result.status = 'fail'; result.failures.push('marked known_fail ...') }   // 标了却通过 ⇒ 红
}
```

**⇒ 一句话**：`__knownFail` 只在 `test.backends.map(...)` 那一支里被写；不写 `backends` 的条目在函数第一行就 `return` 了，**标记从来没被读到**——失败照旧算 `fail`，通过也照旧算 `pass`，"标了却通过"的提醒同样不会出现。

## ② 改前/改后的读数（同一支探针，逐字）

探针：`test/cases/smoke/core/fib10_pipeline.cml`，断言改成 `expect_stdout_contains = ["Fibonacci(11) = 89"]` 使它必败；`backends` 不写，标 `known_fail = true`。

| | 标了且失败 | 标了却通过 |
| --- | --- | --- |
| **改前**（`git show HEAD:test/tools/run-tests.mjs`） | `[FAIL] probe.before` · `[SUMMARY] total: 1, pass: 0, fail: 1` | `[SUMMARY] total: 1, pass: 1, fail: 0`（一个字都没说）|
| **改后** | `[SUMMARY] total: 1, pass: 0, fail: 0, known failures: 1` | `[SUMMARY] total: 1, pass: 0, fail: 1` + `[INFO] marked known_fail but passes, so the mark is out of date and must be removed` |

对照组（不标、同样必败）：`[SUMMARY] total: 1, pass: 0, fail: 1` —— 证明那条断言真的是败的，不是探针自己坏了。

## ③ 改法：先读标记，再分形状

- **有 `backends`**：标记是后端名的列表（写法不变），**名字不在列表里仍然按名字拒**：`known_fail names 'fvm', which is not in backends of X`；写 `true` 也拒（"name the back ends that fail"）。
- **没有 `backends`**：这一条只有一次运行，标记写 `known_fail = true`。写了列表就按名字拒：
  `known_fail on probe.4 names back ends (["fvm"]), but the test has no backends list, so no back end of that name runs here: write known_fail = true`
  ——**错形状不再悄悄失效，而是启动就报错**。

## ④ 全树扫"标了却没起作用"（用 runner 自己的 loader，逐条）

`test/plans` 下写 `known_fail` 的条目共 **6** 条，逐条并列（`scripts/plan-inventory.mjs` 之外的第二个读者，`loadPlanTests` 读完直接看 `__knownFail`）：

| 文件 | 条目 | 形状 | 标记生效 |
| --- | --- | --- | --- |
| `test/plans/feat/compile/transforms.plan.toml` | `grad_array_no_int.fvm` | `backends=["fvm"]` | `grad_array_no_int.fvm.fvm` |
| 同上 | `grad_of_an_array_parameter.nvm` | `backends=["nvm"]` | `grad_of_an_array_parameter.nvm.nvm` |
| 同上 | `grad_array_sum.nvm` | `backends=["nvm"]` | `grad_array_sum.nvm.nvm` |
| 同上 | `grad_array_product.nvm` | `backends=["nvm"]` | `grad_array_product.nvm.nvm` |
| `test/plans/feat/linear/effects_model_edges.plan.toml` | `edges.no_ctrl_edge_kind` | `backends=["gir"]` | `edges.no_ctrl_edge_kind.gir` |
| 同上 | `edges.gate_has_no_control_input` | `backends=["gir"]` | `edges.gate_has_no_control_input.gir` |

**6 条全部有 `backends`，6 条全部生效**：今天没有一条落在缺陷里。**这才说得通它为什么一直没被发现**——它不是"某些条目失灵"，是"这种写法谁都还没用过"，`20860eb01` 是靠"标记写了却还在红"才看出来的。

（第一行那个名字 `grad_array_no_int.fvm.fvm` 是 `20860eb01` 补 `backends = ["fvm"]` 时留下的：条目名本来就以 `.fvm` 结尾，展开后又加了一个。不影响判定，只影响读起来像写了两遍。）

## ⑤ 反向验证（作者点名要的）

`known_fail = true` + 一条**会通过**的用例 ⇒ 必须红，并报"标了却通过"：

```
[SUMMARY] total: 1, pass: 0, fail: 1
    [INFO] marked known_fail but passes, so the mark is out of date and must be removed
```

## ⑥ 与 `20860eb01` 的关系

**`20860eb01` 不回退**：给 `grad_array_no_int` 补 `backends = ["fvm"]` 让标记生效，**是那一条用例的正确处置**（它本来就在 `fvm` 上跑）。这一页补的是机制——**任何写法都能标、写错形状会被拒、标了却通过会红**。两件事不冲突：那笔修的是那一条，这笔修的是"下一条不用再靠猜"。

## ⑦ 读数

- runner 自检：`node test/tools/selftest/run.mjs` → `runner self-test ok`（它按名字钉着 `marked known_fail on 'prl' but passes: remove the mark`，所以有后端的措辞一个字没动，只有"没有后端"那条换了不带冒号的句子——带冒号会被 `printDetailLines` 当成"标签: 值"拆成两列打印）。
- 全量 `-Plan all -Tier fast`：`[SUMMARY] total: 8442, pass: 8426, fail: 0, skip: 10, known failures: 6`、`RUN_EXIT=0`（`ea5d5de70`，原 `lane-a3`，该分支已删；含 `develop` 20860eb01 那 30 笔）。**`known failures: 6` 正好是 ④ 那 6 条**；这次从 3 涨到 6 不是这一笔造成的，而是同一遍里新编进来的 `transforms` 模块让 33 条原先 skip 的用例真的跑起来（`skip 43 → 10`），它们原来没跑到，标记也就没有机会生效。
- 探针文件跑完即删，工作树里没有留下 `_kf_*.plan.toml`。