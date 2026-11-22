# 前端那条原地复用路径对 `var` 形参仍然可达

**问句（一句）**：「删掉 `Builder::inPlaceRebindingOf` 与 `inPlaceCalls_`」的前提是它们已不可达 ——
但实测它们对 **`var` 形参**与**模块级 `var`** 仍在跑、原地形照样出现在图里；那么删除的前置究竟是什么？

**默认建议（本轮按此开工）**：**等 `var` 形参不再是 cell 且顶层 `var` 随 S5 变成值之后，一次性纯删**；
在此之前不删，也不先改注释。`pathAssignmentOf:1661` 那一支要**单独算**（它不依赖 `inPlaceRebindingOf`）。

> 本页的读数取自 `7f7fc4eab` 的 Release 构建（主机 `88zjwei`），而
> `git diff 7f7fc4eab origin/develop -- src/compile/gir` 为空：前端代码与 `02939ffb7` 一致，
> 所以这些读数是当前 `develop` 的读数。

## 一、命中（`git grep -n "inPlaceRebindingOf\|inPlaceCalls_" -- src include test`）

```
符号本体 3 处：builder.h:3574（声明）· builder.h:3588（成员）· builder_operators.cpp:1301（定义）
活动读写：1441（insert）· 1661（insert，另一条）· 2213（call）· 2230/2231（erase+insert）
            3365 / 4140（contains）
其余 20 余处：include/、src/builtin/ 的注释，test/ 的 plan 注释、case 注释、plan desc 与 known_fail_reason
```

## 二、三条实测链（各带图里看到的东西）

```
① `var` 形参是 cell —— 仓库自己的用例就够，不用另写程序
   test/cases/consistency/new_values/sort_rebinding.cml:25  func sorted_param(var v: int[]): int sync {
   test/cases/consistency/new_values/sort_rebinding.cml:26      v = v->sort
   ⇒ ssaAssignmentOf 退（`var` 形参没有 SSA 绑定：写回需要一个地方）
   ⇒ loopStateAssignmentOf 退（不是循环头携带的状态）⇒ unusedStatement 真 ⇒ **inPlaceRebindingOf 跑**
   `camel sort_rebinding.cml std::gir`（gir 的 span 从 0 数，25 即源码 26 行）：
     N01 [label=":var/take", type="OPER", span="25:8-25:9", value_type="int[]"]
     N02 [label="array:sort_l_in_place", type="OPER", span="25:11-25:15", value_type="int[]"]
     N03 [label=":op/assn_owned", type="OPER", span="25:6-25:7", value_type="var int[]"]
   ⇒ 这正是 `inPlaceRebindingOf` 造的形状（`assn_owned` + `:var/take` + 该算子的 in-place 形），
     而 in-place 形只有在 `inPlaceCalls_.contains` 那两处（3365 / 4140）才会被选中。

② 模块级 `var` 仍是 cell —— src/compile/gir/builder_expr.cpp:3460 明写
   「A module's top level is not a region of a body: its `var`s are cells every function of the
    module reaches, and they stay cells until stage S5」⇒ 那里 ssaAssignmentOf 恒退
   程序（6 行）：var gx: int[] = [] / func main(): int sync { gx = gx->append<1> … }
   `std::gir`：:var/take + :struct/append_arr_in_place + :op/assn_owned
   `std::nvm`：打印 `[1, 2]`   ← 真跑通，不是图上的死枝
   同一形状在 `var` 形参上也跑得通：`v = v->append<7>` 的调用方打 `2 [1, 7]`（写回发生了）

③ `inPlaceCalls_` 有第二个写者，和 rebinding 无关
   pathAssignmentOf:1661 —— cell 数组的 `xs[0] = 9`（sync 块）：:var/take + :op/set_index_in_place
   （in-place 形的声明在 src/builtin/executors/builtin.cpp:783），而在 4140 读到它才选得出来。
   ⇒ 删这个成员会**直接打断元素赋值的原地写**，不只断 rebinding。
```

**控制读数（说明那句「不可达」到底对谁成立）**：`var zs = [3, 1, 2]; zs = zs->sort` 里的 `zs` 是
同区域局部 `var`，S1 之后它是值 —— 图里没有 `:var/take`、没有 `:op/assn_owned`，`inPlaceRebindingOf`
确实不跑。**所以「已不可达」只对同区域局部 `var` 成立，对 `var` 形参与模块级 `var` 不成立。**

## 三、与已合并注释的冲突（精确到哪一句、哪个文件）

```
A. test/plans/feat/modules/array_sort_kernels.plan.toml:29-36
   「Since stage S1 … a same-region local `var` is a value, so there is no cell to take the array
    out of and the rewrite cannot run at all … The front-end rewrite and `inPlaceCalls_` are to be
    deleted with it.」
   ⇒ 对**那个程序的四个局部 `v`** 是真的；假的是「cannot run at all」这个推广，以及据此写下的
     「to be deleted with it」这句**决定** —— 本页第二节量的是它还能跑的两个入口。

B. test/plans/feat/linear/var_reference.plan.toml:1347-1351
   「`v` is a same-region local `var` … no `:var/take` … and `Builder::inPlaceRebindingOf` …
    declines for want of a cell.」
   ⇒ 同样：对 `element_assigned_append.cml` 的 `v` 是真的；作为对**机制**的陈述则只在该作用域里真。

C. 出处（不是 S3b 引入的）：这两句分别来自 `874f0507a`（"declines for want of a cell"）与
   `5c96b09d0`（"a value and not a cell"）；S3b（`02939ffb7`，合 `3fc7904da`）改写了它们**周围**的段落
   并把这两句原样留下（`src/compile/gir` 在 S3b 里一行没动）。
```

⇒ **要紧的不是「注释旧了」，而是它让下一个人以为可以整段删**：它把「同区域局部 `var` 上不跑」
写成了「整个机制不跑」，而删除的判据正是后者。

## 四、删除的前置（两件都成立，且第二件不是「等 S3b」）

```
① `var` 形参不再是 cell。
   今天它是。S3b 已经合了（02939ffb7）而它**没有**变成值 —— 所以「随 S3b 变成值」这个前提已经兑现不了，
   得先裁 `var` 形参要不要变值（相邻的一页：2026-11-19-a-var-parameter-cannot-hand-its-value-back.cn.md）。
② 模块级 `var` 随 S5 变成值（builder_expr.cpp:3460 那句 "until stage S5" 兑现）。
⇒ 在此之前删，断的是**两条腿**：rebinding（1301）与元素赋值的原地写（1661→4140）。
```

## 五、本线做了什么

```
① 没有删、没有改任何 plan 注释、没有提交代码（本页除外）：车道量到「可达」就停了。
② 删除并入 S5 之后一次性纯删；那时 `pathAssignmentOf:1661` 与 `inPlaceCalls_` 要按「谁还在读 4140」一起算。
③ 六计划没有跑：没有删除，就没有可比的改动（也避免占用 `opt` 的单跑位）。
```

## 默认建议（写死）

**默认建议**：按页首已经写死的那条开工 —— **现在不删** `Builder::inPlaceRebindingOf` 与 `inPlaceCalls_`，
也不先改 §三 A/B 那两段注释；删除并入 S5 之后做**一次纯删**，且 `pathAssignmentOf` 里 `1661` 那一支
（元素赋值的原地写）**单独算**。
**判据**：这一轮的可比读数是「什么都没动」，所以六计划逐条不变就是通过；真要动，前置两条必须先成立 ——
① `var` 形参不再是 cell（`sort_rebinding.cml` 上 `std::gir` 里不再出现 `:var/take` + `:op/assn_owned`）、
② 模块级 `var` 随 S5 变成值（`builder_expr.cpp` 那句 "until stage S5" 兑现）；并且
`git grep "inPlaceRebindingOf\|inPlaceCalls_" -- src include test` 的活动读写（1441/1661/2213/2230/2231/3365/4140）
全部归零。在那之前删，断的是两条腿。

## 决策者答（2026-11-23）：被重构取代

原地与否归执行计划（见 A 类 in-place 两页），前端只降 SSA。另记一条现状缺陷：模块顶层 `var` 按原则 3.5 是编译错误（"顶层的 `var` 没有用处：编译错误，提示用 `let`"），实现里还是 cell（"until stage S5"）。这些页出自同一个根子：GIR 建图器在一个阶段里同时定类型和建图，并用 cell、打包位、就地改开放签名来表达 `var` 形参。核心抽象第 8 节定了前端分阶段：类型检查阶段"函数类型在这里定下来，此后不再改"；建图"只做机械的翻译……`var` 只降低成 SSA（原则 3.5），`Ref` 是唯一的单元"；第 5 节定了"函数类型有唯一的主人"，第 17 节定了"'开放类型'随前端重构消失"。于是本页问的机制在新前端里不存在：调用表达式的值永远是声明的结果，`var` 形参的终值是调用的另一份输出，由调用处重绑。lane-ssa 分支不合，归档。

带进 前端分阶段 的验收输入：顶层 `var` 改报编译错误

带进 执行（核心抽象第 14 节） 的验收输入：`sort_rebinding.cml`、`xs[0] = 9` 元素写的原地形式（由执行计划给出）
