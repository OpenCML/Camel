# 写在一个没人读的绑定里的效应，还发生吗？

## 一句话

`E9` 的 `wrong` 里有两类，其中两处「期望打印、实际没打印」**不是编译器吞了效应**：编译器自己报了
`SemanticDiag::EffectNotExecuted`，说那个调用永不执行——而生成器的 eager 模型照打印。**三方对不上**：
生成器的守卫说「没有效应写在非效应位置」，编译器说「这些效应永不执行」，模型说「它们会打印」。

## 现场

```
progen_361  期望 15 行，实得 5 行，stderr 恰好 3 条警告
progen_429  期望 10 行，实得 9 行，stderr 恰好 1 条警告

warning[SemanticDiag::EffectNotExecuted]: The call of 'r1' is never executed:
    nothing needs what it gives, so nothing runs it.        --> progen_361.cml:34:19
warning[...]: The call of 'map' is never executed ...      --> progen_361.cml:49:19
warning[...]: The loop is never executed ...               --> progen_361.cml:44:15
warning[...]: The branch is never executed ...             --> progen_429（`let n58 = match …` 的臂）
```

生成器自己有一道守卫 `test/tools/progen/model.py:987 effect_violations`，docstring 原话是
「the eager reference does not model [按需求值], so the generator never writes an effect in a non-effect position」。
两个种子各跑一次：`seed 429 attempts 2 effect_violations []`、`seed 361 attempts 6 effect_violations []`。
**守卫判「干净」，编译器判「不执行」。**

`361` 那三条分两组：

- **34 行**：`let n14 = r1(...)`，`n14` 全程序再没出现过；而 `func r1(...)` **函数体没有 `sync`**。
  按 3.1/3.4，一个没人读的 `let`、写在没有同步块的身体里，它的调用本来就不该执行——**编译器对**。
  守卫之所以没拦住，是因为它按 `eff` 标记判调用，而 `r1` 的效应是**间接**的（它调的函数里有 `println`）。
- **44 / 49 行**：`let n31 = [2.0 for q30 in ['m']]`、`let n32 = loop …`，同样是没人读的 `let`，
  但**在带 `sync` 的身体里**。`L7`/`L8` 那 5 对、共 10 行就丢在这两处。

## 设计原则里拉向两边的两句

- §3.4：「**块一旦执行，它直接写着的语句里的效应一定发生**」——支持「该打印」。
- §3.4：「**块本身执不执行，和任何表达式一样，看它是否被需要**」「写在里面的嵌套块按它自己的写法执行」——支持「没人读就不执行」。

## 建议的裁定（默认建议，按它开工）

**分两半，因为两组不是同一句。**

1. **34 行那组（没人读的 `let`，身体里没有 `sync`）**：**编译器对，生成器错**。
   处置：修 `model.effect_violations`，让「间接效应」也算——一个调用的效应若是通过它调用到的函数传递来的，也必须被标记。
   **不改编译器。** 这一组不是语义问题，是生成器守卫的覆盖面问题。

2. **44 / 49 行那组（没人读的 `let`，身体里有 `sync`）**：**这一组需要作者一句话。**
   两种读法各自自洽：`sync` 块是「块一旦执行，里面的效应一定发生」的载体，
   那么「没人读这个 `let`」是否等于「这个 `sync` 块不执行」？
   **默认建议：仍然不执行**——`sync` 决定的是「一旦这条语句被执行，它的效应不会被省掉」，
   不是「这条语句一定被执行」。按这个默认，**生成器不该把效应写进没人读的绑定的右值**，
   而这道守卫要能认出「这个右值里的效应是否会因为名字没人读而整个消失」。

## 为什么这不能由实现车道自己定

它是**语义**：改变量是「程序打印几行」。设计原则在这种冲突上优先于代码与测试结果，
而它自己没把这一句写死，所以归作者。**在裁定到达之前，按上面的默认建议开工**——
A 路（改编译器让它执行）是语义变更，不先做。

## 附带的一条工具账（不依赖裁定，已修）

`E9` 记 `got` 时有两道**静默截断**（60 行 + 6 行），于是「缺 1 行」被读成「缺 4 行」。
`0248a4aea` 让 `runner.capped` 在截断处写一行 `... N more line(s) truncated`。
**凡截断，必须把截断本身写成一行**——一个被截断的输出和一段本来就更短的输出，在报告里长得一样。