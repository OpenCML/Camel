# 类型包：一次合并里的两份实现

type packs（`..E`）在两支线上各写了一遍，表示的选法是这次合并唯一需要判断的地方。

## 两侧各有什么

- 我们这一侧把 `..E` 做成独立类型 `TypePackType`：节点自带包名，替换走
  `substituteTypeVars(type, bindings, packs)`，第三个参数是一张显式的包绑定表；
  rest 参数的类型槽存的是它展开的那个类型（`..xs: ..A` 存 `..A`）。
- 另一侧把 `..E` 做成 `TypeCallType::makeExpand(体)`，包本身是 `TypeCallType::makePack`；
  替换只有一个参数，展开在 `substituteTypeVars` 内部用 `expandPack` 分两趟完成（先量长度，
  再按下标逐个替换），rest 参数的类型槽存的是包变量本身（`..A` 里的 `A`）。

两套表示的符号几乎不重叠：一侧有 `TypePackType`、`type_pack_bindings_t`，
另一侧有 `expandTypePacks`、`restPack`、`expandPack`。

## 怎么定的

按作者先前那条对齐结论执行：表示取另一侧的 `expandTypePacks`/`restPack`/`expandPack`，
我们的 `TypePackType`、`type_pack_bindings_t` 与三参数的 `substituteTypeVars` 整体撤掉，
我们这一侧多出来的语义逐条搬过去。这是实现级对齐，不是语义变更：写出来的类型、能过的程序、
该报的错误都还是同一套。

搬过去的具体是四件事：

- `..` 的优先级：它比比较松、比 `->` 紧，读法在 `rangeExpr` 一层（`1..n+1` 是 `1..(n+1)`）。
- `when` 子句里可以写 `..条件`，逐成员判定。
- 一个类型表达的体恰好落在一个包上时，写成的那一串类型就是元组；一个展开只准对着一个包，
  但"一个"指**声明**在头里的那些包：多于一个（`::<..A, ..B>` 的体同时写 `A` 和 `B`）在展开处报
  `PackExpansionNeedsOnePack`，零个声明包是合法的（`..ParamsOf<F>`、`..WithOf<F>`、`zip`、
  `firsts::<..A>` 都靠它，包由调用绑上）；调用没给包成员时在调用处报同一个诊断。
- rest 参数写了 `..xs: ..A`，该类型进 rest 槽，返回类型按成员展开。

## 还留着的判断

裸写 `..E`（不在元组元素里、也不是 rest 参数的类型）按"成员组成的元组"读，这是原先就写在
文法与 6.12 里的读法；若要收紧到只有元组与 rest 槽可用，改的是同一处展开。

## 两处查过、不算缺口的地方

- 那条判定只数**头**声明的包，rest 槽自己写的名字不算。`func f::<A, ..B>(..xs: ..A): ..(A, B)`
  这样的声明能过（它不歧义：`A` 由 rest 槽绑，`B` 没人绑），但每次调用都被"结果类型里的变量
  没被固定参数确定"（`GenericArgumentsMismatch`）拦下，没有程序能凭这一条跑过去。
- 调用处查"残留未绑定的展开"走数组、元组、结构体与 other，不走函数类型与流类型；函数类型里
  的残留展开由后一条同样的 `GenericArgumentsMismatch` 拦下（实测），流类型那一支写不出能
  触发它的程序（`(..A)..` 的返回值无处可写），故没有补递归。