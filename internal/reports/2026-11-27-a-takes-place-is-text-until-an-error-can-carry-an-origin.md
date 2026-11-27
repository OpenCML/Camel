# 待办：故障的位置今天当文本带，等错误结构能带 `origin` 时升级成 `DiagLabel`

> 线：`lane/bang-suffix`（工作树 `bang-suffix`）。相关：`docs/design-principles.cn.md` 3.9 的 `x!` 那一条
> （"位置报在写 `!` 的地方，同时带上错误产生的位置"）、`0a435bb255`、`16d890b09b`。

## 今天是什么样

`x!` 把失败的值取出来、把错误原样变成故障：报告**站**在 `!` 上，错误产生的地方**当一句话**带在报告旁边。
右侧是证据：`test/cases/consistency/unwrap/take_fallible.cml` 的实跑（`--diag-format short`，四个后端逐字相同）

```
take_fallible.cml:14:23: [Error]: the number is not 12 [the error was made here: take_fallible.cml:8:35] …
```

`14:23` 是 `!`（第 23 列）；`8:35` 是 `error('NotANumber', …)` 的 `e`，即错误产生处。

## 为什么没做成 `DiagLabel`

`Diagnostic::related` 的每一条是 `DiagLabel{SourceRange range; std::string message; std::string path}`
（`src/kernel/include/camel/core/error/diagnostics/range.h:37`），**要一个源码范围**；而错误值记录的是一串文本：

- 错误结构的第三个字段是 `String`（`src/kernel/core/type/composite/tuple.cpp:215`），由编译器把 `error(...)` 的调用处
  作为追加实参填进去（`src/stdlib/std/builtin/operators/tagged.cpp:82-88`）；
- 这串文本只留 **basename**（`src/stdlib/std/cpp/fault_sites.cpp:70-75` 把目录抹掉），所以"文本 → 范围"要么解析
  `basename:line:col` 并猜哪个文件，撞名时无解；要么拿诊断自己的范围（`!`）冒充那条 note 的位置，给工具喂假数据。

两条都不做，于是走 `RuntimeFault::notes`：故障带的是**纯文本行**，进 `Diagnostic::notes`，文本渲染印成一行
`note:`（`src/kernel/core/error/diagnostics/render.cpp:776-778`），JSON 是 `"data":{"notes":[…]}`，**没有**
`relatedInformation`，没有一个范围声称自己指向错误产生处。

## 升级条件与要做的事

等错误结构能带 `origin`（即 `location` 不再只是 `String`，或在它旁边多一个 origin 字段）时：

1. take 的 lowering 不再读 `location` 的文本，改读那个 origin；
2. `__fail__` 的第三个实参改成 origin（或继续走 `carried`），把它做成
   `CarriedReport{related = {DiagLabel{origin, "the error was made here", path}}}` —— 通道已在
   `src/kernel/core/error/runtime.cpp:175-183`（`fault.carried().front().related` 逐条进 `d.related`）；
3. 那时 JSON 才有可点的 `relatedInformation`（`src/kernel/core/error/diagnostics/diagnostics.cpp:336-360`），
   LSP 侧同理（`src/tools/lang-server/server.cpp:430`）；
4. 订正本条与 3.9 的措辞：位置不再"带着文本"，而是带着一个可解析的位置；`take_where_text` 那条用例的读数
   （`<json::decode>` 这种非文件位置）要重新裁定 —— 内核记录的位置文本届时也得给出 origin，否则留空。

**不做的事**：不解析 `basename:line:col`；不为了让 JSON 好看把 `!` 的范围塞进 note。这两条都是"失败时落到错的一侧"。