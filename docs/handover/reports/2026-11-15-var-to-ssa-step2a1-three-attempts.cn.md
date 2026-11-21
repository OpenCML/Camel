# 第 ②a-1 步：三次尝试，那个守卫一次都没触发

日期 2026-11-15。分支 `lane-varscope`，基线 `b3ea288d3`。

## 做了什么

按上一页的落点，在 `builder_control.cpp` 里 `visitBranchNode` 捕获 `joinWithInputs` 之后加了一段：
语句形分支（`joinsNoValue`）且合并点只有两个 WITH 槽（then 的值 + 缺席 else 的 `null`）时，若臂里
恰好写了一个不逃逸的 `var`，就把槽 1 的 `null` 换成"分支之前的版本"（`valueVersionOf` 给的写节点
的第二操作数，或单元 `:var/new` 的输入），并把合并点的类型从 `void` 改成值类型。
"槽按臂分、不按变量分"这条约束按你的要求写进了注释：两个以上被写的 `var` 保持单元，不做。

## 结果：图一点没变

三次 build，每次把以为卡住的守卫放宽一格：

| 尝试 | 放宽了什么 | `q1` 的合并点 |
|---|---|---|
| 1 | 用 `absentElse` | 槽 1 仍是 `null`，合并点仍 `void` |
| 2 | 改用 `dropsArmValues`（GCT 判的"if 没有 else"）+ `armList.size()==2` | 同上 |
| 3 | 去掉 `armList.size()`，只留 `joinWithInputs.size()==2` | 同上 |

`q1` 的图三次都还是：

```
N03 :var/new    N04 JOIN (void)    N08 -> N04 [WITH 0|0]    N09(null) -> N04 [WITH 0|1]
N0B :var/get    N03 -> N0B [NORM]  N04 -> N0B [CTRL]
```

输出三次都没变（`nvm/fvm/jit/prl` = `1 / 0 / 3`），所以这段代码是**惰性**的：它不是"改错了"，
是**根本没走到**，或者走到时 `joinsNoValue`/WITH 槽数与预期不同。

## 结论（给下一次的起手，不再是猜测）

三次放宽同一类条件都无效，说明问题不在条件的宽窄，而在**这段代码的位置**：程序里的语句形 `if`
要么没进 `visitBranchNode`，要么进了但 `joinsNoValue` 在那里不为真、WITH 槽不是 2 个。
继续猜的代价是每轮一次 build；**一条临时打印**（把 `joinsNoValue`、`absentElse`、`dropsArmValues`、
`armList.size()`、`joinWithInputs.size()` 打到 stderr，build 一次、跑一次）就能定死，比重猜三次便宜。
下次从这条打印开始，别从放宽条件开始。

## 这一轮的读数

| 项 | 读数 |
|---|---|
| `q1`/`q2` 图 | 三次尝试后与原图逐行相同（`std::gir`，槽 1 = `null`） |
| 四后端 stdout | `nvm/fvm/jit/prl` 均为 `1 / 0 / 3`，与基线一致（改前、改后都一样） |
| `feat/parse` | `total: 115, pass: 115, fail: 0` |
| 树 | `git status` 干净；编辑已 `git checkout --` 撤回（一个永不触发的守卫比没有守卫更坏） |

`git stash list` **不是空的**，有两条，都不是我的：
`stash@{0}` = `On lane-varexit: varexit-prefix (restored after an accidental pop in another worktree)`，
`stash@{1}` = `On wzj-dev: wip-static-branches`。我没有 stash 过任何东西，也没有动这两条。

`feat/compile` 与 `feat/opt` 按你说的没跑。