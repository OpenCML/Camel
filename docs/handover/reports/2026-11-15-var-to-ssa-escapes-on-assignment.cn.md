# 第 ②a 步找到真正的原因：被赋过值的局部 `var` 被记成"逃逸"

日期 2026-11-15。分支 `lane-varscope`，基线 `dd06c1851`。临时打印已删，`src/` 与基线逐字节相同。

## 探针原文（同一 build，`q1`）

```
[DBG-F] outer=1 written=1 escaping=1 port=0
```

外层守卫成立（`joinsNoValue=1`、`wits=2`、`arms=2`），被写变量恰好 1 个（`written=1`），
不是 `var` 形参（`port=0`）——**`escaping=1`：这个变量在 `escapingVariables_` 里**。

## 根因（一句话）

`Builder::requireMutableArgument`（`src/compile/gir/builder_calls.cpp:927-943`）对**每一个**交给
`var` 形参的实参都 `escapingVariables_.insert(node)`。赋值在 GIR 里就是一次调用
`__assn__(x, 1)`，第一个参数是 `var int`，于是**每一次赋值都把被赋值的局部 `var` 记成逃逸**。
那段注释写的是"实参作为资源交给被调方，单元与被调方共享，所以它保留单元、永远不化成 SSA，
无论被调方写在哪"——这条规则的本意是**用户的函数可能把单元带出去**；赋值算子不会带出去，
它就是"写"本身。

这一条同时解释了两件事：
1. 为什么 `q1`（分支里的写）在**原有**的折叠里也不化 SSA——折叠第一步就 `continue` 掉了
   `escapingVariables_.contains(cell)`（`builder_expr.cpp:1844-1846`）；
2. 为什么我上一轮那段守卫走进了块、类型也全对却什么也没改——**它根本没进到类型比较那一步**，
   被 `escapingVariables_` 挡在更前面。

## 下一步的正确改动（一行 + 注释）

`requireMutableArgument` 在记为逃逸之前判一下调用方是不是赋值算子本身：是的话不记（`callee`
已经作为参数传进来了，判它等于赋值算子的名字即可）。改完之后：

- 直线写读立刻变成 SSA（原有折叠就会做，不需要新代码）；
- 分支里的写才需要 ②a 的值合并点（上一轮那段代码的位置和类型判断都是对的，去掉 `escaping`
  这一层拦截之后它才会真正生效）；
- ②a 的窄条件不变：两个以上被写的 `var` 保持单元（`WITH` 槽按臂分）。

这一轮没改 `src/`：预算用在了探针上。下一轮第一件事就是这一行。