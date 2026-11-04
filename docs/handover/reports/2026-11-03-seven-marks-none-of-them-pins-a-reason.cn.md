# 七条钉子没有一条钉住理由，所以过期是静默的

> 2026-11-03，车道 `lane-knownfail-audit`，只读审计 `origin/develop` @ `7537a9ea5`。
> `test/plans/**` 一个字节未改；下表每一行的证据都来自该提交上的构建（`out/latest`，裸跑用例）或一行的 `std::gir` / `git grep` 读数。

## 一张表

| 钉子 | 钉的理由 | 今天成立 | 证据（一行） |
| --- | --- | --- | --- |
| `compositions.grad_vmap_f` | pullback 对"函数值调用"没有规则（`modules/autodiff/pullback.cpp#Backward::refusalReason`） | **是** | `autodiff: cannot differentiate through this call of a function value: … the function is the result of 'transforms:vmap'`，exit 1 |
| `compositions.vmap_grad_f` | vmap 对"常量"没有规则（`vmap_operator.cpp:136`） | **是**（行号已漂到 `:143`） | `transforms: vmap has no rule for \`a constant\` (a constant of a type this transform has no batch for)` |
| `compositions.jvp_vjp_f` | `stdlib/vjp.cml#vjp` 的 `transpose(lin)` 不收"结果是 pair"的函数 | **否** | 今天拒的是 `transpose takes a function that returns a \`float\``（`transpose_operator.cpp:100`，exitType 必须是 `Float64`），实参 `(f32) => f32`；该调用今天在 `:74` |
| `compositions.grad_vjp_g_zero` | 同一行，`transpose` 不收"结果是数组"的函数 | **否** | 同一条消息，实参 `(f32[4]) => f32` |
| `edges.no_ctrl_edge_kind` | builder 仍造 CTRL 边（`writeControlLink`） | **是** | `std::gir` 今天 4 条 `type="CTRL"`；`writeControlLink` 仍有 16 处 |
| `edges.gate_has_no_control_input` | GATE 仍收 CTRL 输入 | **是** | `N03 -> N04 … type="CTRL"`、`N0F -> N11 … type="CTRL"`：两个 GATE 各有一条 CTRL 进线 |
| `perf.showcase_c2` | `std::vjp` 不是已注册的 pass | **是** | `Unrecognized graph rewriting/scheduling/translation pass 'std::vjp'` |

**三行附带**

- **2 条过期**：`jvp_vjp_f` 与 `grad_vjp_g_zero` 钉的机制（pair / 数组结果没有臂）今天不是拒因；改用例仍 `fail`，于是照旧记 `[XFAIL]`，运行里一个字都不提。
- **1 条孤儿注释**：`test/plans/feat/compile/transforms.plan.toml#transpose_transposes_the_callee_and_calls_it` 还写着 "Marked known_fail … the mark is the only thing standing between the plan and a red"，而那条钉子在 2026-11-02 的 `19f0b05e8` 里已删；今天 `grad_of_an_array_parameter` 是红，且无钉。
- **数字漂移**：effects 那条 note 的"63 个读者文件 / 138 条 CTRL 边"今天是 **64 / 192**（`test/plans` 108 + `exemplars` 84）；计划头"图里没有 ORDER 也没有 SYNC 边"早已不成立（今天 2 条 ORDER + 4 条 SYNC —— 正是那两条无钉用例变绿的原因）。理由成立，数字过期。

## 为什么会静默：这个字段存在、会被比对、但一条都没写

- 读取在 `test/tools/run-tests.mjs#expandBackends`（第 2274 行）：`__knownFailMsg = typeof test.known_fail_msg === 'string' ? … : null`，不写就是 `null`，所以老钉子照旧合法。
- 比对在 `test/tools/run-tests.mjs#executePlans`（第 3317–3325 行）：标记过的用例失败后，若钉住的片段不在 `failures ∪ stdout ∪ stderr` 里，就把它从 `[XFAIL]` 改判为 `fail`，打出 `known failure changed its mind: expected "<pinned>", got "<first line>"`。**会比对，不是死字段。**
- 空转的原因是没人写：`git grep -c known_fail_msg -- test/plans` = **0**（7 条钉子全部只有 `known_fail_note`），而 `docs/test-plans.md` 只登记 `known_fail` 与 `known_fail_note`，**没有登记这个字段** ⇒ 写钉子的人不知道有它。所以"理由变了"今天只能靠人读出来。
- 顺带：`changed its mind` 那一段在 `test/tools/selftest/` 里没有 fixture 覆盖（selftest 只覆盖"标了却通过"那一侧）。