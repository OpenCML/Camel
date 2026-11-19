# 前端那条原地复用路径对 `var` 形参仍然可达

**问句（一句）**：「删掉 `Builder::inPlaceRebindingOf` 与 `inPlaceCalls_`」的前提是它们已不可达 —— 但实测它们对 **`var` 形参**与**模块级 `var`** 仍在跑；那么删除的前置是什么？

**默认建议（本轮按此开工）**：**并入 S5（顶层 `var` 变值）之后一次性纯删**；在此之前不删。`pathAssignmentOf` 那一支要单独算（它不依赖 `inPlaceRebindingOf`）。

## 一、命中

```
符号本体 3 处：`builder.h:3574`（声明）· `builder.h:3588`（成员）· `builder_operators.cpp:1301`（定义）
活动读写：`1441`（insert）· `1661`（insert，另一条）· `2213`（call）· `2230/2231`（erase+insert）· `3365`/`4140`（contains）
其余 20 余处：`include/`、`src/builtin/` 注释与 `test/` 的 plan/case 注释
```

## 二、「不可达」写不出来，有两条真调用链

```
① `var` 形参是 cell：`test/cases/consistency/new_values/sort_rebinding.cml:25`
      `sorted_param(var v: int[])` 里 `v = v->sort`
      ⇒ `ssaAssignmentOf` 退（`var` 形参无 SSA 绑定，它是 cell）⇒ `loopStateAssignmentOf` 退
      ⇒ `unusedStatement` 真 ⇒ **`inPlaceRebindingOf` 跑**
      实测 gir：`:var/take` + `array:sort_l_in_place` + `:op/assn_owned`
② 模块级 `var` 仍是 cell：`src/compile/gir/builder_expr.cpp:3460` 明写
      「A module's top level … stay cells **until stage S5**」⇒ `ssaAssignmentOf` 对它**恒退**
      6 行程序：`var gx: int[] = []` + main 里 `gx = gx->append<1>`
      ⇒ gir 出 `:struct/append_arr_in_place` + `:op/assn_owned`；`std::nvm` **打印 `[1, 2]`**（真跑通，不是图上死枝）
```

⇒ **⇒ 「`ssaAssignmentOf` 抢先」只对**同区域局部 `var`**成立**（`var zs=[3,1,2]; zs = zs->sort` 实测确实不再走这里），对 `var` 形参与模块级 `var` **不成立**。

## 三、`inPlaceCalls_` 还有第二个写者

```
`pathAssignmentOf:1661` —— cell 数组的 `x[0] = 9`（sync 块）→ 读在 `4140` → `:op/set_index_in_place`（`builtin.cpp:783` 声明该 in-place 形）
⇒ **⇒ 删这个成员会**直接打断元素赋值的原地写**，不只断 rebinding。**
```

## 四、与已合并注释的冲突（要紧）

```
`test/plans/feat/linear/var_reference.plan.toml` 的注释（随 `02939ffb7` 合进 develop）现在说：
   「`Builder::inPlaceRebindingOf` … declines for want of a cell」「v is a value and not a cell」
⇒ **⇒ 而实测在 `var` 形参上它在跑、原地形**出现了**（`:op/assn_owned` 在 gir 里）⇒ **那句注释现在是假的**
```

⇒ **⇒ 这不是「注释有点旧」，是**一条已合并的判据说明与现状相反**；它会让下一个读注释的人以为可以删。**

## 五、删除的前置

```
两件都要成立：
① `var` 形参随 S3b 变成值（于是它不再是 cell）
② 模块级 `var` 随 S5 变成值（`builder_expr.cpp:3460` 那句「until stage S5」兑现）
⇒ 在此之前删，会留下**一条断的腿**：元素赋值原地写（`1661`）与 rebinding（`1301`）是两条，删成员断的是前者的载体
```

## 六、本轮处理

```
① 不删、不改注释、不提交代码（车道的做法正确）
② 删除并入 S5 之后，一次性纯删；`pathAssignmentOf:1661` 那一支单独算
③ 本页落 `questions/`，等作者确认「先改注释还是先等 S5」
```