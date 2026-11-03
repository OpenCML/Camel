# 8 个 `*tos` 行不是运算符：它们服务于 `string(x)` 这个转换调用

给作者的观察，不是要改什么。它是"这一族为什么存在"的答案，也顺带改了我自己那张三栏表的两个错。

## 事实

1. **`:op/dtos` 在注册表里的名字是 `string`，不是 `dtos`**。`camel inspect --ops` 的原行：

   ```
   OPERATOR {"module":"<builtin>","name":"string","uri":":op/dtos","with":0,"norm":1,"purity":"pure","signature":"(float) => string"}
   ```

   `dtos` 只是 URI 的后缀。同族的 `itos`、`ltos`、`ftos`、`stos`、`ctos`、`ztos`、`atos` 一样，名字都是 `string`。

2. **它登的是转换调用 `string(x)` 的语义，不是运算符**。`include/camel/standalone/format.h#toText` 自己写着：

   ```
   /// `string(x)` (:op/itos, :op/ltos, :op/ftos, :op/dtos, :op/atos): the text of `x`.
   ```

   注册处是内置组的规则表（`src/builtin/modules/globals.cpp`，`OperatorGroup::create("string", {...})` 里每条 `":op/dtos"` 配一个 `StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::String())`）——是**静态类型解析器**，即"这个调用对 float 实参返回 string"。

3. **它已经被声明了，而且是一条通配声明**。`stdlib/prelude.cml#bf16`：

   ```
   #op(':op/*tos')
   export func string::<T>(x: T): string
   ```

   所以这 8 行**不需要新声明**——我这一轮没有写任何东西，也写不得：再写一条就是给同一个调用形状第二个声明。

4. **通配在这里是真有效，但只在一种地方有效**。绑定逻辑在 `src/compile/gir/builder_modules.cpp`：

   ```
   if (const auto builtins = context_->importModule("")) {          // 只在内置模块里找同名的组
       if (const auto entity = builtins->getNativeEntity(Reference(name)); ... entity->isOperGroup()) {
           for (const auto &[ruleUri, rule] : entity->operGroup()->resolvers())
               if (rule != nullptr && matches(uri, ruleUri)) { claim... }
   ...
   const bool ownRule = !claimed && uri.find('*') == std::string::npos && ...
   if (!claimed && !ownRule) { NativeDeclarationNotLoaded ... }
   ```

   两个条件合起来就是这条事实：**带 `*` 的 `#op` 只能靠"名字是内置组、且组的规则表能匹配"来绑定**；一旦名字不是内置组（例如 `tensor` 模块的 `from_bytes`），`claimed` 为假，而 `ownRule` 又被 `*` 直接否掉——于是那条通配永远没有生效的路。`string` 是内置组，所以 `:op/*tos` 能用；`tensor:from_bytes_*` 不是，所以用不了。

## 对三栏表的两处更正

我原来的量法有两个错，这一族同时踩到：

- **按 URI 的后缀当名字**：`:op/dtos` 被我当成名字 `dtos`，于是"找不到同名声明"——实际名字是 `string`，`string/1` 的声明就在 `prelude.cml:705`。**改用注册表 `name` 字段**后，这 8 行进"已覆盖"。
- **没认通配 tag**：`#op(':op/*tos')` 覆盖哪些 URI，静态数不出来，得按"前缀 + 后缀"匹配。更正后同样进"已覆盖"。

## 结论

这 8 行**不删、不搬出表**：它们**已被覆盖**，覆盖者是 `string::<T>(x: T): string` 那条通配声明。它们也不是运算符——**声明不到不是因为写不出，而是因为它们本来就是转换调用 `string(x)` 的语义**。

`等点名`。