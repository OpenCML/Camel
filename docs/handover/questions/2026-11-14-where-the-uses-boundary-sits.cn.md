# `uses` 的边界落在哪一格：变量、句柄、还是不透明的值

## 问什么

设计原则 3.3 现在的说法是：**`uses` 只列外部资源与句柄形参，`var` 表示写；变量不写进 `uses`。**
把旧写法（每个外部资源都必须带 `var`、变量可以写进子句）撤掉之后，还有三处边界是这句话没有
说清的，本页把每一处的现状、读数和默认做法记下来，等作者裁一次：

1. **不透明的值形参能不能进子句**（`t: Txn<F>`）；
2. **只读的外部资源在无函数体的原生声明里要不要精确到"读"**（`--verify-ops` 只比一个方向）；
3. **撤掉的诊断号 0x00020C 记在哪**。

## 1. 不透明的值形参

`stdlib/graph.cml` 的事务 API 用子句把"这批调用改的是同一个事务"写出来：

```text
stdlib/graph.cml:303: export func replace::<F>(t: Txn<F>, old: Node, new: Node): void uses var t
stdlib/graph.cml:309: export func erase::<F>(t: Txn<F>, n: Node): void uses var t
stdlib/graph.cml:318: export func eval::<F>(t: Txn<F>, n: Node): Node? uses var t
stdlib/graph.cml:325: export func add_constant::<F, T>(t: Txn<F>, g: Graph, value: T): Node uses var t when is_ordered::<T>()
stdlib/graph.cml:331: export func commit::<F>(t: Txn<F>): F! uses var t
stdlib/graph.cml:335: export func abort::<F>(t: Txn<F>): void uses var t
```

`Txn` 是 `export type Txn<F>`（不透明类型），**不是** `#resource type`，所以 `t` 既不是句柄形参也不是
`var` 形参：按 3.3 的字面（"外部资源与句柄形参"），这六处都该被拒。

问题在于拒了会怎样：**事务的效应不是消失，而是掉进"没写下来"那一格**——`commit(t1)` 与
`commit(t2)` 之间不再有序，`replace` 与 `commit` 之间也不再有先后，而这正是这组 API 存在的理由。
今天 `Builder::resourceSignatureOf` 接受任何非 `var` 形参（`ResourceClass::Object`），只有变量被拒
（`SemanticDiag::VariableInUses`）；也就是说，**实现比规则的字面宽一格**。

## 2. 只读的外部资源

撤掉"外部资源一律带 `var`"之后，只读的外部资源写成不带 `var` 的形式。四处因此改了拼写（其余
`uses var <外部资源>` 是真正的写，照旧）：

| 位置 | 算子自己声明的访问 | 现在的子句 |
| --- | --- | --- |
| `stdlib/ckpt.cml:66` `load_tree` | `ckpt:load_tree`：`fs` Read、`stderr` Write | `uses fs, var stderr` |
| `stdlib/test_support.cml:27` `thread_count` | `test_support:thread_count`：`process:process` Read | `uses process` |
| `stdlib/signatures/tiles.cml:51` `source` | `tiles:source`：`tiles:driver` Read | `uses driver` |
| `stdlib/signatures/tiles.cml:57` `inspect` | `tiles:inspect`：`tiles:driver` Read | `uses driver` |

`scripts/stdlib-reference.mjs --verify-ops` 恢复了"子句说写、算子只读"的比较；**反方向没比**：
子句说读、算子写。理由是编译器自己不比模式（`src/compile/gir/builder_modules.cpp`
`resourceIdentities` 只比资源身份，注释写明"子句说碰哪些资源，不说怎么碰"），脚本若比了反方向，
就成了比编译器更严的第二套规则。代价：一条无函数体的声明把写算子写成读，编译器与脚本都放过它，
只有调用点的承诺检查会红。

## 3. 撤掉的诊断号

`0x00020C`（`ExternalResourceNeedsVar`）不再存在。号写在 `diagnostics/registry.tsv` 的一条注释里，
**不回收**（仓库惯例：号不重用，`0x00020D` 是新的 `VariableInUses`）。

## 三条路

- **甲（默认）实现按现在的宽一格**：非 `var` 形参都能进子句（不透明值也算），只拒变量；
  `--verify-ops` 只比"说写、其实读"；0x00020C 只留注释。代价：3.3 的字面读起来比实现窄，
  下一个读原则的人会以为 `uses var t` 该被拒。
- **乙 实现收到字面**：只有外部资源与 `#resource type` 的句柄能进子句，`Txn` 那六处改成别的方式
  表达事务顺序（句柄类型，或者显式的 `with` 资源）。代价：改 `stdlib/graph.cml` 的公开签名与
  所有调用点，且要作者先定"事务顺序用什么写"。
- **丙 改原则的字面**：3.3 把"外部资源与句柄形参"扩成"外部资源、句柄形参，以及不透明的值形参"。
  代价：设计原则由作者维护，本车道不动它。

## 默认建议（按它开工，不等回复）

甲，并把乙、丙两条留给作者：本车道只做"撤掉变量进子句"这一件，`Txn` 那六处**原样保留**，
`--verify-ops` 只补回一个方向。若作者裁成乙，改的是 `resourceSignatureOf` 的形参分支加一个
`isHandleType` 判定、`stdlib/graph.cml` 六个签名，以及它们的调用点。