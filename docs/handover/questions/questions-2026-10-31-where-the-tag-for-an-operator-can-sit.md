# `#jvp` 挂不到 `prelude` 的运算符声明旁边，那它该挂在哪

日期：2026-10-31　车道：`lane-recheck`

**要作者答的一句：`#jvp` 这一族标签的挂点，是"必须与运算符声明同一模块"（那就得给它们造一个能同时看见两边的家），
还是"按名字挂、不必同模块"（那就要把挂点规则写出来）？**

## 现象（全部是这一轮的读数）

原定的最后一步是：**在 `prelude` 的 `func operator + (f32)` 旁边写 `#jvp(add_jvp)`**。它挂不进去，原因不是
判据，也不是摆放，而是：

> **`prelude` 的声明阶段连 `import { Node } from graph` 都做不到**
> （`SemanticDiag::ImportedModuleHasErrors`：`Cannot import module 'graph': it has errors`），
> **只能 `import` 纯声明的 `types`；而 `rule` 的降级需要 `import { __rule } from rewrite`，`rewrite` 又导入
> `graph`。**

于是"往前奏里加一条 `rule`、再把标签挂到运算符旁边"这条路上有两个独立的死结：**`prelude` 放不了 `rule`**，
**`prelude` 也看不见 `autodiff` 的规则**（分层）。

顺带把一个**假设**推翻了：**"标签实参在 `prelude` 里找不到同一文件的 `rule`，是解析顺序缺陷"——不成立。**

| 读数 | 结果 |
|---|---|
| 普通模块，`#jvp(mul_rule)` 写在 `rule mul_rule` **之前** | 编译通过，打印 `6.0` |
| 普通模块，`#jvp(mul_rule)` 写在 `rule` **之后** | 编译通过，打印 `6.0` |
| `import { mul } from geo`（`geo` 里既有 `rule` 又有 `#jvp(mul_rule)`） | 编译通过，打印 `6.0` |
| `prelude` 里加一条 `rule` | `ImportedModuleHasErrors`，`Cannot import module 'rewrite'` |

**所以 ② 没有代码可改**：同一个模块里前后两种顺序都通，引入进来的模块也通；`prelude` 那次报
"names nothing this program declares" 是**正确**读数（`add_jvp` 在 `autodiff` 里，`prelude` 按分层看不见它）。
硬改就是为一条不存在的缺陷动解析——这一句留给下一个人，别再试一次。

## 三个可以照字面执行的选项

**(a) `autodiff` 自己给运算符声明标签**：标签不必挂在声明处的同一模块，按名字（或限定名）挂。
- 代价：**这一条今天量过了，读数在下面"跨模块挂的读数"一节——不可行**，所以 (a) 今天做不成；
  剩下的问题是"要让它可行得改哪一处"，那是裁定之后的事。

**(b) 新增一层"只有声明、没有值"的模块**，让 `prelude` 与 `autodiff` 都看得见运算符声明。
- 代价：多一层；`prelude` 的导入顺序要定（前奏是最先编译的，这一层必须在它之前可用，且仍然不能有值）；
  并且要把 `+`/`-` 的声明从 `prelude` 挪到这一层，`prelude` 反过来导入它。

**(c) `#jvp` 一族留在 `autodiff`，并明确"标签的挂点不必与算子的声明同模块"**。
- 代价：要把挂点规则写清楚（标签写给谁看、读者按什么找到它：算子 id、限定名、还是声明处的标签），
  否则下一个人还会试着挂进 `prelude`——今晚已经试过一次。

## 我方默认建议

**(c)**，理由是它把今晚已经证明的事实写成规则，而不是再造一层：`prelude` 的边界（声明阶段不能引入有值的
模块）是**结构性的**，不会因为多一层就消失；而"标签在哪写"本来就是语言规则，写清楚比挪结构便宜。

但要说实话：**(c) 成立的前提是"跨模块挂"今天可行，而这个读数已经取到了——不可行**（见下一节）。
所以在读数回来之后，我的建议改成 **(b)**：今天唯一机械可行的路，是把运算符声明放进一个"只有声明、没有值"的
模块，让 `prelude` 与 `autodiff` 都看得见它。**这一页要作者裁的仍是那句**："挂点必须与算子声明同模块吗"——
裁"不必"，我做 (c) 并把跨模块挂这条路的失败原因查清；裁"必须"，我做 (b)，把它当成唯一的家。

## 跨模块挂的读数（2026-10-31，只读数、未改语义）

在标准库的副本上测，测完即弃：

| 写在哪 | 写法 | 读数（原文） |
|---|---|---|
| `autodiff.cml` 末尾，新函数上 | `#jvp(string)`（非限定名，`string` 是 `prelude` 的函数） | `error[SemanticDiag::ImportedModuleHasErrors]: Cannot import module 'graph': it has errors (…/graph.cml).` 锚在 `autodiff.cml` 那一行 |
| `autodiff.cml` 末尾，新函数上 | `#jvp(prelude::string)`（限定名） | 同上，同一个诊断、同一处锚点 |
| 对照：同样程序、未改的 `stdlib` | 无该标签 | `1`，通过 |

**结论一句：跨模块挂今天不可行**——它既不是"接受"，也不是页面里预期的那两种拒绝（`names nothing this
program declares` / `not one function`），而是把整个模块拖成 `ImportedModuleHasErrors`，并且**诊断指的是
`graph` 而不是那行标签**。这一点本身就是缺陷：读的人会去查 `graph`。

对照一侧还有一条限制：**`prelude` 里新加的 `export func` 不进程序的可见名**（写成 `export func probe_c`
仍报 `Unresolved reference: 'probe_c'`），所以"在 `prelude` 里挂、指向 `autodiff`"这个反向对照
今天根本测不了——前奏的导出表是固定的。