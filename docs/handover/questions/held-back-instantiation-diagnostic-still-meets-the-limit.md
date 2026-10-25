# 问题：held-back 的实例化诊断仍可能撞上限额，而失败时它哪里都不去

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的提交号若在 develop 上查不到，属于那条线。


**问题一句话**：一次泛型规则实例化会开一个自己的 sink 并复制外层 sink 的错误限额，而那个 sink 里的诊断
**按设计不报给用户**——于是一个真实错误数为 0 的程序会因为一条永远不会被报出的诊断撞上限额，把整个 pass
变成失败；**这个 sink 该不该有限额，以及限额异常出现时处理器该不该说出它携带的诊断？**

**卡住**：`src/compile/gir/builder_diagnostics.cpp` 的 `SpeculativeInstantiation`（它复制外层配置，
`inner_->setConfig(outer_->getConfig())`）、`src/compile/gir/rule_templates.cpp` 的 `genericRuleInstance`
（只留 `why`）、`tools/camel-cli/main.cpp` 与 `tools/debugger/src/run.cpp` 的限额处理器；以及
`rules::simplify` 在 `--max-errors 1` 下的行为。

事实与读数，不含该修哪儿的判断。基座 `af4b633ba`（`origin/develop`）。

## 1. 事实

- 一次泛型规则实例化会开一个自己的 sink 并把诊断扣在里面：`Builder::SpeculativeInstantiation`
  （`src/compile/gir/builder_diagnostics.cpp`）建 `inner_`，并且
  **`inner_->setConfig(outer_->getConfig())`** —— 它复制外层 sink 的限额。
- 那个 sink 里的诊断**按设计不报给用户**：`genericRuleInstance`（`src/compile/gir/rule_templates.cpp`）
  只留 `why`，`SpeculativeInstantiation::reason()` 读第一条错误的正文，`why_not` 才是用户读它的地方。
- 因此**一个真实错误数为 0 的程序**，可以因为一条永远不会被报出的诊断撞上"编译错误限额"，把整个
  pass 变成失败。复现（CLI，把编译限额压到 0）：`camel --max-errors 1 <calls.cml> rules::simplify std::nvm`。
- 那条诊断逐字（调试器路径上由限额异常携带，见 `what()`）：
  `Error[SemanticDiag::NoMatchingFunction] at <install>/stdlib/rules.cml:63:62: No matching function found for argument types: <> (() => int, int); operator '+' is not defined for these operand types.`
  它是 `int_add_zero::<T>(x: T) when is_integer::<T>()` 在 `T` 等于一个函数类型上的实例化：条件是
  `is_integer::<T>()`，正文仍要实例化，于是 `x + 0` 没有可用的 `+`。

## 2. 这次只改了另一半

调试器不再给编译 sink 限 0（与命令行同一个数 `kDefaultMaxErrors`），所以调试器路径不再抛；
**"held-back sink 复制外层限额"这一条没有动**，把编译限额调到 0 仍然能复现上面那次失败。

## 3. 要问的

1. held-back 的 sink 该不该有错误限额？它的诊断不报给用户，撞限额的唯一效果是把"这个类型元组上实例化
   不成立"变成"整个 pass 失败"，而后者在命令行与调试器上都一样是错的。
2. 这类限额异常仍然可能出现（例如 `--max-errors 1`）时，限额处理器该不该把异常携带的诊断说出来？
   现在 `tools/camel-cli/main.cpp` 与 `tools/debugger/src/run.cpp` 的限额处理器都不打印 `e.what()`，
   于是运行失败而输出里一个字都没有（诊断落在被丢弃的 sink 里）。

## 4. 选项

1. **（问题 1）A — held-back 的 sink 不给限额**：它不报给用户，限额对唯一的效果是"实例化不成立"变成
   "整个 pass 失败"。代价：那个 sink 从此没有任何上限——`rule_templates` 那一族的实例化若真的发散
   （§5 第三条未查），不再有东西挡住它。
2. **（问题 1）B — 保持复制外层限额**：代价就是本页测到的那条：真实错误数为 0 的程序把整个 pass
   变成失败，命令行与调试器上都一样错，而读者看不到任何诊断。
3. **（问题 2）A — 限额处理器把异常携带的诊断说出来**：代价：要打印的那条诊断属于一个"永不报给用户"
   的 sink，位置落在用户没写过的地方（上面那条在 `stdlib/rules.cml:63:62`）。
4. **（问题 2）B — 保持不打印**：代价：运行失败而输出里一个字都没有（诊断落在被丢弃的 sink 里）。

## 5. 没做到的

- 没在 `--max-errors 1` 下跑计划，只跑了单条命令；没查这条诊断在 `why_not` 里给用户读时是什么样子。
- 没查"哪些类型元组会被枚举到函数类型"是刻意的还是 `tuplesOf` 的副作用。