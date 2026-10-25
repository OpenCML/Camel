# 面向读者的页面：改过的、还留着旧说法的、需要拍板的

用户直接读的那批页面（`docs/language/` 与其 `en/` 对照、`docs/cli*`、`docs/setup*`、`docs/README.cn.md`、
`docs/roadmap.cn.md`、`docs/stdlib-conventions.cn.md`）已按设计原则的语言改写：已裁定的写法当成语言本来
就有的样子写，不再带"尚未实现/保留/目标"的标注，也不再叙述以前怎么写。本页记本次权限之外的、以及改法
与工具链冲突的地方。

## 一、逐页结论

| 页面 | 结论 |
|---|---|
| `language/01_introduction.md`（+en） | 循环段重写为 `loop`（原来写"目前没有循环语法"） |
| `language/02_lexical.md`（+en） | `::` 的例子换成 `Shape::Circle`；八进制转义去掉"已取消"与编号；关键字表那一行的"尚未实现"由另一次改动去掉（现在写"保留"），见第三节 |
| `language/03_data_model.md`（+en） | 模块类型一行换成 `Json`；`range(a, b)` 换成 `a..b`；去掉"暂时" |
| `language/04_execution_model.md`（+en） | 未改：唯一命中是 `D:<file>` 与 `<file>` 的 Windows 路径差（见第二节） |
| `language/05_modules.md`（+en） | `Tensor::ones` 换成枚举变体；查找顺序去掉"旧的 `CAMEL_MODULE_PATH`" |
| `language/06_expressions.md`（+en） | `Tensor` 相关全部换成 `f32[]`/`f32[..]`/`f32[2, 3]`；步长切片与多维下标按设计原则 5.4 写成语言的一部分，删掉两条指向数组表示的说明；`f..g`/`a..b` 按 4.2 写成语言的一部分 |
| `language/07_statements.md`（+en） | `constas` 例子换成能编译的 `float[n]` + `loop`（输出 6.0 / 15.0）；模块类型、具名维度两处改写；类型运算那一段按设计原则写成"含义尚未决定" |
| `language/08_top_level_and_grammar.md`（+en） | 去"不再写 `constas`"一类叙述 |
| `cli.cn.md` / `cli.md` | 模块调用改 `::`；`--threads` 去掉 `CAMEL_NUM_THREADS` 的说法；`--devices` 去掉被它取代的环境变量；张量参数写法换成数组写法；遍的一览去掉旧入口与旧别名；"遍的设置是参数"去掉"不再是环境变量"；`onnx::export_model` 的调用改成文字描述（见第五节） |
| `setup.cn.md` / `setup.en.md` | 未改：没有命中 |
| `README.cn.md` | 未改：示例已是目标写法；只剩 1.0 之前的发布政策一句 |
| `roadmap.cn.md` | `enum`/标签/循环/模块缓存四条的"退役/已删除/尚未实现"改写；第 1 节已合入的 4 条按本页自己的规则删去；撤回的裁定记录去掉 |
| `stdlib-conventions.cn.md` | 伞形参数与类型限定别名去掉 `Tensor` 字样 |

## 二、待实现名单：本次工作期间它落地了，本页原本要登记的条目不再需要

`scripts/check-doc-snippets.mjs` 现在有 `PENDING` 与 `PENDING_MAX`：条目按页面与片段围栏正文的
SHA-256 前 16 位标识，必须带一行原因；片段重新能编译（并打印它的输出块）就必须删掉条目；条目匹配不到
任何片段会被拒；总数不得超过 `PENDING_MAX`，加一条要在同一次改动里抬高它。现在名单里只有一条
（`docs/reference/camel-for-ai.md` 的流片段），与本次无关。

本次一度以为要登记 10 条，其中 8 条是**本机那份现成编译器的假象**：`static for n, F in fields T`、
`for j in indices ts` 这种不写括号的头，基线里的解析器已经接受（"去掉 `static for` 头的括号"那串改动在
基线里），只是本机那份 `camel.exe` 不认——它不是按本分支的树构建的。按本分支的树在主机上重新构建后，
`docs.snippets_compile` 是绿的，所以这 8 条**不需要**条目，文档正文也照设计原则的原语形态写。

另外 2 条是 `docs/language/04_execution_model.md` 与 `en/` 同页的输出块：页面写 `<file>`，Windows 上
打印 `D:<file>`。这不是"片段跑在语言前面"，而是同一段路径在两种主机上的写法差异，Linux 主机上是绿的，
所以它也不该进名单（进了会在 Linux 上被判"已经能编译并打印"而必须删掉）。

**问：`D:<file>` 这一条要不要在页面上写成不含盘符的形式，让它在两种主机上都绿？** 那要改 04 页的输出
块，且 `en/` 同改，属于本次没有裁定的一部分。

## 三、保留字表：那一行现在写"保留"，剩下的问题在 token 表

`02_lexical.md`（2.4）的关键字表由 `scripts/camel-for-ai.mjs` 从页面读出来，再与
`include/camel/parse/syntax/token_kinds.def` 的分组对齐：它写死五行标签（`KEYWORD_GROUPS`），行数必须
是 5、标签要逐字相等，标签的头一个词还必须等于 token 表里那个关键字的分组名。另一次改动把这五行里最后
一行的标签从"保留，尚未实现"改成了"保留"（英文 `Reserved`），并同步改了 `KEYWORD_GROUPS` 与
`docs/reference/camel-for-ai.md`。本次沿用这个结果，页面那一行现在是
`| 保留 | is of try catch finally typeof keyof |`。

还没解决的是分组本身，而不是页面。同一次改动把生成器从"分组对不上就拒绝"改成了"把分歧写出来"：
`camel-for-ai.md` 现在由生成器自己给出 `The token table … groups \`enum\` (token table: \`Reserved\`;
reference: Type and conversion). This file follows the reference.`，也就是说**把 `typeof` 挪到"类型与
转换"这一行，生成器不会再拒绝**——本次试过：改完两行以后 `node scripts/camel-for-ai.mjs --check` 只说
"the 'lexical' region is not what its source produces (39 entries); run … --write"，不再说读不到这张表。
要做的只是把它挪过去，再让 `scripts/camel-for-ai.mjs --write` 重录一次生成页。

- `typeof` 按设计原则 5.6 是固定原语（`07_statements.md` 已按"是一个完整的类型"写它），放在"保留"
  一行里与页面自己的说法不一致。
- `keyof` 按同一条应当是库里取字段名的类型函数，根本不该是关键字；但它还在
  `include/camel/parse/syntax/token_kinds.def` 里，而生成器要求每一个声明了的关键字都出现在某一行，
  所以它暂时只能留在"保留"行。
- `try`/`catch`/`finally` 按设计原则 3.9 不进语言，留在"保留"里是对的；`is`、`of` 的含义尚未裁定，
  留着也对。

**问：这一挪（`02_lexical.md` 与 `en/` 各一行，加一次 `--write`）由谁做？** 它要改生成页
`docs/reference/camel-for-ai.md`，本次被划在禁改范围里，所以只做到验证为止；`keyof` 的去留还要连同
token 表一起定（那是 C++ 改动）。

## 四、别的页面上与目标写法冲突的地方

- `docs/reference/camel-for-ai.md` 不再写"not implemented"（现在是
  `| Reserved | is of try catch finally typeof keyof |`，生成器自己写出它与 token 表的分歧）；剩下的
  是签名区域里的 99 处 `Tensor` 字样，它们由 `stdlib/signatures/` 的源码生成，等那批 `.cml` 搬完会自己
  消失。这一页本次禁改（它是事实来源），**问：还需要谁做什么才算对齐？**
- `docs/stdlib-conventions.cn.md` 第 81 行"尚未声明化的 C++ 模块（过渡）"与第 120 行的类型限定别名：
  它们描述的是今天 `stdlib/signatures/` 下还剩 10 个 `.cml` 的树状态，所以留着了。
  **问：这一行留到搬完再删，还是现在就按目标删？**
- `docs/roadmap.cn.md` 第 5 行的"更新：2026-10-19"仍是旧日期，第 4、5 节是 2026-10-19 的审查记录。
  记录不改；**问：日期戳要不要随下一次真正盘点一起更新？**
- `06_expressions.md` 的 `...xs` 展开、关键字实参 `f(a: 1)`、`e is T`、元组/结构体模式，
  `07_statements.md` 的解构 `let { x, y }`：另一次改动把"保留、尚未实现"去掉了，现在只留诊断码；
  同一批里**没有**删掉的标注记在 `docs/handover/questions/doc-status-marks-that-need-a-ruling.md`。
  **问：按设计原则它们仍然没有裁定，只写"报某个诊断"会不会被读成"语言里就是这样"？**
- 还在的"未定"说法（本次都没动）：`03_data_model.md` 两处"当前实现的行为，尚待确定"、
  `04_execution_model.md` 的"副作用的管理与追踪仍在研究中"与"行为尚未决定"、`06_expressions.md` 的
  "泛型高阶函数……目前推不出来"、`07_statements.md` 的"目前的限制"与类型运算"含义尚未决定"。

## 五、`onnx::export_model(...)` 撞上了遍形式的闸门

`test/tools/check-doc-pass-forms.mjs` 的规则是"任何已知遍名紧跟 `(` 就是被删掉的写法"。cli 两页原来
写的是程序里那次运行期调用，用点号 `onnx.export_model(fn, example, path[, dynamic_axes[, target]])`，
点号不在遍名表里，所以规则没碰到它。按目标写法写成 `onnx::export_model(...)` 之后，同一行就让
`docs.word_form_only` 与 `docs.planted_page_passes_the_page_rule` 变红。

本次把这两行改成文字描述（名字 + 实参依次是什么），不写 `(`，闸门恢复。**问：一个函数的程序级调用与
它同名的遍，这个碰撞以后每次都会出现；规则要不要放过 `cml` 围栏里、或明说"这是运行期那次调用"的行？**