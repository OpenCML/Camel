# `stdlib-reference` 只认单行 `with <…>`（问题页，2026-10-31）

**现状。** `scripts/stdlib-reference.mjs` 的 `readDeclarations()` 遇到以 `with <` 起头的行，只把**这一行**交给 `bracketed()` 做括号配对；参数排成块状（每个参数一行、带 `///` 注释）就抛 `unbalanced brackets in: with <`。表现是 `registry.reference_is_generated`、`registry.signatures_match`、`registry.docs_complete`、`registry.migrations_stay_migrated` 四项同时红，错误信息只指向生成器内部栈。

**为什么现在才发现。** `stdlib/onnx.cml` 原本是 `with <out: string = "">` 单行；给 `onnx::export` 加 `trace`/`dump`/`prep_stats` 三个参数时排成块状就触发了。`stdlib/std.cml` 里 `std::profile` 的参数块一直是多行却没报错，是因为那条声明不走这条解析路径（它不是算子声明）——所以问题一直是隐性的，只要有一个**模块**的声明文件把遍参数排成块状就会暴露。

**值得改吗。** 值得，代价小、收益明确：

- 参数一多（`std::profile` 已经 9 个），单行会变成一长串，每个参数失去一条注释的位置；声明文件本来是文档的一半。
- 改动局部：`readDeclarations()` 里把 `with <` 起头的行向下拼接，直到 `bracketed()` 不再抛错或括号配平；文件里已经有现成的 `closingBracket()` 可复用，不需要新的词法逻辑。
- 两侧都能验证：`node scripts/stdlib-reference.mjs --verify-doc` 与 `registry.*` 四项。

**临时措施。** `stdlib/onnx.cml` 的那行声明保持单行，并在上方写了一行注释说明原因，避免下一个人再排成块状。

**若决定不改。** 那么这条约束要写进生成器的头部注释与声明文件的约定里（"参数必须单行"），否则它会继续以四个红测试的形式出现。