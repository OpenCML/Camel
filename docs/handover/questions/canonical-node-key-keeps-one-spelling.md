# 问题：按规范 id 合并的写法，留下的是哪一个拼写

事实与读数，不含该修哪儿的判断。基座 `handover/h465-flip-integration`（`3d30c2c60`）。

## 1. 事实

- 类型的"规范 id"是 `include/camel/core/type/intern.h` 里的 `Type::canonicalId()` /
  `TypeTable::canonicalId()` / `TypeTable::canonicalIdOf(TypeId)`：同一个结构、把 intern 键里的
  **命名词**（维度名、pack 名、形参名）抹掉之后那个类型的 id。名字留在键里，所以 `f32[n]` 与
  `f32[?]` 是类型表里**两个对象**（`src/core/type/intern.cpp` 的 `intern`，命名键会顺带插入它抹名的
  孪生），但**一个规范 id**。
- 这一轮把"比类型身份"的地方从对象地址改成规范 id：`src/runtime/writings.cpp` 的 `NodeKey`
  （原为 `const Type *type`，现为 `TypeId typeId`）与 `NodeKeyHash`、同一文件的等价检查与 AC 链直通、
  `include/camel/runtime/specializations.h` 的 `RuntimeSpecializationBindingKey` 与它的哈希、
  `src/runtime/draft_session.cpp` 里同一个键的哈希、`src/runtime/draft_inline.cpp` 的绑定排序与
  CAST 直通、`src/runtime/graph.cpp` 的 `nodeContentHash`。
- 后果一：两个**只差维度名拼写**的常量写法（同 kind、同权限、同 region、同 payload、同输入）现在
  合并成**一个**节点；合并后节点带的类型是**先被索引的那个对象**的拼写（`DraftWritings::index`
  先到先得，`byKey.try_emplace`）。改动前两者地址不同，永远不合并。
- 后果二：`f32[n]` 与 `f32[?]` 的写法现在会**互相满足** `DraftWritings::assertEqualDeferred`
  的类型相等条件，也会在 `graph_edges.cpp` 的合并写法不变式下被视为同类（那里本来就是比规范 id）。

## 2. 要问的

1. 节点带的类型是哪个拼写，取决于索引顺序。若某条诊断/打印读的是**这个节点**的
   `GraphNodeHeader::dataType`（不是程序里那份声明），输出里的维度名就可能随索引顺序变。这算不算
   需要定死的地方？本车道按"身份比规范 id、打印留命名对象"的读法保留了这个后果，没有改任何打印。
2. 若要定死，应该定成哪一个：留着**写进程序的那个**拼写（现状，先到先得），还是让合并后节点的类型
   一律取抹名的那个（`TypeTable::canonical(type)`）？后者更可复现，但会让诊断丢掉维度名。

## 3. 没做到的

- 没有构造出在计划里可观察的例子：快层 `total 6219, pass 6198, fail 2, skip 11, known failures: 8`
  与基座逐字相同，`type_array.selftest` 与它的毒化变体都绿，所以不知道这个拼写差异是否真的会出现在
  某条输出里。
- 没有改 `docs/design/**` 里关于"一个类型一个对象"的写法：本轮只动实现。