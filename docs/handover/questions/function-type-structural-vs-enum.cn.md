# 问题：函数类型是结构别名（5.2），「每个函数类型一个 enum」是名义类型，两者如何并存

## 现象

`docs/design-principles.cn.md` 5.2：函数类型的名字只是别名，`(x: int) => int` 与另一处写同一签名
是**同一个类型**，可互换。作者的设计笔记（commit `df3796086`）§1 要求「每个函数类型一个
`enum`」——`enum` 是名义类型：`EnumDecl` 有进程内唯一的 `id`（`include/camel/core/type/enum.h:88`），
两处 `equals` 比的是声明，不是拼写。

于是同一个值在运行期是一个东西，在类型层面却可能是**两个不可互换的类型**。运行期替换按 `TypeId`
（`GraphTypeSubstitution`），只能命中同一对象或同一结构；命中不了的那个位置就留在旧类型上。

## 我实测到的具体现象（可复现）

`test/cases/opt/defun/value_tag.cml`：

- 值节点（`inc` 取作值）的类型打印为 `(x: int) => int pure`；
- `apply(f: (x: int) => int, x: int)` 的**参数**类型打印为 `(x: int) => int`；
- 两者**结构上只差 `pure` 标记**，编译器允许 `pure` 值进入非 `pure` 位置（可赋值），但两者 `TypeId`
  不同：按 id 替换只改值节点，`apply` 的签名与槽位还写着函数类型，提交校验器立刻报

  `graph 'main' node 2 (10): passes a FnValue_inc_1 as ordinary argument 0 of 'apply', which takes a (x: int) => int`

  这正好是文档说的「重建后的图不可运行」那一半。

我的临时解法：把「互相可赋值」的函数类型合成**一族**，一族一个 `enum`（`defun_value.cpp: 第 (3) 步`），
才一次提交通过。但这等于把 `pure`/effects 的差别从类型层面抹掉了。

## 两条路的代价

**A. 一族一个 enum（我当前采用）**：等价关系 = `A->assignableFrom(B) || B->assignableFrom(A)`。

- 收益：与 5.2 的「结构」一致；运行期本来只有一个值；跨 `pure` 边界的赋值不需要转换。
- 代价：`pure` / effects / resources 上不同的两个签名共享一个 `enum`，`gir` 里看到的类型名不再
  区分它们；若将来有人按 effects 做优化，类型里读不到这个信息了（effects 另有一套标记，这里只是
  说 enum 名不区分）。

**B. 一个拼写一个 enum**：保留 effects 区分。

- 代价：值从 `pure` 位置流向非 `pure` 位置时需要一条**枚举间的转换**（同一个 tag、不同声明），
  运行期是空操作但类型系统要认；与 5.2「同一签名就是同一类型」直接冲突（会让 `(x:int)=>int pure`
  与 `(x:int)=>int` 变成两个类型）。

**C. 不用 enum，用 `Variant` 的内建 tag 当作「无名义类型的标签值」**：与 5.2 最一致，但文档
§1 明说标签要**在值的类型里**，方案 C 把标签放回运行期表示，不作首选。

## 默认建议

**A**，并把「同一函数值类型 = 互相可赋值的签名」写进设计文档（否则下一个人还会按 `TypeId` 去替换，
再撞一次同样的校验器）。同时请作者明确：**`pure` / effects 标记是否属于 5.2 意义上的「结构」**。

## 需要作者一句话

- `pure` / effects / resources 标记是否参与函数类型的同一性？
  - 若**不参与**：现在 A 就是对的，文档补一句即可。
  - 若**参与**：跨族赋值需要什么转换（B 的 enum 间转换，还是保留一个「宽类型」的 enum 作为唯一
    表示）？