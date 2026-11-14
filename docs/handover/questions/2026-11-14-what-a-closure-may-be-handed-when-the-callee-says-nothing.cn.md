# 签名什么都不说的函数收到一个闭包时，规则该不该判不安全？

## 问什么

状态规则二（设计原则 3.5；作者 2026-11-14 裁定，`origin/fsh-dev` `c8af40747`）说：闭包不得把 `var`
捕获带出声明它的那次求值。三条携出路径都在 `src/compile/gir/builder_state.cpp#carryOutOf` 里判：
作为结果、写进不属于这次求值的格子、交给**会把它存下来的**函数。第三条只对**签名说得清**的被调用者生效：

```cpp
if (calleeType == nullptr) {
    return std::nullopt; // nothing is known about what it does with what it is given
}
...
if (signature->mode != sem::ResourceSignature::Mode::Concrete) {
    return std::nullopt; // a signature that says nothing promises nothing
}
```

于是：`let g = () => acc; unknown_taker(g)`，而 `unknown_taker` 的签名（`Generic`，或根本没写）
不承诺它把 `g` 留到返回之后——**这条路径现在放行**。它可能存了（比如存进模块资源），也可能只是
调一下；静态上无从判断。

## 证据（原文照录，同一台主机、同一棵树）

| 判法 | `-Changed -ChangedBase 5ec45c70f -Jobs 8` 的红集 | 说明 |
| --- | --- | --- |
| 未知签名**也判**（保守：说不上来就算它存） | `total: 5018, pass: 4890, fail: 111` | `map`/`foreach`/`filter`/`foreach` 一族全死：`lambda_filter.*`、`value.counter_closure.*`、`new_values.*`、`fault.in_foreach_closure.*`、`entering.assignment.*`、`named_struct_types`、`nullable_assign.enters.*`、`agree_with_fvm` |
| 外部资源也算一条路（更保守） | `fail: 246` | 打印一个闭包只是打印函数值（`Function(graph=__g_48__)`），控制台不「存」它，却把 `nested_function_value.*` 等一批正例判红 |
| 未知签名**放行**（现在的形状） | `fail: 1` | 唯一的红是基线红 `runner.backend_matrix`（`plan-selection`：`test/plans/feat/mm/gc_root_window.plan.toml` 的用例没有目录规则选得中） |

一个高阶函数收一个闭包、返回时丢掉它，是最常见的写法；按保守判，**语言里最平常的代码都不合法**。
这就是为什么默认选了放行。

## 三条修法（要作者挑一条，或都不挑）

1. **要求调用方标注**：库函数如果要收闭包，必须在签名里说清它对参数做了什么（`uses`/`writes`
   已经能表达「写进 `var` 参数」「结果能带函数」）。签名说不清的被调用者收到闭包时，编译器报错并
   要求它把话说清。代价：所有把函数当参数收的库函数都得补标注（`map`/`foreach`/`filter`/`sort_by`…），
   且泛型高阶函数很难写得比「我可能存它」更准。
2. **给高阶函数加类型约束**：把「收闭包但不留」写进类型（例如一个 `FnOnce` 式的参数修饰），
   让签名说得清而不用每个库函数手写标注。代价：这是新语法面（本车道受「不加新语法」约束，不能做）。
3. **维持放行 + 写在明处**：保留现状，在 `docs/diagnostics.md` 的
   `SemanticDiag::ClosureCaptureEscapes` 一条下写明「签名无信息的被调用者不判」，让读到这条规则的人
   知道它是**有意的边界**，不是漏判，也不是待修的 bug。

## 默认建议（按它开工，不等回复）

**第 3 条：维持放行，并在诊断文档里写明。** 理由：这条边界是「静态上判不了」的直接后果，不是实现
偷懒；换第 1 条会把代价推给每一个高阶库函数（且泛型函数仍说不出准确的话），换第 2 条要加新语法，
而新语法属于另一档裁定。**最要紧的是把这条边界写在别人会读到的地方**——诊断自己的说明里，
以及上面那张红集表——否则下一个人会把它当 bug「修」成保守判，然后发现全树 100+ 条用例变红，
再把它改回来，中间没有任何记录。

若作者要第 1 条，落点是 `carriedOffAt` 里那两处 `return std::nullopt`：改成
「签名不 `Concrete` 且实参能带闭包 → 报 `ClosureCaptureEscapes`」，然后逐个库函数补标注。