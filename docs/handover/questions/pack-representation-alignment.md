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
- 一个类型表达的体恰好落在一个包上时，写成的那一串类型就是元组；体不落在任何包上、
  或落在两个包上，都在展开处报 `PackExpansionNeedsOnePack`；调用没给包成员时在调用处报同一个。
- rest 参数写了 `..xs: ..A`，该类型进 rest 槽，返回类型按成员展开。

## 还留着的判断

裸写 `..E`（不在元组元素里、也不是 rest 参数的类型）现在按"成员组成的元组"读，
这是原先就写在文法与 6.12 里的读法，不是新加的规则；两侧在此前只在元组里用过它。
若日后要收紧到只有元组与 rest 槽可用，改的是同一处展开。