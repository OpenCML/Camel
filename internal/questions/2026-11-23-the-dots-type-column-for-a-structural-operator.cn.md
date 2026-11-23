# DOT 的 `type=` 对一个写成算子的成员读/构造写哪种名字：`OPER` 加 URI，还是 `ACCS`/`FILL` 加原来的标签
2026-11-23 ｜ 来自 `lane/cpp-knows-access` ｜ 对象：`trans.port_fill_metadata` 的 `port_names_and_fill_types`、`compile.static_expansion` 的 `comprehension_graph` 与 `static_tuple_function_materializes`

## ① 事实：三处红在导出器的类型列，不在翻译遍

三处断言的都是 DOT 行里的 `type=`（以及成员读那一行的 `label`）：

- `test/cases/trans/port_fill_metadata.py` 要求有一行
  `type="FILL"` 与 `value_type` 等于构造的四种类型，并要求有一行 `("ACCS", "int")`；
  今天的报错是 `AssertionError: ('rgir', 'ACCS', 'int')`。
- `comprehension_graph` 的 `verify_script` 从 `type="ACCS"` 的行里取 `label`，比
  `".p,.0,.1"`；今天的报错是 `Error: field accesses changed: `（列表为空——一行 `ACCS` 都没有）。
- `static_tuple_function_materializes` 数 `type="FILL"` 与 `type="ACCS"` 的行数。

翻转之后这三处的节点是 `OPER`：`gir_metadata.cpp` 的 `recordType` 对 `GCNodeKind::Oper`
答 `"OPER"`，标签取 `GCOperBody::uri()`（`:op/access_key`、`:op/fill`）。于是
`type="ACCS"` 一行都没有，成员读的 `label` 也不再是字段名。

读数（同一台主机，同一三组目标 `feat/trans`、`entry_args`、`static_expansion`）：

- 翻转树 `b2f22a9d4`：`[SUMMARY] total: 283, pass: 262, fail: 21`，三处都在红里。
- 同一棵树加 `std::cpp` 那一笔（`b49be11f4`）：`total: 283, pass: 266, fail: 17`，三处原样红。

⇒ 红与 `std::cpp` 无关：那个遍只翻译，不导出 DOT。

## ② 两条路，各带代价

- **(甲) 导出器按算子回答**：`OPER` 节点的 URI 若是四个之一，`type=` 用 `ACCS`/`FILL`
  （exec 侧已有 `shapeName`，注释写的正是「a back end's dump shows for it」），成员读的
  `label` 也回到字段名/位置。三处断言原样绿。代价：`:op/access` 与 `:op/access_key` 在 DOT
  上又分不开（除非另加一条属性），而「这个节点是 OPER」这件事不再可见——阶段 2 之后 OPER 是常态，
  不是边角。
- **(乙) 导出器不动**：`type="OPER"` + URI 作标签，三处断言改写成算子形式（按
  `:op/fill` / `:op/access_key` 计数）。代价：三处不再验「每个结构体一个 FILL、每次构造一个
  FILL」这种按 kind 的说法，改验「每个构造一次 `:op/fill`」；而 `comprehension_graph` 的
  `.p,.0,.1` 标签在 OPER 节点上没有现成来源（标签今天是 URI），要先定「成员读这一行的 label
  写什么」，那不是一条断言能顺手定的。

## ③ 我的默认：(乙)，导出器不动，三处留给翻转那一侧

本车道只改 `std::cpp`；三处照原样红，并写在车道的读数里。理由：图现在真的是 OPER，让导出器把
OPER 说成 `ACCS` 是在展示层抹掉这件事；而 (甲) 顺带要定「成员读的 label 写什么」，那是展示层的
一条规则，不该由这一条车道在修翻译遍时替它作答。

## ④ 还缺什么才算能用 (甲)

`shapeName` 只回答四个算子的名字，不回答成员读的标签。若选 (甲)，要补的是「一个 OPER 节点在
DOT 里的 `type=` 与 `label` 从哪来」这一条规则（算子记录里的什么字段），而不只是把 `OPER` 换成
`ACCS`/`FILL`。
## 决策者答（2026-11-23 19:00）

**乙，导出器不把 `OPER` 说成 `ACCS`/`FILL`。** 补上 ④ 缺的那条规则，而且它对所有算子是同一条：DOT 里一个 `OPER` 节点的 `type=` 是 `OPER`，`label` 是算子的 Camel 名字加它的编译期 `with` 实参（诊断与转储显示 Camel 名字、不显示 URI，原则 9.3；成员读的位置按计划 2.3 是编译期参数）。所以成员读一行是 `access<0>`、`access_key<"p">`，构造是 `fill`。三处断言改成按这个读：`port_fill_metadata` 数 `fill` 行与取成员行，`comprehension_graph` 从 `access_key`/`access` 行的实参取出 `p,0,1`，`static_tuple_function_materializes` 按算子名计数。`shapeName` 不再用于转储。
