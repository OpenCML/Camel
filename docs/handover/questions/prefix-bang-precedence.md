# 前缀 `!` 比 `->` 结合得松（`!x->contains<1>` 被解析成 `(!x)->contains<1>`）

## 现象（已用构建出的二进制复现）

```camel
return !x->contains<1>
```

报 `SemanticDiag::NoMatchingFunction … (int[]); operator '!' is not defined for these operand types`——
即它按 `(!x)->contains<1>` 解析。三种写法都正常编译并运行：

```camel
return !(x->contains<1>)
let has = x->contains<1>; return !has
```

`->` 是最高一级的后缀/管道类运算符，`!` 是前缀一元；按其它语言的惯例（Rust、Swift、Kotlin 等都让
方法调用/后缀绑定更紧），`!x->contains<1>` 应当读成 `!(x->contains<1>)`。

## 选项与代价

- **(A)（建议）让 `->` 比前缀 `!` 结合得更紧**：改优先级表一处，语义是"`!` 作用于整个后缀链"。
  代价：理论上会改变今天依赖 `(!x)->...` 的写法，但那种写法**今天根本编译不过**，所以没有兼容成本。
  判别用例：`!x->contains<1>` 与 `!(x->contains<1>)` 结果相同；`!x` 单独、`!x + 1`、`!!x` 不变；
  `!x->a->b` 读成 `!(x->a->b)`。
- **(B) 保留现状，只在文档写明**：代价是每个用户都要撞一次这条诊断才能学到；诊断消息本身也不提示
  优先级（它只说 `!` 不适用于 `int[]`），所以学习成本落在错误信息上。
- **(C) 保持解析、把诊断改成提示优先级**：比 (B) 好，但仍是"用错误消息教优先级"。

## 影响

前缀 `!` 与后缀链混写是很自然的写法（`if !xs->is_empty() { … }`）。这不是边重构触及的范围，
与任何进行中的工作无关，可以随时做。

## 来源

`handover/h342-graph-ops` 施工方报告（复现用 `probe_bang.cml`，最小复现）：

```camel
func f(x: int[]): bool { return !x->contains<1> }   // 报 NoMatchingFunction
func g(x: int[]): bool { return !(x->contains<1>) } // 通过
```