# 论文 A 车道（`edit`/`view` 前端与块降级）：四件做完，下一手从哪起

车道 `lane/editview-a`，工作树 `D:\Projects\Camel-wt\editview-a`，分支 `lane/editview-a`，
tip `b4193b4cc3`，基座 `6d1816ce21`（`origin/integration`，§15 算子面已进来）。
（块身份哈希那条审查活是别人的；早先那笔 `block_form.cpp` 的哈希修正仍在本分支上
（`e3d9219c7`），合并时看它是否已经在别处落地。）

## 四件的读数（都在 `test/plans/probe/editview.plan.toml`，`std::nvm`）

```
① 全成/全不成   body_fault_keeps_function   exit 0  block= -1 | old= 6
   体内调一个会抛的函数（1/0），块的值是那个错误，旧函数照旧答 5+1=6。
② return 走块尾  body_return                 exit 0  after the block | answer= 6
   体里先 `d(1)` 再 `return 100`：块照样提交，`return` 只结束体。
③ 反证（一对）   body_last_expression_42     exit 0  body last expression= 42 | answer= 6
                body_last_expression_43     exit 0  body last expression= 43 | answer= 6
   体尾表达式换成 42 / 43，块的值不变 —— 这才是“体的末表达式不是块的值”。
④ formatter      `-UpdateAstHashes` 跑完：wrote … re-hashed 4594 line(s), kept 0 line(s)
                随后的只读检查：ast hashes: 4594 file(s) checked, 0 stale，exit 0
```

作法：块头的那个函数在 GCT 层包进 `attempt` 的故障边界（`LINK(1)[DRef(attempt), FUNC]`），
所以 `edit` 块的值类型是 `G!`，体内失败是块的值而不是程序的结束；`view` 不包（它不改东西，
体内的故障仍是程序的）。体在 AST 层是块内自己的一个语句块，所以 `return` 结束的是体，
块头交出的那句仍在体后面照常跑。

## 层读数（tip `b4193b4cc3`，宿主 load 69，`-Jobs 8`）

`-Plan test/plans/probe,test/plans/feat/parse -Check ast-hashes`：
`[SUMMARY] total: 159, pass: 155, fail: 4`，`[TREE] clean`，`ast-hashes exit 0`。

4 条红全是 §15 设计例，逐名（三条红表里没有，第四条红表里有但已转绿）：

```
editview.sec2_fold    Unresolved type reference: 'Node'
editview.sec2_log     The name 'console' in the resource clause … neither a parameter nor a resource
editview.sec2_count   Unresolved reference: 'is_mul'
editview.sec12_step   Unresolved reference: 'replace'
```

模块面现在有了（`src/stdlib/graph.cml`：`nodes(d)`、`graphs(d)`、`graph`/`spelling` 两个只读资源……），
但这四条当初是照设计原文抄的例子，没有 `import … from graph`，用的 `is_mul` / `console` / `replace`
也还不是模块里的名字。下一手要么给它们补 import 和自写谓词，要么等 `replace`/`erase` 导出。
对照 `5450962ad` 的 630 行红表：我跑的这两层只覆盖 1 行（`parse.frontend frontend.ast_record`），
它现在绿了；新增红名 0 条，超时 0 条。

## AST 快照

整表重录过一次（25 行换值，0 行丢：`graph` 模块、`graph_*` 模块用例、`prelude`、rulesets、
stdlib 签名这些在最后一次录之后才落地的行）。`cast_pair_conversion.cml` 那行没变：
`51b8c3c912e51248 ok test/cases/runtime/oper_shape/cast_pair_conversion.cml`。

## 还没做的（下一手）

1. **“提交出来的值是一个新函数”**（`replace`/`erase` 真改图）——本车没做，等 §15 那两个算子的出口。
2. **句柄逃逸检查**——没做，`sec12_escape` 仍是**未验证**：它现在 PASS 不代表逃逸规则成立。
3. 上面那 4 条 §15 设计例的 import/谓词。

## 不许

冻结 C++（`src/stdlib/autodiff/`、`rule_templates.cpp`、C++ 优化遍）不动；不为让探针变绿改写诊断文本；
车道分支不推 origin。