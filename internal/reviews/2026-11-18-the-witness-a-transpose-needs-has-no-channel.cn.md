# 转置要一条残差当「零的见证」：那条通道 11-04 被删了，而它今天必须回来 —— 但回哪一半？

## 一、要问的那一句

`modules/transforms/transpose.cpp:1311-1315` 逐字：

> 「**Nothing records "this member wants a value" any more (2026-11-04).** The channel that used to record it -- a parameter's index per member, handed to the call site so it could forward the value from the closure -- is gone with the read that filled it by position … **`needs` is therefore empty for every member built here**；the parameter stays because the call site and `transposeSignature` are written from it.」

而作者 2026-11-18 17:50 的答复要求「转置需要某个输入形状的零、而它的余切没人读时，**前向就把这个输入的形状（或它本身）作为残差捕获进 `pb`**」。

⇒ 两句合起来：**要它回来，但回来的是「零的形状见证」，不是「law 读的值」** —— 而 11-04 撤它的理由正是后者（一个 law 读操作数的值被答成了非实参）。所以这条通道要**窄着恢复**，不是原样恢复。

## 二、为什么「只加残差」不成立（本轮实测）

`split::residualsOf` 在排序块之后**追加**每个参数值端口（既有编号一项不动），`zeroOfType` 的 `!exact` 改成 `return rt::kInvalidNodeRef`。读数：

- `fixed` 那一半仍然逐字 `fixed pullback(1.0)= ([0.0, 0.0, 0.0])`（**残差编号一项没动**，成立的）
- `open` 那一半：`TRANSPOSE function=split_of_jvp_of_const_open_backward skip=unread-parameter port=1 type=float[...] no-cotangent-zero`

⇒ **从一个红换成另一个红**，不是修好。原因：`zeroOf` 拿到的是「`lin` 的第 1 个参数」（切值），它要的是「第 1 个参数的**前向值**」那条残差，而两者之间**没有映射**。按类型匹配是错的：两个 `float[...]` 长度 2 与 3，零必须取自己那一条。

## 三、两个候选

**(1) 恢复 `needs` 通道（探针记 ask，调用点按参数索引把值作捕获端口交给成员）**
落点是现成的 `capturePortOf` 的 `find(at)`（`transpose.cpp:2256-2276`）。
**但它够不到本次失败的那条路**：`transpose(lin)` 是**顶层的值级调用**（`transposeKernel` → `buildTransposeGraph`，闭包由内核**原样复制**），那里**没有调用点可以问** —— 没有「调用点把值交进来」这一步，值已经在闭包里了，缺的只是「哪个槽是参数 k 的前向值」。

**(2′) 可推导的尾部约定**：`split` 追加的参数值残差放在闭包**末尾、按参数序**；`transpose` 用自己就有的事实推导出来——`lin` 的参数个数（`funcType()->normTypesCount()`）恰好等于追加的条数，于是参数 `at` 的前向值在 `closureOf.size() - paramCount + at`，并用**类型相等**做核对：类型不符就按名拒绝，不猜。

## 四、默认建议

**默认 (2′)**。理由两句：**(1) 落不到本次失败的路上**（顶层 `transpose(lin)` 没有调用点）；而 **(2′) 的约定是可推导、可核对的**——`transpose` 只用 `lin` 自己的参数个数与闭包长度两个数，且每一次取用都过一遍类型相等，不符就拒绝。**这不是「隐式约定」，是一条带断言的约定**，断言就在唯一那个读者里。

## 五、用途窄到一句（写进代码注释，逐字）

> **「这个通道只承载一件事：这个成员要参数 `at` 的**前向值**作零的**形状见证**。它不承载 law 读的值，也不被这个读者之外的地方读。」**

不得被 law 读值复用（11-04 撤它的理由），不得在 `zeroOf` 之外读。

## 六、判据

1. `grep 'skip=unread-parameter'` 在 `test/results` 里 = **0**（这条诊断必须消失，不是换个名字）
2. 四个 `pullback(1.0)` / `pullback(2.0)` 逐字 `([0.0, 0.0, 0.0])`（长度 3 的 `x`）
3. `git diff` 里这条通道的**读者只有一处**（多处就是做大了）
4. 残差编号：改前改后前 N 项逐字相同
## 决策者答复（2026-11-18）

**同意 (2′)，作为 `split` 与 `transpose` 之间一条写明的约定**：`split` 把每个参数的前向值按参数序追加在线性一半闭包捕获的末尾；`transpose` 只在 `zeroOf` 一处，按"`lin` 的参数个数 = 末尾这一段的长度"取出第 `at` 个作为零的形状见证。理由同页：顶层 `transpose(lin)` 没有调用点可问，(1) 落不到这条路上；(2′) 只用 `lin` 自己的类型与捕获个数就能推出、能核对。这也与 ECOOP 一侧的定义一致："依赖形状的零只从参数见证造"。第五节那句用途说明照写进代码注释；判据四条照页上的。约定写在 `split` 与 `transpose` 两处各一句，互相引用，不在第三处出现。
