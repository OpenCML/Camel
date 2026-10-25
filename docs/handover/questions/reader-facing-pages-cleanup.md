# 面向读者的页面：改过的、还留着旧说法的、需要拍板的

用户直接读的那批页面（`docs/language/` 与其 `en/` 对照、`docs/cli*`、`docs/setup*`、`docs/README.cn.md`、
`docs/roadmap.cn.md`、`docs/stdlib-conventions.cn.md`）已按设计原则的语言改写：已裁定的写法当成语言本来
就有的样子写，不再带"尚未实现/保留/目标"的标注，也不再叙述以前怎么写。本页记本次权限之外的、以及改法
与工具链冲突的地方。

## 一、逐页结论

| 页面 | 结论 |
|---|---|
| `language/01_introduction.md`（+en） | 循环段重写为 `loop`（原来写"目前没有循环语法"） |
| `language/02_lexical.md`（+en） | `::` 的例子换成 `Shape::Circle`；八进制转义去掉"已取消"与编号；保留字表原样留着（原因见第三节） |
| `language/03_data_model.md`（+en） | 模块类型一行换成 `Json`；`range(a, b)` 换成 `a..b`；去掉"暂时" |
| `language/04_execution_model.md`（+en） | 未改：唯一命中是 `D:<file>` 与 `<file>` 的 Windows 路径差（见第二节） |
| `language/05_modules.md`（+en） | `Tensor::ones` 换成枚举变体；查找顺序去掉"旧的 `CAMEL_MODULE_PATH`" |
| `language/06_expressions.md`（+en） | `Tensor` 相关全部换成 `f32[]`/`f32[..]`/`f32[2, 3]`；步长切片与多维下标按设计原则 5.4 写成语言的一部分，删掉两条指向数组表示的说明；`f..g`/`a..b` 按 4.2 写成语言的一部分 |
| `language/07_statements.md`（+en） | `constas` 例子换成能编译的 `float[n]` + `loop`（输出 6.0 / 15.0）；模块类型、具名维度两处改写；类型表达式的保留项去掉"尚未实现" |
| `language/08_top_level_and_grammar.md`（+en） | 去"不再写 `constas`"一类叙述 |
| `cli.cn.md` / `cli.md` | 模块调用改 `::`；`--threads` 去掉 `CAMEL_NUM_THREADS` 的说法；`--devices` 去掉被它取代的环境变量；张量参数写法换成数组写法；遍的一览去掉旧入口与旧别名；"遍的设置是参数"去掉"不再是环境变量"；`onnx::export_model` 的调用改成文字描述（见第五节） |
| `setup.cn.md` / `setup.en.md` | 未改：没有命中 |
| `README.cn.md` | 未改：示例已是目标写法；只剩 1.0 之前的发布政策一句 |
| `roadmap.cn.md` | `enum`/标签/循环/模块缓存四条的"退役/已删除/尚未实现"改写；第 1 节已合入的 4 条按本页自己的规则删去；撤回的裁定记录去掉 |
| `stdlib-conventions.cn.md` | 伞形参数与类型限定别名去掉 `Tensor` 字样 |

## 二、该进待实现名单的片段：检查器里没有这张名单

按裁定，编译不过或输出不对的片段应记进 `scripts/check-doc-snippets.mjs` 的待实现名单，名单里的片段
不让闸门变红。**这个基线上没有这张名单**：该脚本里搜不到 `pending`/`待实现` 一类机制。所以本次只在
文档正文按目标写，名单留给能改检查器的人。今天红的 10 条全部是本次改动之前就红的：

- `docs/language/06_expressions.md` 439、460、480 与 `docs/language/en/06_expressions.md` 同名三处：
  `fields T`、`indices ts` 这两个编译期序列现在要求括号（报 `SyntaxDiag::MissingToken:
  Expected '(' before 'n'`／`'j'`）。例子里写的是设计原则的原语形态。
- `docs/language/07_statements.md` 390 与 `docs/language/en/07_statements.md` 390：同一原因
  （`static for j in indices ts`）。
- `docs/language/04_execution_model.md` 223 与 `docs/language/en/04_execution_model.md` 223：
  输出块写 `<file>`，Windows 上打印 `D:<file>`。与内容无关，只是路径写法，Linux 主机上不会红。

**问：名单加在 `scripts/check-doc-snippets.mjs` 的哪一种形式里（文件 + 片段 + 原因），由谁加？**
在它落地之前，闸门会一直红着 8 条编译失败。

## 三、保留字表：本次想动而没动

`02_lexical.md`（2.4）的保留字表不能按目标改写，原因在工具链：`scripts/camel-for-ai.mjs` 用一份写死的
五行表（`KEYWORD_GROUPS`）读这张表——行数必须是 5、行标签要逐字相等，标签的头一个词还必须与
`include/camel/parse/syntax/token_kinds.def` 里那个关键字的分组一致。

- 把 `typeof` 从"保留，尚未实现"挪到"类型与转换"（设计原则 5.6 把它列为固定原语，`07_statements.md`
  也已按"是一个完整的类型"写它），`docs.ai_reference.current` 就报
  `the 'lexical' region could not be read from its source: §2.4 has 4 of the 5 keyword rows`。
- `typeof`、`keyof` 在 token 表里的分组都是 `Reserved`，所以它们只能待在标签以"保留/Reserved"开头的
  那一行里。
- 反过来，`try`/`catch`（设计原则 3.9 明确不进语言）留在"尚未实现"里并不准确；`keyof` 按设计原则 5.6
  应当是库里取字段名的类型函数，不是关键字。

所以这张表原样留着，`07_statements.md` 里 `typeof` 已按原语写。**问：谁把
`scripts/camel-for-ai.mjs` 的 `KEYWORD_GROUPS` 与 token 表的分组一起调整，并重录
`docs/reference/camel-for-ai.md`？** 在那之前，"`typeof` 是保留、尚未实现"与"`typeof` 是一个完整的
类型"这两句话只能并存。

## 四、别的页面上与目标写法冲突的地方

- `docs/reference/camel-for-ai.md` 的保留字一行仍写 `Reserved (accepted, not implemented)`（含
  `typeof`），签名小节里还有 `Tensor` 参数"正在退场"一类说法。这一页本次禁改（它是事实来源），
  **问：谁来把它也对齐？**
- `docs/stdlib-conventions.cn.md` 第 81 行"尚未声明化的 C++ 模块（过渡）"与第 120 行的类型限定别名：
  它们描述的是今天 `stdlib/signatures/` 下还剩 10 个 `.cml` 的树状态，所以留着了。
  **问：这一行留到搬完再删，还是现在就按目标删？**
- `docs/roadmap.cn.md` 第 7 行的日期仍是 2026-10-19，第 3、4、5 节是 2026-10-19 的审查记录。记录不改；
  日期戳要不要随下一次真正盘点一起更新？
- `06_expressions.md` 的 `...xs` 展开、关键字实参 `f(a: 1)`、`e is T`、元组/结构体模式，
  `07_statements.md` 的解构 `let { x, y }`：仍写着"保留，尚未实现"。设计原则没有裁定它们的去留，
  所以保持原样。**问：这些继续等裁定，还是也要当成目标写？**
- `03_data_model.md` 的"当前实现的行为，尚待确定"、`04_execution_model.md` 的副作用追踪"仍在研究中"、
  `06_expressions.md` 的泛型高阶函数推断"目前推不出来"、`07_statements.md` 的泛型函数限制：都是设计
  原则没有裁定的行为，保留原样。

## 五、`onnx::export_model(...)` 撞上了遍形式的闸门

`test/tools/check-doc-pass-forms.mjs` 的规则是"任何已知遍名紧跟 `(` 就是被删掉的写法"。cli 两页原来
写的是程序里那次运行期调用，用点号 `onnx.export_model(fn, example, path[, dynamic_axes[, target]])`，
点号不在遍名表里，所以规则没碰到它。按目标写法写成 `onnx::export_model(...)` 之后，同一行就让
`docs.word_form_only` 与 `docs.planted_page_passes_the_page_rule` 变红。

本次把这两行改成文字描述（名字 + 实参依次是什么），不写 `(`，闸门恢复。**问：一个函数的程序级调用与
它同名的遍，这个碰撞以后每次都会出现；规则要不要放过 `cml` 围栏里、或明说"这是运行期那次调用"的行？**