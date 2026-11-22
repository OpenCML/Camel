# re 重写说明

文件：`re.cml`。依据：设计原则 4.4（配置在尖括号、数据在圆括号）、3.9、10.4；设计稿 `design/stdlib/README.cn.md` 写明 `re` 要"完整正则（匹配、查找、替换、分组）"，并把 `re::test(pattern, text)` 列为违背 4.4 的约定。

## 设计

模式是配置（决定"这是哪一个函数"），文本是流过函数的数据，所以模式进 `with`，文本是普通参数：`line->find_all<'[0-9]+'>`。唯一的新原子是搜索本身（`find`，内核直接建出 `Match?`）；匹配记录、全部匹配、替换、切分都用 Camel 写在它上面。模式仍是字符串，不引入 `Regex` 句柄类型（见问题 1）。

## 函数表

| 名字 | 处理 | 理由 |
|---|---|---|
| `test(pattern, text)` | 删除；由 `has_match<pattern>(text)` 取代 | 参数顺序违背 4.4；`test` 不是判断名（约定用 `is_x`/`has_x`）。`has_match` 的体是 `find(...) != null`，`#impl(has_match_kernel)` 指向原 `re:test` 内核，省去建 `Match` 的开销 |
| `Match`（类型） | 新增，Camel 的 `export type` | `{ start, end, text, groups: (string?)[] }`；位置按码点、终点不含，与字符串下标 `text[m.start:m.end]` 直接配合；未参与匹配的分组是 `null`（不用 -1） |
| `find<pattern, start = 0>` | 新增（原子） | 第一个起点不小于 `start` 的匹配，`Match?`；内核建出 `Match`（含分组，未参与的分组是 `null`）。`start` 是带默认值的 with 参数，不必另写"从某处找起"的函数 |
| `find_all<pattern>` | 新增 | 一个 `loop` 反复调用 `find`；空匹配后前进一个码点 |
| `replace_with<pattern, by>` | 新增 | 用函数决定替换（函数是配置，写在尖括号） |
| `replace<pattern, new>` | 新增 | 一行：`replace_with` 的常量函数；`new` 按字面，不展开 `$1`（要引用分组用 `replace_with`） |
| `split<pattern>` | 新增 | 一个 `loop`；与 `str::split` 同名同义，用限定名或改名导入区分 |
| `validate(pattern): string!` | 新增（原子） | 来自外部的模式在使用前检查，失败是错误值 `RegexInvalid`，成功原样交回，可以串起来：`p->validate ?-> (q: string) => text->has_match<q>` |
| `has_match_kernel` | 私有内核（URI `re:test`） | `has_match` 的 `#impl` 内核 |

模式写错时，`find`、`has_match` 等是故障 `RegexInvalid`（模式通常是源码里的常量，错就是程序错误；动态模式先过 `validate`）。没有写 `pure`：原子的纯度在 C++ 登记，没有要求纯的回调（`by` 可以带效应，按匹配顺序调用）。

## C++ 侧改动（modules/re）

- `operators.cpp`：
  - `test` 的内核改为从 `with` 视图读 `pattern`、从 `norm` 读 `text`（原来两个都在 `norm`）；URI 仍是 `re:test`，登记名 `test` 保持。
  - 新增 `re:find`：`with <pattern, start>`、`norm = text`，返回 `Match?`（内核构造 `{ start, end, text, groups }` 结构体，位置按码点；Windows 上 `wchar_t` 是 16 位，要按码点而不是 UTF-16 单元换算）。没有 -1 哨兵，没有 `spans`。
  - 新增 `re:validate`：编译模式，成功返回原字符串，失败返回错误值 `RegexInvalid`（沿用 `regexErrorReason` 的文字）。
  - 同一模式在一次运行里多次出现时只编译一次（按模式文本缓存），否则 `find_all` 每轮都重编译（已裁定：模式保持字符串，C++ 侧缓存编译结果）。
- `module.cpp`：`declare` 表加 `find`、`validate`（纯）。
- `RegexInvalid` 现在是 `RuntimeDiag`（故障）；`validate` 返回的是错误值，种类名同为 `RegexInvalid`。目录（`docs/diagnostics.md`）需补一条"也作为错误值种类"的说明。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `test(pattern, text)` → `text->has_match<pattern>` | `test/cases/modules/std/re/basic.cml`（3 处）、`test/cases/modules/stdlib/utf8_string.cml`、`examples/text/log_filter.cml`（1 处，注释里也提到 `re.test`）、`test/cases/compile/diag_render/runtime_regex.cml` 与 `test/plans/feat/compile/golden/diag_render/runtime_regex.txt`（诊断里的算子名和调用写法变）、`test/cases/compile/native/{re_arity,re_declaration_import,re_type}.cml` |
| 文档 | `examples/README.cn.md`、`examples/README.md`、`docs/design/stdlib/README.cn.md`（"`re::test` 违背 4.4"一句随之成为已解决，可删）、`test/plans/feat/examples.plan.toml`；`stdlib-reference.md`、`camel-for-ai-stdlib-signatures.md` 由生成器重生成 |
| 其余提到 `re` 的地方 | `test/cases/tools/debugger/{cli_session,loop_frames}.test.mjs`、`test/cases/tools/lsp/editing.test.mjs`、`scripts/headers.mjs`：我没有逐个看是否按 `re::test` 取示例，迁移时需要核对 |
| 新增用例 | `find`/`find_all`/`replace`/`replace_with`/`split`/`validate` 的值用例（含空匹配、未参与的分组、非 ASCII 文本的码点位置）与 `RegexInvalid` 的故障用例 |

## 已裁定（决策者）

模式保持字符串、C++ 缓存编译；整套一起做；原语直接返回 `Match?`。

## 仍待确认

1. **正则方言**：ECMAScript 语法（`std::regex`）没有命名分组、后行断言，性能也一般。推荐：先沿用，换成 RE2 一类时 `Match` 与全部 Camel 体不用改。
2. **`replace` 的 `new` 要不要展开 `$1`？** 推荐：不展开（字面），要引用分组用 `replace_with`。

## 草稿里没有验证的写法（没有工具链，请审阅时留意）

- `find_all` 的 `loop … do match … { case null => … case m => … }`：`match` 直接作循环体，两臂都是 `(Match[], int)` 元组。
- `export type Match` 的字段 `groups: (string?)[]` 按 `docs/language/03_data_model.md` 的括号写法。
