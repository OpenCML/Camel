# 第 ② 步（分支合并点）：实测推翻了一个前提，改动因此比预想的大

日期 2026-11-15。分支 `lane-varscope`，基线 `2e3628b10`（再并一次 `origin/develop`）。

## 1. 先修正上一份诊断里的一处猜测

起步页写的是"JOIN 把单元自己当成一个输入，所以单元的用途里有一个折叠不认识的 JOIN"。**实测不是这样。**
`q1` 的边（`std::gir`，只留单元和合并点相关的）：

```
N03 :var/new (var int)      N04 JOIN (void)      N07 :op/assn_l (var int)      N0B :var/get (int)
N02 -> N03 [0|0 NORM]        单元拿到初值 0
N03 -> N07 [0|0 NORM]        单元进写
N07 -> N04 [0|0 CTRL]        写 -> 合并点，是控制边
N05 -> N04 [0|0 NORM]        只有 BRCH 是合并点的规范输入
N03 -> N0B [1|0 NORM]        读的数据口是单元
N04 -> N0B [1|0 CTRL]        读的次序口是合并点
```

合并点**没有**把单元当规范输入，只从写那里收了一条 `CTRL`；单元的用户只有"写"和"读"两个，
两个都是建图器记录过的。所以 `builder_expr.cpp:1855-1869` 那道 `known` 检查对 `q1` 并没有生效，
它看着像限制，其实不是这一处的病因。

真正的病因是两条叠在一起：

1. **这个分支是按"没有值"建的**：`if c then { x = 1 }` 是语句，`dropsArmValues` 为真
   （`builder_control.cpp:2608-2613`），`joinsNoValue` 随之为真，`addArm` 直接把
   `joinType = Type::Void()`（`2643-2649`）。合并点里**没有位置放这个值**，所以
   `readArmCellValue`（`1386-1423`，只在臂的类型是单元类型时才动手）也不会替它读出来。
2. 分支之后那次读 `N0B` 的"最近一次写"解析成**合并点**（`N04`），而折叠只承认
   "来源是赋值算子且有两个规范输入"（`builder_expr.cpp:1877-1901`）；合并点不是赋值算子，
   于是 `continue`，读保留单元。代码里那段注释（`1879-1882`）正是这个情形。

因此第 ② 步不是"放开折叠 + 删掉合并点的单元输入"两处小改，而是：**让一个按语句写的分支，
在它内部写了外层 `var` 时，为这个 `var` 产出一个值合并点**（裁定原话
`x = if c then 1 else x`），再让折叠承认"来源是值合并点"。这要动 `joinsNoValue` /
`dropsArmValues` / `addArm` 的分支形状，以及 `valueVersionOf` 对"分支之后的版本"的解析。

## 2. 已经测好的基线（改后要用它比）

`q1 = func main(c: bool): int sync { var x = 0; if c then { x = 1 }; println(x); return 0 }`
`q2 = func main(): int sync { var x = 0; let _ = loop i = 0 while i < 3 do sync { x = x + 1; i + 1 }; println(x); return 0 }`

| 后端 | q1 (c=true) | q1 (c=false) | q2 |
|---|---|---|---|
| `std::nvm` | `1` | `0` | `3` |
| `std::fvm` | `1` | `0` | `3` |
| `std::jit` | `1` | `0` | `3` |
| `std::prl` | `1` | `0` | `3` |

一件改后要预期的事：`q1` 现在在 stderr 上给一条
`IgnoredSideEffect: The call of '<__assn__>' is not in a sync block, and it takes a var argument`。
写变成"分支的结果"之后它不再是效应，这条警告应当消失——**stdout 不变、stderr 少一条**，
逐字节比较的是 stdout，但报告里要写明这条是有意的变化。

## 3. 我这一轮停在这里的原因

第 ② 步的改动落在分支形状上，一次成败要 build + `q1/q2` 四后端 + `feat/compile`(2464)、
`feat/opt`(1412)、`feat/parse`(115) 三份读数才看得见，而分支形状一改，节点序号会整体前移、
`ast_hashes` 与若干 `.dot` 会变。我这一轮剩下的时间不够走完"改 → 读数 → 改"至少两圈，
所以停在这里：树是全绿的，基线、边、病因、下一步落点都在这页。

下一步的落点按顺序：`builder_control.cpp:2621`（`joinsNoValue` 的判定）→ `2643-2649`
（`addArm` 里 `joinType` 的处理）→ `readArmCellValue`（让"臂里写了外层单元"也读出值）→
`builder_expr.cpp:1877-1901`（承认值合并点为版本来源）。