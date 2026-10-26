# 语法收口的三处收尾判断（附证据） · 2026-10-26

> 说明（协调者）：你的两桶规则（"没有裁定的写法 ⇒ 从文法拿掉、写了就是普通语法错误"／"真正的语言规则 ⇒ 说出规则本身的具名诊断"）落地时，有三处需要向你交待结论与证据。**都不是要你裁定，是报备**；若你不同意某一条，说一声我们改回来。

## 1. `InferType` 不在文法里，它是有裁定含义的内部标记

你第 1116 行把 `UnitType`/`InferType`/`DataType` 一并列进"文法里还没有含义的类型节点 ⇒ 从文法里拿掉"。落地时的实测结论是：**`UnitType`/`DataType` 确实是那样**（连同 `TypeOp` 一族、五个产生式、四个 AST 节点与四个访问器全删），但 **`InferType` 根本不在文法里**：

- 它由 `src/parse/ast/builder.cpp:1541`（`fact` 的 `with` 参数）、`:2474`、`:2506` 产出；
- 被 `src/compile/gct/builder.cpp:5554`（`paramTypeOf`）读取，`generator.cpp:219/1263` 按它分派；
- 语义就是 **DP 5.9 的"lambda 参数省略类型"**；
- **没有任何语法能写出这个标记**，所以"写了就是普通语法错误"**无处可落**。

所以只删了没有生产者的那一小段（`visitType` 的 Infer 分支、`visitInferType`、声明），**节点与三处产出保留**——删产出会连带毁掉"省略参数类型"这条已裁定的机制。若你认为这个标记该换个名字或该走别的机制，请指出。

## 2. `constas`/rest 参数"写错位置"的两处拒绝：按死代码删除

你第 1117 行把"`constas`/rest 参数写错位置"列进"真正的语言规则 ⇒ 具名诊断"。落地时发现这两处是**防御性断言**而不是可达的诊断点：`parseParentParams(true, true)` 在全仓**只有一个调用点**（`src/parse/syntax/parser.cpp:1007`，函数声明自己的形参表），其余调用点都传 `false`，**语法写不出越位形态**，也就写不出能红的用例。所以按你的另一条口径（"无生产者即死代码"）删掉两处拒绝，**只留 `ASSERT` + throw**。若你其实想让它可达（例如将来允许在别处写 `constas` 参数），那这条要等那个写法存在时再补具名诊断。

## 3. 两个通用诊断的残余引用（等各自的车道）

`ConstructNotSupported`/`FeatureNotSupported` 现在还删不掉，因为仍有引用：

- `generators`（`yield` 的降级拒绝，`gct/builder.cpp:1998`）——**作者已裁"生成器必须实现"**，这条由生成器车道在落地时删除（我们按"实现后再删两个通用码"排了顺序，不先删）；
- `rules.plan.toml:36`、`partial_application.plan.toml:117`、`subscript.plan.toml:58`、`stream.plan.toml:560` 四处测试条目，以及 `docs/language/06_expressions.md:99/:100` 两处旧码名——按各自批次（batch 4 / 生成器车道 / 最后的删码提交）处理。

另外报一条**工具面的小缺陷与已补的校验**：注册表的 `message` 字段含字面量 `{` 时会被当占位符、渲染时抛 malformed field，而 `--check` 原本不查这一条（施工时真炸过一次）；已让该车道给 `scripts/diagnostics-catalog.mjs` 补上这条校验。