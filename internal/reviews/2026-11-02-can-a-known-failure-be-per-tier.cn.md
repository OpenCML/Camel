# 一个用例能不能"只在某一层算 known failure"

**状态：待裁。** 这一页问的是工具，不是语言：`known_fail` 这个标记今天只能在**整个仓库**上生效，而这轮遇到的是一个**按层不同**的结果。

## 一、实测到的形状

`feat/trans.cml.print_and_recompile`：

- **Release 层（fast）：绿。** 全量 `8515 total / 8495 pass / fail 0 / skip 10 / known 10`，`RUN_EXIT=0`（`lane-a3` `132aac76d`，binary 06:13:45）。
- **Debug 层：红**，红在一句断言上：
```
Camel Debug Assertion Failed:
    Location   : ../src/core/type/base.cpp(335)
    Function   : bool camel::core::type::Type::assignableFrom(Type *) const
    Expression : to->code_ != TypeCode::Ref && from->code_ != TypeCode::Ref
    Suggestion : Ref type cannot be assigned
```
（`cml_contract.py` 重新 canonical 打印体时，子进程编译器中止）

而那句断言**是故意的**，它自己的注释写着（`src/core/type/base.cpp#Type::assignableFrom` 原文）：

> 324: `// Still an ASSERT, now with the measurements that settle why. Turning it on aborts 17 plan`
> 325: `// cases, not the four the ruling named: … `rank_zero_slot_*`, `print_and_recompile` and`
> 328: `// `ort_struct_casts` reach it too -- valid programs comparing a name with a primitive target`
> 330: `// all over the compiler, not one caller that forgot to resolve. … Stays a release-time check.`

⇒ 它在 `ASSERT` 里，Release 编译掉、Debug 留着；**它点名了这条用例**；它触发的输入是**合法程序**。

## 二、为什么不能直接标 `known_fail`

`test/tools/run-tests.mjs#printResult` 原文：

> 3236: `// A known failure that fails is reported and counted apart, and does not fail`
> 3237: `// the run. One that passes fails the run: the mark is out of date.`
> 3243-3246: `marked known_fail … but passes: remove the mark` / `… but passes, so the mark is out of date and must be removed`

⇒ 标记是**全局一个布尔**：标了，Release 那一层就会因为"标了却过了"而**整轮红**。**所以"给一个只在 Debug 红的用例标 known_fail"这件事，今天做不出来。**

## 三、四条路，各自的代价

### (a) 给标记加一层：`known_fail.debug = true`

- **改哪**：`test/tools/run-tests.mjs` 的 `TEST_FIELDS` 与 `__knownFail` 的判定；`known_fail` 从 bool 变成可带层的对象/`known_fail_tier`；`scripts/plan-inventory.mjs` 与 `docs/test-plans.md` 的字段表；selftest 里那几个 `known_but_passes.*` 夹具要各补一条"只在另一层通过"的用例。
- **代价**：新增机制（今晚的判据里，新增机制要单独立账）；而且**它是第一个"同一用例在不同层有不同真相"的字段**，之后"层"会渗进每个标记类字段（`skip_unless` 已经在做类似的事，但那是**前置条件**，不是**结论**）。
- **好处**：Debug 层从此可以 `fail 0`，而且红被**写进账**而不是被无视。

### (b) 让那条断言只在"这是合法程序"的路径上闭嘴

- **改哪**：`Type::assignableFrom`（`src/core/type/base.cpp#Type::assignableFrom`）——要么在比较前把 `Ref` 解析掉，要么让调用方先 resolve 再比。
- **代价**：注释自己说这是"which type is a name allowed to stand for, and who reads it"的问题，即**类型系统**的问题；而且那段注释记着"打开它会中止 17 条计划用例"（`rank_zero_slot_*`、`comprehension_context.annotated`、`context.var_sibling`、`ort_struct_casts`、本条）。**改它等于改语言的一处判定**，不是改一个测试。
- **好处**：根上解决，Debug 与 Release 给出**同一个结论**。

### (c) 维持现状：`failure_note` + Debug 层带着这条红

- **改哪**：不用改代码；这笔账写在 `test/plans/feat/trans/cml.plan.toml` 该条的 `failure_note` 里（已写），以及提交信息里。
- **代价**：**Debug 层永远有 1 条红**，任何"Debug 全绿"的判据都过不了；红是"有账的红"，不是"没看见的红"。
- **好处**：**不改任何判据**。今晚的判据是"不许为了让 Debug 绿而放宽检查"，(a) 新增机制、(b) 改类型系统、(d) 拿掉覆盖，都不如它干净。

### (d) 把那条用例从 Debug 层移出

- **改哪**：`cml.plan.toml` 该条的 `tier`/`debug_tier`。
- **代价**：**拿掉覆盖**——Debug 层从此不再跑这条打印再编译的用例，而 Debug 恰恰是它唯一能暴露问题的那一层。
- **好处**：Debug 层立刻 `fail 0`，但那是**少测**换来的。

## 四、我方默认

**(c) 维持现状 + 把这笔账写清。**

理由一条：**这四条里只有 (c) 不改任何判据**。(a) 是新增机制，(b) 是改类型系统，(d) 是拿掉覆盖；在"不许为了让 Debug 绿而放宽检查"这条判据下，只有 (c) 是"什么也没放宽，只是把看不见的红记下来"。

**若作者裁 (a)**：我方建议的最小形状是 `known_fail = { debug = true }`（而不是再来一个并列字段），并且要求同一提交里 `docs/test-plans.md` 的字段表与 selftest 同时更新，否则"层"会变成第二个 `known_but_passes` 那样的隐性规则。

**若作者裁 (b)**：那是类型系统的活，与这条用例无关；届时 `failure_note` 与这一页一起撤。