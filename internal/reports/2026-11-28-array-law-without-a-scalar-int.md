# 疑点查实：没有标量 `int`，就没有 `T=int` 的数组律

**结论**：**是缺陷**（不是"设计如此"，也不是"机制正在被替换"）。律本身没有缺条目；缺的是 `T=int` 的**实例**——引擎枚举类型元组的来源只有"程序里出现过的整值类型"，`int[3]` 的元素类型不会自己出现在那张表里。规范文本（设计原则 9.11 与重写设计稿 2.5）要求的是"匹配处按被匹配节点的类型实例化"，实现做的是"按程序的整值类型预先枚举"，两者只在类型参数等于整个值类型时重合。

本文所有代码引用以集成尖 `5dd8d9a3f4` 为准（`src/stdlib/rewrite/module.cpp` 在该尖与我读的版本一致；`01a53b959d..5dd8d9a3f4` 未改动 `module.cpp`、`rules.cml`、设计原则与重写设计稿）。

---

## 1 问题①：`T=int` 的数组律有没有对应条目

有。律写在库里，一条规则、按元素类型参数化：

```camel
// src/stdlib/rules.cml:99
export rule array_add_self::<T>(x: T[...]) when is_real::<T>() { x + x => x * 2 }
```

库自己的注释把这件事说成契约（`rules.cml:25-33`）：

```
 * One law is written once. The head declares the type parameter and the condition names the
 * family (`rule mul_one::<T>(x: T) when is_real::<T>()`), so there is one instance per element
 * type and the engine fires the instance whose type the matched node has. ...
 * The array form of a law is a rule of its own (`array_mul_one`): `x: T[...]` binds `T` to the
 * element type, so one rule is the law for every shape, ...
```

所以"缺"不在律里，在**实例从哪来**。`module.cpp#buildEngineRules` 为一个泛型规则做的唯一动作是"对程序类型元组逐个建实例"（`module.cpp:805-806` 的注释、`:824-826` 的调用）：

```cpp
// A generic rule has no sides of its own: it contributes one instance per type tuple of the
// program it is applied to, made once and kept.
...
const size_t arity = entry != nullptr ? entry->typeParams.size() : 1;
for (const std::vector<Type *> &types : tuplesOf(ctx, *base, ruleId, arity)) {
    const size_t index = camel::compile::gir::genericRuleInstance(ctx, ruleId, types, rule->withValues());
```

元组的来源是 `module.cpp#tuplesOf`（`:563`），而它只读 `module.cpp#programValueTypes`（`:564`）：

```cpp
// module.cpp:516-544  programValueTypes
for (const auto *graph : program.graphs()) {
    ...
    // The type of the value, not the cell a `var` parameter is held in
    Type *type = camel::core::type::valueTypeOf(graph->node(it.ref())->dataType());
    // A value of `never` never exists (design principles 3.9)
    if (type == nullptr || type->code() == TypeCode::Void || type->code() == TypeCode::Never) {
        continue;
    }
    const bool known = std::any_of(types.begin(), types.end(), [&](const Type *seen) {
        return seen == type || seen->equals(type);
    });
    if (!known) { types.push_back(type); }
}
```

`valueTypeOf` 只剥 `var` 单元（`src/kernel/include/camel/core/type/composite/tuple.h:256-266`：`isCell()` 时取 `typeAt(0)`，其余原样返回），所以 `int[3]` 这一格进表的是 **`int[3]`**，不是 `int`。`T` 是**元素**类型（`x: T[...]`），因此对 `T` 只有一格候选 `int[3]`：为它建的实例模式是 `int[3][...]`，该实例建不出来（条件/算子都不成立），`buildEngineRules` 安静跳过（`module.cpp:835-840`）。`int` 这一格永远没有。

## 2 问题②：缺了会怎样——一条走不到的实际证据

### 2.1 代码路径：`T` 取不到元素类型

把上面三处串起来，一个只有 `int[3]` 值、没有 `int` 标量值的程序：

| 步骤 | 位置 | 结果 |
|---|---|---|
| 收集整值类型 | `module.cpp#programValueTypes`（`:516`，靠 `valueTypeOf`；`:528`） | `{int[3], …}`，没有 `int` |
| 枚举 `T` 的候选 | `module.cpp#tuplesOf`（`:563-598`） | 只有 `(int[3])`；`ruleConditionAt` 判 `is_real::<int[3]>()` 为假 ⇒ 无元组 |
| 建实例 | `module.cpp#buildEngineRules`（`:824-846`） | `T=int` 的实例一次都不会被建 |
| 匹配 | 引擎的规则表 | 没有任何实例的模式是 `int[...]` ⇒ `int[3]` 节点永远匹配不上 |

`T[...]` 在匹配处本应把 `T` 解成元素类型（重写设计稿 2.5），但实例是**预先造好**的，造的时候没有"匹配处"可问，只有程序的值类型表可问。

### 2.2 实测 A/B：同一用例、同一命令，只差"末尾那个标量 `int` 值在不在"

用例 `test/cases/modules/rules/tensor_target.cml`，命令 `tensor_counts::report → rules::simplify → tensor_counts::report → std::nvm`（同一个探针计划文件，逐字相同）。两次都以 detached HEAD 跑两个提交，`[camel-remote] measuring: the commit alone (no tracked change)`：

| | A：`camel-sha-07877b09e063` | B：`camel-sha-fbea22146430` |
|---|---|---|
| 入口 `main` | 末尾 `0` 作为入口的值交出 | lowering 不再交出末尾 `0`（别处已改的行为） |
| 程序里有没有 `int` 值 | 有（那个 `0`） | 没有（`int[3]` 的张量与常量都不算） |
| run 名 / `[provenance]` | `20261128_184759_30188_azfk` / `[provenance] tip (dirty: 1 files) \| binary built 2026-11-28T18:48:15+08:00 \| tree /home/zjwei/camel-work/main-exit \| tier fast \| jobs 1` | `20261128_184825_12056_3uee` / `[provenance] tip (dirty: 1 files) \| binary built 2026-11-28T18:48:40+08:00 \| … \| jobs 1` |
| 第二段读数（`before` 段后、`rules::simplify` 之后） | `… \| i64_self 0 1 \| i64_distribute 1 1 \|` | `… \| i64_self 1 0 \| i64_distribute 1 1 \|` |
| 含义 | `i64_self` 里 add 1→0、multiply 0→1：`array_add_self` **生效** | add 1→1、multiply 0→0：律**没生效** |

（`dirty: 1 files` 指那次跑里未跟踪的探针计划文件；两次的 tracked 改动都是 0，读数是对着两个提交本身取的。两段的完整读数在 `D:\Temp\tensor_at_07877b09e0.log` 与 `D:\Temp\tensor_at_fbea221464.log`。）

唯一被改动的事实是"那个标量 `int` 值还在不在"：张量、常量、`i64_self(t) => t + t` 的形状两次完全一样。律的开关跟着这个值走，而不是跟着 `int[3]` 的张量走。

### 2.3 另一条（顺手取的）确认：集成尖上这个用例是绿的

集成尖 `5dd8d9a3f4` 上跑 `test/plans/feat/modules/rules.plan.toml`：

```
[PASS] rules.tensor_laws_counts                             214.79 ms
[provenance] tip 5dd8d9a3f4c0b5992712b9ecca96063f11f128be | binary built 2026-11-28T20:36:21+08:00 | tree /home/zjwei/camel-work/main-exit | tier fast | jobs 2
[SUMMARY] total: 174, pass: 174, fail: 0
```

（日志 `D:\Temp\task470_rules_tip.log`；其中那行 `[PASS]` 就是 `rules.tensor_laws_counts`，它钉的正是 `i64_self 0 1`。）

**为什么现有用例照不出来**：浮点律总带标量系数（`x * 3.0` 的系数按元素类型取，于是程序里有 `f32` 标量值，`T=f32` 有实例）；`i64` 这一格此前一直靠入口末尾那个 `0` 撑着。库里没有一条用例是"只有 `int[3]` 张量、没有任何 `int` 标量值"的形状，所以这格缺实例从未被读到。

## 3 问题③：该走哪条路——缺陷

**规范依据**（设计原则优先于代码与测试）：

- `docs/design-principles.cn.md:739`（9.11）：「**泛型规则在匹配处实例化**：被匹配的节点的类型解出类型参数，以类型元组为键缓存；某个类型元组做不出实例（条件为假，或模式在该类型上没有算子）是安静的，`why_not` 报告原因。」——解出类型参数的是**被匹配的节点**，不是"程序里值类型的集合"。
- `docs/design/rewrite/README.cn.md:75-82`（2.5 泛型规则）：「引擎按匹配到的值的类型实例化规则。」
- `docs/design/rewrite/v2.cn.md:160`：「泛型规则是一个值，引擎在匹配处按类型实例化。」
- 库自己的契约：`src/stdlib/rules.cml:26-33`「one instance per element type and the engine fires the instance whose type the matched node has」；数组形式「`x: T[...]` binds `T` to the element type」。

**机制不被替换**（所以不是"等替换、别修"）：

- `docs/design/graph-native-optimizer/README.cn.md:30` 与第 3 节：内核保留"引擎"——规则匹配（模式集预编译成匹配自动机）、右侧模板、事实的不动点求解、等价饱和与按代价提取、组合子调度。"规则引擎若用 Camel 写，每条规则都会被它解释执行，第一层就退化成第三层"——引擎留在 C++ 是**目标**，不是待拆的旧物。
- `docs/design/architecture/refactor-plan.cn.md:62`：重写（`kernel/rewrite`、`lib/rewrite`、`lib/std/cxx/opt`）在目标布局里是一条车道的独占目录——这是**搬家**，不是替换。

**最小修法（一句话，不落代码）**：在 `tuplesOf` 的候选类型来源上补"按模式位置解出的类型"——`programValueTypes` 给出的是整值类型，还应把每个数组/张量值类型的**元素类型**作为候选（对 `T[...]` 这种位置，匹配处会把 `T` 解成元素类型；枚举要覆盖匹配能解出的那几格）。

**判据（改完读什么）**：`test/cases/modules/rules/tensor_target.cml` 在"入口不交出末尾那个值、程序里只有 `int[3]`、没有任何 `int` 标量值"时，`rules::simplify` 之后的读数仍是 `i64_self 0 1`（add 1→0、multiply 0→1）。这条读数今天在改前的树上读出来是 `i64_self 1 0`（见 2.2 B 列）。

**边界**：只读查实，没改任何文件、没开枝、没 push；需要运行的部分在集成尖上用 `-Jobs 2` 跑，读数带 `[provenance]`。

## 4 读数纪律：跑前跑后的树状态

```
跑前 HEAD a87b5ba7d1bbcfe3661f450f87001c0067a64b10
跑前 status 行数 0
detach 到集成尖时 HEAD 5dd8d9a3f4c0b5992712b9ecca96063f11f128be
detach 到集成尖时 status 行数 0
跑后 HEAD a87b5ba7d1bbcfe3661f450f87001c0067a64b10
跑后 status 行数 0
```

（还原用 `git checkout lane/gate-exit-no-value`，没有 `reset --hard`、没有删锁。）

## 5 顺带看到、与本疑点无关的一条

集成尖 `5dd8d9a3f4` 上那次跑的 `[SUMMARY]` 是 174/174 全绿，但整条命令仍以 exit 1 结束：`check-doc-links` 报了 3 条引用对不上（`docs/design/effects/plan.cn.md:60,248` 指向 `kernel/execute/pass.cpp#std::prl`，`test/plans/feat/linear/jit_without_codegen.plan.toml:19` 指向 `kernel/execute/pass.cpp#jitWithoutCodeGeneratorWarning`）。这是集成尖上已有的文档引用漂移，不是这次跑出来的。

