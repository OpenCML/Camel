# `fields` 的第三个名字 `tags` 绑什么：标签名、标签值还是标签 id（2026-10-25，h475 lane）

本文件只记事实与读数，不写裁定、不猜倾向。读数在本 lane 的树（`handover/h475-static-seq-forms`，
提交 `c539f1417`，host `88zjwei`，`out/latest/bin/camel <case> std::fvm`）。

## 一、裁定第 4 条原文

`docs/handover/reviews/questions-2026-10-24-evening.md`（develop 上，`questions-2026-10-24-evening.md`
的 `static-loop-sequence-two-argument-range-and-the-tuple-reading` 一节）：

> 4. 三个名字：序列的元素是三元组时（`fields T` 带标签）允许 `for n, F, tags in fields T`，与
> `static for` 同一规则；名字个数由序列元素决定，多了少了都报错。今天的语法错误改掉。

## 二、本 lane 实现的读数

`tags` 绑的是**标签名**：一个编译期 `string[]`，元素是写在那个字段上的标签的名字，按声明的顺序
（`src/compile/gir/builder_static.cpp:591` 的 `Builder::staticFieldTagsNode`，由 `visitStaticForNode`
与 `visitStaticLoopNode` 每轮调用；取值 `module_->fieldTags(Reference(decl->name()), index)`，再
`attr::entries` / `attr::keyName`）。`fields T` 的元素在实现里**总是三部分**（名、类型、标签），
字段没有标签时第三部分是空的 `string[]`。`static for (n, F, tags) in fields T` 走同一段。

读数（`test/cases/compile/static_expansion/static_loop_head_tags.cml`，提交 `c539f1417`）：

```
tag width<w: int> on field
type Box = { #width<8> low: int, high: int }

for n, F, tags in fields Box do acc + tag_of::<typeof(tags)>()   -> 2   // typeof(tags) 是 string[]
for n, F, tags in fields Box do acc + len(tags)                  -> 1   // low 一个标签，high 没有
```

## 三、依据：从 Camel 代码读标签被放在后面那一期

- `include/camel/core/tags.h:22`：函数、类型与字段的标签由声明它的模块保存
  （`module::Module::declarationTags`），C++ 模块用 `ofDeclaration`/`ofField` 读；"reading it from
  Camel code (`match type`, `fields`) comes with the first stage of generics, as the handover says."
- `docs/handover/reviews/H51-plan.md:9`（Q5）："第一期只接受 `(n, F)`；`(n, F, tags)` 第二期随标签
  反射一起做，现在写出来报'第二期'。"
- 标签自身的值（`#width<8>` 的 `8`）由 `attr` 的值表示，读它的写法（标签反射）今天没有。

## 四、改动面

要改成别的东西（标签的**值**，或标签的 id / 句柄）只需动 `src/compile/gir/builder_static.cpp:591`
的 `staticFieldTagsNode` 一个函数：把那个编译期值换成别的数据即可，头名字、AST、GCT 与诊断都不用动。

## 五、记录不一致（本 lane 只报事实；`reviews/` 是决策者的文件，本 lane 不改）

1. `docs/handover/reviews/H51-plan.md:9` 的 Q5（`(n, F, tags)` 属第二期、写出来报"第二期"）与上引
   裁定第 4 条（要求 `for n, F, tags in fields T` 能过）已经不一致。
2. 裁定第 4 条的"多了少了都报错"与实现的读数有一处出入：实现里**少写是合法的**——
   `static_loop_head_tags.cml` 的 `for n in fields Box` 通过，`(n, F)` 也通过（develop 的语料里
   到处是 `(n, F) in fields T`）；只有**多写**、超过元素的部分数才报
   `SemanticDiag::StaticHeadNameCount`（本 lane 新增的码，`diagnostics/registry.tsv`，消息写明
   写了几个、这个形态给几个）。