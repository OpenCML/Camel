# 用例账本：读到的、留下的、还没定的

`test/cases/**` 的普查已经做完，"每个用例都被某个计划用到"并进了 `scripts/plan-inventory.mjs`
（第四条检查，快层有 `test/plans/feat/gate/case_ledger.plan.toml`）。这一页只记那条车道权限之外、
需要负责人或对应车道拍板的事。

## 留下的六条例外，理由是否成立

`CASE_EXCEPTIONS` 现在有六条，账本每次都会打印条数（今天 6 recorded）：

- `test/cases/perf/gc/{build_tree,fib,jit_const,map_chain}.cml`、`test/cases/para/core/block_sort_merge.cml`：
  读取它们的是手工工具（`test/tools/gcbench/gcbench.py`、`test/tools/perfbaseline/baseline.py`），
  计划都不跑。按"没有计划用到就删"该跟那些工具一起处置；按本次的间接引用口径
  （"作为夹具目录的一部分被读取"）它们算被读到。**问：留还是删？**
- `test/cases/compile/array_alias/array_alias.plan.toml`：计划文件却躺在 `test/cases` 下，
  属于张量别名翻转那条车道。账本记它"保留并说明理由"，没有搬动（搬动会改计划选择）。
  **问：搬进 `test/plans/`，还是原地保留？**

## 删掉的用例，还有一处说明指向它

`test/tools/facts-cost.py` 的用法示例（第 25-27 行）三条 `--case` 都以
`test/cases/facts/cost_many_tensor.cml` 为工作负载，该文件随本次普查删除。按"以后需要再测，
从报告里的说明重建"，示例已没有可指的对象。**问：示例留着当那次测量的记录，还是改指现存用例？**
（这条车道没有改这个文件。）

## 账本口径：注释里的文件名算引用，注释里的目录名不算

提交 `dc4d97167` 的说明写了五处来源，最后落在两条规则上：

- 文件被任何文本按名字点到（注释、`desc` 都算）就算被读到——计划说"期望值来自某个独立实现"
  时，点到的就是一个有人要重跑的文件；
- 目录只有**代码**打开才算：手写的"p01-p26 在 test/cases/compile/namespace_rules"这种索引、
  `test/plan-selection.json` 的目录规则都不算；`path.join(ROOT, 'test', 'cases', ...)` 这种
  拼出来的夹具目录算。

**问：这条口径对不对？** 它决定了 `p23_graph_names.cml` 与 `calc.py` 的处置，也决定以后新落的
用例怎样才能被认作"被用到"。

## 顺带发现：以某个用例命名的那条测试，跑的是另一个用例

`test/plans/feat/compile/namespace_rules.plan.toml` 里 `ns.p23_graph_names` 的名字来自第 23 行，
`case` 却是 `p01_qualified.cml`，断言也是 p01 的 `lib::` 系列。这次没有改这条既有测试（改它的
`case` 就得改它的断言），而是新增 `ns.p23_graph_names.entry_module` 锁 p23 自己的值：入口模块的
函数在 dump 里不带模块名，`f` 与 `N::f` 是两个标签。**问：原测试是否要改指 p23？**