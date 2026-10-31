# 一个 `Ref` 类型能不能被赋值（或者说：`assignableFrom` 的“先解析”前提还算不算）

**提出**：2026-10-31，`src/core/type` 一族的检查普查，`src/core/type/base.cpp` 的
`Type::assignableFrom`。

**问题**：`assignableFrom` 在比较两个类型之前，有一句

```cpp
// Built-in types: composite types containing Ref must be resolved before assignment.
GUARD(to->code_ != TypeCode::Ref && from->code_ != TypeCode::Ref, "Ref type cannot be assigned");
```

它的意思写在注释里：走到这里的类型都该已经解析过，`Ref` 是尚未解析的引用类型，不该出现在
赋值判断里。普查把它从 `ASSERT` 改成 `GUARD`（每一版构建都生效）之后，**四个合法用例**当场
中止：

- `poison:compile.comprehension_context:comprehension_context.annotated`（`nvm`、`fvm`、`jit` 三个后端都红）
- `poison:compile.contextual_empty_array:context.var_sibling.nvm`

读数（`Guarantee: to->code_ != TypeCode::Ref && from->code_ != TypeCode::Ref`，
`Reading: Ref type cannot be assigned`）：

```
poison:compile.contextual_empty_array:context.var_sibling.nvm   expected four lines, got 1
stdout 为空，stderr 是 Camel Invariant Violation（上方 Guarantee/Reading）
```

这几条用例的共同点：把一个值赋给**带类型标注的绑定**，而标注本身是 `Ref` 类型（`type X = ...`
这类别名，或上下文推导里尚未解析的引用）。换句话说，`assignableFrom` 真的被用 `Ref` 调用了。

**两种读法**：

- **A：调用方错了。** “先解析再赋值”是真前提，标注在进入赋值判断之前必须 `resolve`；那么
  `comprehension_context.annotated` 与 `context.var_sibling` 是缺陷（标注没解析就用），
  修法在调用方，这一句检查保持“每一版都生效”。
- **B：不变式写窄了。** `Ref` 就是类型的一种，`assignableFrom` 对 `Ref` 也能给出答案（例如
  `Ref` 只匹配自己或解析后的目标），那么这句话不该断言“不会到达”，该改成对 `Ref` 的正常判断。

**现在怎么处理的**：这一处退回 `ASSERT`（发布版不生效），加注释点名上面四个用例，**语义未改**；
其余 54 处照常改成了 `GUARD`。整族因此停在 57/58，等的就是这个答复。

**裁定之后**：若为 A，修调用方（解析再赋值），检查恢复为发布版生效；若为 B，把这一句改成对
`Ref` 的判断，并补用例钉住“标注是 `Ref` 时赋值成立/不成立”的答案。

---

## 2026-10-31 13:50 裁定与落地状态

作者裁 **A**：`Ref` 是尚未解析的引用，`assignableFrom` 不该替它作答；那四个用例是调用方
在标注解析之前就拿去比，修法在调用方。

**落地卡在这里，页留着**：按裁定把 `assignableFrom` 那句恢复成发布版生效后，在
`gct::Builder::visitStructInto` 里试了两版修法——

1. 值类型是 `Ref` 时整条字段比较跳过、交给通用路径（`visitData(ast)`）：**四个目标用例全绿**，
   但 `rank_zero_slot_tuple_element`（nvm/fvm/jit）与 `print_and_recompile` 变红；
2. 只在“值与字段类型不相等”时跳过：同样这三条红。

原因：`fieldType->assignableFrom` 走的是**复合类型的重写版本**（`StructType`/`Array` 等），
同一处 `Ref` 值在别的程序里正是由那个重写版本答“可以”的；调用方一跳过，字面量就
从“按位置类型建”变成“按自己推出来的类型建”，三处用例因此换了形状。

所以真正需要的是**在复合类型那一层解析这个 `Ref`**（它不是名字表里的条目，而是“这个位置的
类型还没定”），而不是在 `visitStructInto` 跳过一次比较。这一笔需要动 `StructType::assignableFrom`
的入口前提，属于类型系统内部，等下一次排期；当前树因此仍是**全绿**：`base.cpp` 那处保持
`ASSERT`，整族 57/58。

### 2026-10-31 22:40 按裁定动手：量出来的与裁定预期不一样

裁定说"调用方先 resolve 标注"，我按这个方向试了三版，读数如下（每版都是全量 feat fast）：

| 版本 | 做法 | 读数 |
|---|---|---|
| v1 | 调用方整条比较跳过 → 通用路径 | 目标 4 条绿，`rank_zero_slot_tuple_element`×3 + `print_and_recompile` 红 |
| v2 | 只在"值类型 ≠ 字段类型"时跳过 | 同上三条红 |
| v3 | 在复合类型层豁免：`GUARD` 条件加"目标是复合/第三方类型时不查"，其余原样 | **17 条红**：`rank_zero_slot_field*`（5）、`rank_zero_slot_tuple_element*`（4）、`rank_zero_slot_nested_field*`（4）、`print_and_recompile`、`ort_struct_casts`、`comprehension_context.annotated`×3 |

v3 的 17 条里有 12 条是**从来没被提名过**的：`rank_zero_slot_field*`、`rank_zero_slot_nested_field*`、`ort_struct_casts`。
它们的共同点是**原始类型目标**在别处（不是 `visitStructInto`）就拿到一个 `Ref` 值——`Ref` 在这个编译器里
不只在"标注还没解析"的地方出现，它还是"这个名字的值类型暂未读出"的占位，跨了不止一个调用方。

**结论**：这一条不是"某一个调用方忘了 resolve"。它问的是**"名字可以在哪些位置被读、由谁读"**，
是类型系统内部的全局问题；按裁定在调用方逐个补 `resolve` 会是一条长链，且每一环都可能改字面量的建法
（v1/v2 已经证明）。当前保持 `ASSERT`，整族 **57/58**，树全绿（feat fast `pass: 8068, fail: 1`，唯一红是机器状态的 `gpu_preference.named_device`）。

**判据（进页）**：「跳过比较和先解析再比较，是两件事——前者改了谁按什么类型建，后者只改了什么时候能回答。」

**下一步的形态**：这页需要作者换一个问法——「一个 `Ref` 值允许被哪些判定读到，读到时应答什么」；
答出来之后，`assignableFrom` 这一句才谈得上变发布版生效。
