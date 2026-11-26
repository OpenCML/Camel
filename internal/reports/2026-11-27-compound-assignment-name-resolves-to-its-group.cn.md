# `a @= 2`：复合赋值写的名字落到它的算子组上（task-126 收工）

`compile.operators` / `int_mat_assign` 从红转绿，**期望值一个字没改**（它记的本来就是设计要的答案）。

## 判据 ① 改前 / 改后的原文

**改前**（`D:\Temp\t122\fail\compile.operators\int_mat_assign\stderr.txt`，tip `065bfb1690`；该文件在两个 tip 之间零改动）：

```
(int_mat_assign)/home/zjwei/camel-work/tree23/test/cases/compile/operators/int_mat_assign.cml:8:7: [Error]: Reference '__assn_mat__' is declared as imported but is not exported by its module. Check that the module exports the name '__assn_mat__' or remove the import declaration. (name=SemanticDiag::ImportNameNotExported, code=0x03000015)
```

**改后**：计划条目里钉的四段**逐段命中**（该条目 PASS，`expect_exit = 1` 也成立）——
`SemanticDiag::NoMatchingFunction`、`int_mat_assign.cml:8:5:`、`(int, int)`、`operator '@=' is not defined for these operand types`。

这四段就是 stderr 里那一条的全部内容：`builder_call.cpp:1787-1791` 把 `argTypesStr` 拼成 `"<{}> ({})"`（这里 with 侧为空 ⇒ `<> (int, int)`），`:1833-1837` 接上 `"; operator '{}' is not defined for these operand types"`（`operatorSymbolOf("__assn_mat__")` = `@=`），`:1889-1891` 以 `SemanticDiag::NoMatchingFunction` 报出，空组的 `overloadsStr` 只有一个空行。
**不能引整行文本的原因**：运行器只为失败的用例落盘 stdout/stderr（`test/tools/run-tests.mjs:3415` 写进失败目录），通过的条目没有存档；这一条是拿"计划钉的四段 + PASS"当原文证据，不是转述。

## 改动

`src/compile/src/gir/builder_expr.cpp`（`Builder::resolveDRefNode` 里 `importedName && !opt.has_value()` 那个分支），**18 行，一处**：

```cpp
if (context_ != nullptr && camel::core::operatorSymbolOf(name) != nullptr) {
    if (oper_group_ptr_t group = builtinOperatorGroupNamed(name); group != nullptr) {
        node_handle_t drefNode{graph->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{group})};
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.builtin");
        LEAVE("DREF");
        return drefNode;
    }
}
```

要点：

- `a @= b` 是编译器写的 link `__assn_mat__(a, b)`（`src/kernel/core/operator_names.cpp:50-60` 的复合赋值表，`:66` 的 `AtAssign` 分词、`parser_expr.cpp:127`、`builder_expr.cpp:500`），这个名字**程序写不出来**（`gct/builder.cpp:1463-1470`：`func __x__` 是 `ReservedIdentifier`）。
- 内建层为每个可重载形式都建组，`@` 没有内建类型，所以 `__assn_mat__` 是**空组**（`src/stdlib/std/builtin/modules/globals.cpp:1232-1233`，注释写明"有它的模块（tensor）提供 `@=`，否则拒绝"）。空组没有任何算子导出这个名字 ⇒ 名字解析在算子解析之前就报"导入了但模块没导出"（`builder_expr.cpp:1294-1298`），用户看到的是自己没写过的内部名字，还被叫去查导入声明。
- 改法：名字是**内建层确实持有的算子组**时，解引用到那个组，让算子自己回答"这些操作数类型没有重载"；不是算子组的名字，同一条 `ImportNameNotExported` 照旧。
- 语义没变：两种情况程序都被拒绝，变的只是拒绝走哪条路。

## 判据 ② 计划读数与逐名比

| run | 计划文件 | 读数 | provenance |
|---|---|---|---|
| 1 | `test/plans/feat/compile/operators.plan.toml` | `[SUMMARY] total: 17, pass: 17, fail: 0, re-run: 2, re-run passed: 2`；`RUN_EXIT=0` | tip `16fce901473cfc66fa25926b11b4d95390bc2d84`，binary built 2026-11-27T05:38:10+08:00，tree `/home/zjwei/camel-work/mm-capacity`，tier fast，jobs 8 |
| 2 | `test/plans/feat/linear/operators.plan.toml`（反向对照） | `[SUMMARY] total: 24, pass: 24, fail: 0`；`RUN_EXIT=0` | tip 同上，binary built 05:41:38 |

两轮都 `[TREE] clean`（`git status --porcelain -u normal: 0 before, 0 after`）；宿主 `[load]` run 1 前 68.28/74.78/77、run 中 76.92 ⇒ 两条 `re-run`（`string_pow_assign`、`neg_string` 首轮超时、重跑通过）是负载抖动，不是回归；构建 27.8 s / 4.1 s，测试 8.7 s / 1.4 s。

逐名比 `D:\Temp\promo21-reds-531.tsv`（531 行）：`:35` 的 `compile.operators  int_mat_assign` **消失**；该计划 17 条全绿 ⇒ **新增 0**。

## 判据 ③ 反向对照：合法复合赋值仍绿

- 同一个计划里：`tensor_add_assign`（tensor 的 `+=`）、`struct_add_assign`（程序自己重载的 `+` 的 `+=`）PASS。
- `linear/operators` 里：`compound_assign.{nvm,fvm,jit,prl}` 四条 PASS（该用例一次覆盖 `%=`、`^=`、`&=`、`|=`），`string_plus_assign.{nvm,fvm,jit,prl}` 四条 PASS（string 的 `+=`）。

## 判据 ④ 改动面

`git diff --numstat`（代码那一笔）：`18 0 src/compile/src/gir/builder_expr.cpp` —— 只有这一处。本页是另一笔（`docs(reports)`），不含代码。

## 判据 ②（同源只报）：张量上的 `@=` 也不通

代码读数：`git grep '__assn_' -- src/stdlib` **零命中** —— 没有任何算子在 `__assn_mat__` 里注册重载；`src/stdlib/tensor/ops/linalg_ops.cpp:184` 的 matmul 只导出 `{"__mat__", "matmul"}`。所以 `@=` 对任何类型都没有重载，tensor 上同样是"被拒绝"，只是拒绝的理由是"没有这个重载"，不是"这个组不存在"。

**这一格没有运行读数**：要一条要新增用例 + 计划条目（`git grep '@=' -- test/cases` 只有 `int_mat_assign.cml` 与 `tools/grammar/operators.cml` 的语法夹具），那是补功能/补用例的另一格。按派工"只报不实现"交代码依据。

## 工具记录（给下一格省一次失败）

`camel-remote.ps1 -Plan` 只吃**正斜杠**：传 `test\plans\...` 会在运行前直接退（`-Plan takes forward slashes ... is one file name on the Linux host`），不产生任何读数。