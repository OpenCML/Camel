# 那条「副作用没有执行」的警告，现在该怎么说话

## 状态：等决策者，按默认当轮开工
## 默认建议：**不加新诊断、不改措辞；只把「出口是读者」写进规则文本与一例**

## 事实

`lane-varparam` 落地「`var` 形参写回」之后，有一条警告消失了：

```
修前（非 sync 体）  调用方读 0 0   stderr: IgnoredSideEffect ×2 + EffectNotExecuted ×1
修后                调用方读 0 1   stderr: IgnoredSideEffect ×2
```

`EffectNotExecuted` 的原文是：

> The assignment has side effects but is never executed.

它的 help 写的是：**"it runs only if something reads the variable afterwards"**。

**而这条警告消失的原因，正是那个从句现在成立了：出口就是那个读者。**

## 问题

1. 这条 help 现在**是否仍然准确**？
2. 若准确，要不要补一句「函数的出口也算读者」？
3. 若不准确，新措辞是什么？

## 为什么这不是车道能裁的

这属于**诊断优先级**——同一位置有多条规则成立时谁先答、以及一条规则的文字描述什么。
本晚已把它记成独立条目（连同 `err.assignment_to_a_captured_let_is_refused`、`err.capture_escapes`、`byref/let_assigned`、`byref/let_compound_assigned` 四条）。
**规则由决策者定，不由车道定。**

## 默认建议的推理

**help 那句话仍然是对的。** 它描述的是触发条件（「只有当后面有东西读它时它才运行」），而出口确实在它之后读它 —— 条件不成立，警告不触发，**这正是规则按设计运作的样子**。

所以默认：**一行代码不改**。理由是改措辞会让「它为什么会触发」变得不可读 —— 现有句子已经把触发条件说清了，问题只在于读者不知道「出口算读者」。

因此建议的做法是**加一条用例**，让这件事被测试而不是被文档描述：

```
test/cases/compile/var/var_parameter_write_at_exit.cml  （已存在）
  其 stderr 应恰好是 2 条 IgnoredSideEffect，不含 EffectNotExecuted
```

**判据**：若某天有人改动出口的写回，这条用例的 stderr 会变，于是「出口是读者」这件事**由测试守住**，不靠一句 help 文字。

## 若决策者不同意默认

两种改法，代价都是「要让一句话同时描述两种读者」：

1. 改 help 加一句「函数的出口也算一次读」；
2. 让 `EffectNotExecuted` 在出口是唯一读者时仍然报，但降级为一条提示。

**第 2 种我不建议**：它会把「按设计运作」重新包装成噪音，而本晚刚有一条用例（`boxes_made_per_call_ten.prl`）因为「不再随调度变化」而真正变绿 —— **让警告变安静的正确方式是把原因消掉，不是把声音调小。**

## 这一页要的答复

一句话即可：**维持默认**，或 **改 help 的措辞（请给出新句子）**。
