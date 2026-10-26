# 三处"代码与准则/用例口径"冲突（施工方只报告、未改）

（都来自"fail 与 never"那条车道的收尾。它把 24 条红治好了，剩下 10 条**全部**卡在这三处口径上，所以它一条都没擅自改。）

## 一、`Json!?` 与 `Json?!`：准则与实现谁是准的

准则 11.2 明写 `decode(text)->get<'a'>` 的类型是 **`Json!?`**，而实现给出的是 **`Json?!`**（`?` 与 `!` 的叠层顺序反了）。后果（逐字，5 个 `json.accessor_error` 用例 × 4～5 个后端）：

```
This 'match' on a value of type 'Json?!' is not well formed: 'case null' needs a value that may be null.
'??' cannot be applied here: its left side has type 'Json?!', which is not a 'T?'.
```

请裁：**以准则为准**（实现把叠层顺序改成 `T!?`），还是**以实现为准**（准则那行改成 `Json?!`）？顺带确认外层是 `!` 还是 `?` 的规则（"`!` 在外"意味着"先看失败、再看有没有值"）。

## 二、箭头形式下的复合字面量没进形参类型

4 条 `context.composite_argument`（`nvm`/`fvm`/`jit`/`prl`）逐字：

```
41:13: [Error]: No matching function found for argument types: <> (float[]). …
Available overloads:            （日志按 240 字符截断）
```

形参是 `f32[]`，实参被定型成 `float[]` —— 也就是设计原则 5.1 说的"复合字面量按形参类型定型"**在箭头形式里没生效**。看着是缺陷，但根因在箭头自身的定型，属设计层，交你判。

## 三、3.8 与用例 `err_plain_left` 的口径

3.8 写的是"对**非可空**的值判空同样是允许的，只是会警告（结果恒定）"，而用例 `err_plain_left` 期望的是**报错**。这条车道按 develop 原有的行为恢复了守卫（没有改预期），请你定哪一种口径。

## 四、一条设计后果（不是问题，只需知道）

`pure` 子句现在要求 C++ 侧有**对应的资源声明**，否则 `NativeResourceMismatch` 会让**整个模块不可导入**（施工方给 `autodiff` 的 `stop_gradient` 补了一条**空资源集**声明才让 1583 个用例里的 270 个红变绿）。如果这就是你要的形状，我们照做；如果你想要"`pure` 子句不需要配套声明"，请在资源那条规则里写一句。