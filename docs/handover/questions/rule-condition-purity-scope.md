# 条件纯度的检查范围，与 `RewriteConflict` 删掉之后的落点

## 已经落地的两件事（读数）

1. **规则定义处的条件纯度检查**（`SemanticDiag::EffectInRuleCondition`，`0x030001AD`）。两个见证程序都变成编译错误，
   都退出 1：
   - 见证 A（条件下嵌套改写）：`test/cases/modules/rules/condition_rewrite.cml:15:18 [Error]: The call of 'rewrite' in the
     function 'publish_now' has an effect, and it is inside the condition of the rule 'add_zero'.`
   - 见证 B（条件下图事务提交）：`.../condition_commit.cml:15:32 [Error]: The call of 'println' in the function
     'publish_now' has an effect, and it is inside the condition of the rule 'add_zero'.`
2. **崩溃修掉了，但成因不是问题页猜的那一处**。见证 A 死的那一次不是"旧程序被回收后仍被读"：条件的**闭包**没有被
   传进去（`modules/rewrite/module.cpp` 的 `callWhen` 只传了实参），条件体里捕获的模块级规则值读成 null，
   `engineRules` 于是在数组的空元素上解引用。修法是让 `callWhen` 把 `function->tuple()` 作为闭包交给
   `callGraph`（`NodeVMSchedPass::callGraph` 增加一个闭包形参），条件的帧才拿得到它捕获的值。

## 一、`RewriteConflict` 删掉之后，还有什么路能走到那种冲突

查下来的结论：**只有规则的条件**。

- **规则右侧写不了调用**：右侧是项语言，`publishes(x)` 这样的调用报 `RuleInvalid`："the right side has a term that is
  not an operator, a constant or a parameter"。所以"在右侧提交"这条路根本写不出来。
- **程序层面交错提交不冲突**：图事务先提交、再改写它替换掉的程序（`test/cases/modules/rules/interleaved_commit.cml`）
  跑成功，不报冲突——会话提交时若"它读过写过的图在现在的程序里没换版本"，提交会重基（`draft_session.cpp` 的
  发布回退）并发布，不丢工作。
- **条件的路**：具体规则的条件现在被检查挡住；**泛型规则的条件挡不住**（见下），它仍然走到那种冲突。

按裁定，`RewriteConflict` 这个错误种类**删掉了**：`modules/rewrite/module.cpp` 三个内核的
`GraphCommitConflict` 兜底 catch 去掉，`stdlib/rewrite.cml` 与 `stdlib/rules.cml` 的 `@fails` 去掉，
`docs/stdlib-reference.md` 重新生成，`docs/technical/46_rules.md` 的那句跟着改。没有测试钉过这个名字。

## 二、要裁定的：泛型规则的条件，检查看不到

具体规则的条件是"规则值带的函数"，能在定义处读到（`__rule_when` 的实参，可能是静态函数值，也可能是带闭包的
`Fill`）。**泛型规则没有这种东西**：`__rule_generic(name, two_way, needs, consts)` 不带两侧，模板按 id 注册，
实例化发生在运行时。于是这个程序照样编译通过、照样跑：

```camel
rule add_zero::<T>(x: T) when publish_now() { x + 0 => x }   // 条件提交一个图事务

func publish_now(): bool {
    let p = current()
    match do_publish(p) { case error e => sync { return false } case q => sync { return true } }
}
```

读数（`RewriteConflict` 删掉之后）：先打印 `added node 9`、`inner ok`，然后

```
():?:?: [Error]: Pass 'std::nvm' failed with an internal error: The commit conflicts with a newer program:
1 graph(s) the session used have another version there ('__root__').
```

也就是退回 `PassInternalError`——**正是更早那条裁定拒绝的读数**（把用户写的规则说成编译器缺陷）。删掉
`RewriteConflict` 是按"只有规则条件能走到"这一条办的，但泛型条件这条支路还在，名字却没了。

要定的：**检查要不要覆盖泛型规则的条件**？三种做法：
1. 编译期扫模板：泛型规则的模板图在模块里按名字/ID 注册，可以直接扫它的条件模板体；直接写在条件体里的效应
   （`println`、`commit`）能看见，按类型元组才出现的效应看不见。
2. 运行时实例化时报：实例化的诊断现在被 `SpeculativeInstantiation` 扣住（设计如此），要在那里开口子。
3. 只把 `RewriteConflict` 留给泛型条件：那就是"只在确实走得到的路径上保留"，但它又是规则条件这条路的
   名字——需要明确这句话包不包括泛型条件。

在第 1 或第 2 条落地之前，泛型条件里的效应既没有诊断，也没有准确的名字。

## 三、诊断的位置只有一个

要求是"在规则定义处报错"并且"点出那个效应所在的位置"。一条诊断只有一个位置，两者只能取一个：
现在是**取效应自己的位置**（`15:18` 那个 `rewrite`），并在消息里点名规则与它所在的那个函数
（"…in the function 'publish_now' … of the rule 'add_zero'"）。若希望位置取规则定义处、效应位置写进消息，
需要说一声：把源位置渲染进消息目前没有现成的接口。

## 四、同一处闭包缺口还在别处

`callWhen` 修好之后，同样"只传图、不传函数值"的调用还有两处，条件或函数体一旦捕获了值，读到的就是空：

- `modules/rewrite/analysis.cpp:203`：`vm->callGraph(fn->program(), fn->graph(), args)`
- `src/execute/camel_pass.cpp:229`：`LibraryFunction{program, graph}`，没有闭包

是否一并修，请定。