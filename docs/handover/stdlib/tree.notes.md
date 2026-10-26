# tree 重写说明

文件：`tree.cml`。依据：设计原则 13.5、5.5，`reviews/stdlib-tree.md` 两轮评审（同形推导式 `T { static for … }`、`typeof`、导出函数自己递归）。没有编译过。

## 现状与改动

这个文件已经基本是范本：按字段遍历全用 `match type`、`static for`/`static loop`、`fields`、`indices`、`t.[n]`，没有一处运行期的类型检查，图上展开成直线代码。改动只有：

| 项 | 处理 | 理由 |
|---|---|---|
| `module tree` 行 | 删除 | 文件名即模块名 |
| `/// @pure`、`/// @effect …` | 删除 | 资源由编译器推出；回调的效应写在说明里（`tree_map`、`tree_reduce` 是"回调做什么，它就做什么"，没有对回调要求 `pure`） |
| `paths_at` | 删除，合进 `tree_paths::<T>(t: T, here: string = '')` | 评审第二轮第 1 条要的默认值写法；`here` 是递归用的，调用者不写 |
| 头注释 | 重写 | 去掉"语言还没有默认值"的说明 |
| `tree_map`、`tree_reduce`、`tree_unzip`、`first_field`、`first_leaf`、`unzip_at`、`dotted` | 保留 | `unzip_at` 带编译期的分量名 `constas j`，不是转手包装，保留 |
| `tree_leaves` | 仍然没有 | 缺的是"结果是异构元组、长度是各字段叶子数之和"的值级元组展开；仍是 `ckpt:tree_leaves`，列为第二期，不在库里绕 |

`with <f: F>` 的位置符合 4.4：函数 `f` 是配置，树是流过的数据（`tree->tree_map<f>`；多棵树同形时，其余的树是剩余实参 `..ts`，与"权重在 `with`、数据在圆括号"一致）。

## 待验证的写法

- `tree_paths` 现在带默认值的普通形参并且递归：`static for n, _ in fields T` 里递归调用 `tree_paths(t.[n], dotted(here, n))` 每次都显式给第二个实参，不会和默认值冲突；返回类型靠推出（泛型函数可省略返回类型），递归实例的推断要编译验证。
- `unzip_at` 的 `constas j: string` 在 `fields typeof(first_leaf(r, s))` 的 `static for` 里被调用，与旧版相同。

## 迁移

44 个文件用 `tree_*`；签名对调用者没有变（`tree_paths(t)` 仍是一个实参）。
