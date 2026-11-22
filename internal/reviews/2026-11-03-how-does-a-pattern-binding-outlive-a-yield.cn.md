# 一页一问：生成器里 `match` 臂的绑定，怎么活过 `yield`？

## 实测事实

```camel
loop s = 0 do {
  match d { Ok(v) => { yield v; } ... }
}
```

这条程序今天被拒：`a match arm binds a name and the arm then yields it: the binding lives in the arm's own graph, so it has to become a cell to outlive the yield`。

两条路径的原文（`builder.cpp`）：

- **常规 `match`**（`:3496-3503`）：`makeEnumCase(name, fields, bindings, shape)`，**然后臂的语句写进 case 节点内部**（`*gCaseNode << caseExprNode`）—— **绑定由 case 节点内部按 `bindings` 抽出，臂必须写在里面才看得见它**。
- **生成器的 `match`**（`:6302-6340`）：臂的语句由 `fill(armPc, …)` 写进 **`arms[armPc]` 的 pc 臂**，因为 **pc 是跨 `yield` 的唯一载体**；而这条路径**从不调用 `makeEnumCase`/`CaseType::Bind`** —— 绑定名根本没被建出来。

⇒ **两者不相容**：放进 case 节点 ⇒ 臂不再是 pc 臂（跨不过 `yield`）；不放进 ⇒ 没有绑定。**并且**被匹配值本身在 match 入口也只读进分支节点（`:6309`），同样活不过 `yield`。

## 问题

生成器臂里的模式绑定应该怎么表达？

- **(甲)** 被匹配值先落成 cell，再对 cell 做"变体投影"得到 payload，绑定名也落成 cell —— 需要图构建器提供"对普通值节点做变体投影"的入口（今天似乎没有，`makeEnumCase` 把投影与臂焊在一起）。
- **(乙)** 生成器的 match 臂不再走 pc 臂，改成 case 节点臂（承当"臂内语句必须能跨 `yield`"这件事另行解决）。
- **(丙)** 语言上禁止"绑定跨 `yield`"，写成诊断（**今天是这个状态，而按 16:30 的口径这是合法程序，所以本项不成立**）。

## 我方默认

**按 (甲) 做，分两步落地**：**(1)** 被匹配值在 match 入口落成 cell（这半件无论最终怎么裁都留得住，正在做）；**(2)** 再看图构建器能否把"变体投影"从 `makeEnumCase` 里单独拿出来，若能则绑定也从 cell 投影。

**(2) 需要新 API 时停手等裁**，因为那是"同一个概念在图构建器里再开一个入口"，不是车道能定的。