# 具名实参在算子组上是否需要可用

Author: 协调者（合并闸门），2026-10-24。Status: open — 需要负责人裁定。测量来自 `handover/h387-stdlib-names`（提交 `9b98d25d5`/`6511d2cf9`/`f07e41780`，在 Linux 主机上实测）。

## 已做到的部分

标准库三处参数改名（`json.get` 的 `index`→`key`、`len` 的 `c`→`x`、`items` 的 `s`→`m`）、参考页的拒绝说明、`stdlib-conventions` 的"同义同名"约定都已落地；改名**确实进入了编译期签名**——`inspect --ops` 现在对 `json:get` 的四个 int 项与四个 string 项打印 `"<key: int> (j: Json) => Json?"` 与 `"<key: string> (j: Json) => Json?"`。

## 但"具名调用随之可用"不成立，而且改名本身做不到

实测（同一棵树上）：

- `get(j, key: 'port')` 与 `j->get<key: 'port'>` → `error[SemanticDiag::ParamArgumentCallee]: The argument 'key' is given by name, and the signature of 'DREF: get' is not known where the call is written.`（链式写法报 `WithArgumentCallee`，签名 `'WITH: argcnt=1'`）
- `len(x: m)`（Map）**连** `len(arr: [1,2,3])` 都报同一诊断（`(string) => int`）。
- `items(m: s)`（Set）→ `ParamArgumentCallee ... 'DREF: items'`。

机理（读码后由探针确认）：具名实参的绑定为一个调用构建重载集，而它只从 Camel **声明图**里取——`src/compile/gir/builder_calls.cpp:899 declarationsOfCallee` 在 callee 的 `drefTarget` 是 `oper_group_ptr_t` 时返回 `nullopt`（`:914-918`）。所以**单个声明**的算子能按名绑定（`kind(j: j)` 编得过），**算子组不行**（`as_int(j: j)` 被拒）。这正是 `optim` 的 Camel 模块集合（h376 的用例）能工作、而标准库算子集合不能的原因。另外，**内建**算子的编译期名字来自 C++ 签名字符串而不是那个参考文件：`src/builtin/operators/collection.cpp:286` 的 `:coll/len = "(c: Map<K, V> | Set<T>) => int"`、`:coll/set_items = "(s: Set<T>) => T[]"`，`src/builtin/modules/globals.cpp:1927/1931` 的 `:struct/len_str = "(string) => int"`（**根本没有名字**）与 `:struct/len_arr = "<> (arr: T[]) => int"`；`stdlib/signatures/globals.cml` 注明"仅参考、不编译"，而 `--verify-ops` 只比个数不比名字——所以只改参考文件时一切照旧是绿的。（该车道正在把这几条编译期名字对齐到 x/m，并加一条"集合内同一位置名字一致"的注册表测试，这部分不阻塞。）

## 选项与代价

1. **扩展绑定到算子组**（在 `declarationsOfCallee` 里把一个"每一项都有带参数名的固定签名"的 `oper_group_ptr_t` 当作该调用的声明集；动态项需要把声明文本转成名字）：按名调用对标准库算子组可用。代价：**改变每一个库算子组的解析**（`contains` 会从今天的泛化拒绝变成按名的 `OverloadParameterNames` 拒绝），需要一个单独测量过的切片与逐库回归。
2. **不动绑定，把范围写清**：具名实参只在"存在 Camel 声明集"的地方可用，标准库里只有内建单声明算子与模块自己的集合能用；参考页写明这一点。代价：**具名参数这条裁定在标准库上只兑现文档价值**——名字没有任何调用能用到。

## 建议

**选 1，但作为当前第一梯队之后的一个单独切片**，理由：一个"没有任何调用能用到的名字"是对用户不成立的承诺，而选 2 要把它当缺陷写进参考页。验收建议两句：`get(j, key: 'port')` 与 `len(x: m)`、`items(m: s)` 按名绑定（C++ 名字对齐之后），且 `contains` 按名以 `OverloadParameterNames` 拒绝；整层绿，并有一条注册表测试钉住"集合内同一位置同名"。
