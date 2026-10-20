# 前端、类型系统与标准库表面：设计审查（fsh-dev，2026-10-08 以来）

> 本文写于设计原则按主题重写之前。文中的"2.9""2.13""第 6 节"等是当时设计原则的节号；这些条文现在收在 [设计原则裁定记录](../../design-principles-log.cn.md)，旧编号到新编号的对照见该文件开头的表。

范围：`src/parse`、`src/compile`（GCT、GIR）、`core/type`、`stdlib/`、`src/builtin` 的表面、模块里新加的训练接口（tree、Bytes、from_bytes、检查点）、运算符重载、语言服务器。
只看设计（长期该不该这样），不看"能不能跑"。`@numeric` 本身已在重做，这里不再重复；下面找的是同一类问题。
每条都在代码里核对过；行号以当前 fsh-dev 为准。

---

## 高

### 1. `tree_*` 是编译器按 URI 字符串认出来的内建，`tree` 模块是空壳

- 位置：`src/compile/gir/tree.cpp:57`（23d97db7）；入口 `src/compile/gir/builder.cpp:5678`；模块 `modules/tree/module.cpp:26`、`:48`。
- 现在的做法：GIR 建图碰到一个算子组，先比 URI 字符串：

  ```cpp
  const std::string uri = ops->resolvers().front().first;
  if (uri != "tree:tree_map" && uri != "tree:tree_reduce" && uri != "tree:tree_unzip" &&
      uri != "tree:tree_paths" && uri != "tree:tree_leaves")
      return nullptr;
  const bool map = uri == "tree:tree_map", reduce = uri == "tree:tree_reduce";
  ```

  `tree` 模块只导出名字，它的 resolver 一律返回 `std::nullopt`（"The compiler resolves callbacks separately for each leaf"）；
  各操作的元数检查、语义与展开全在编译器里（236 行）。纯度声明 `{"tree_map", false}` 等也只是为了让注册表测试通过：这些名字永远不会成为 OPER 节点。
- 为什么不对：和 `@numeric` 是同一类问题。编译核心依赖某个具体模块的 URI；模块改名、换实现或再来一个"按类型展开"的库（设计稿里还有 `Key` 叶子、数组作内部节点 T1、梯度累积等），都要改编译器。
  2.9 说"现在复杂的类型计算都由 C++ 层的算子直接实现，用户很难写出通用的泛型算子。这一项优先级很高"，训练设计稿 1.1 也写着"缺的只是让用户也能写"。这次的做法正好相反：连 C++ 模块都写不了，只能改编译器。
  副作用：这些名字不能当函数值用（`operatorValueGraphOf` 走的是 `resolve`，而 resolve 恒为空），`camel inspect --ops`、悬停也拿不到签名。
- 优雅的形式（二选一，或分两步）：
  1. 通用的"按类型展开"扩展点：resolver（或算子表）可以声明"在建图时按实参类型展开为一段普通子图"，编译器只提供一个 `expand(types) -> 子图` 的接口和建 ACCS/CALL/FILL 的工具；`tree` 模块用这个接口实现自己的五个操作。这和已有的"定义图"（b0c0bf8d，`declareDefinition`）是同一个方向，只是定义图由类型生成。
  2. 长期：语言提供在类型上递归的手段（类型函数里的结构匹配，第 6 节"尚未决定"），`tree_map` 用 Camel 写在标准库里。
- 严重度：高。
- 需要语言作者裁定：是。问题："`tree_map` 这类'形状由类型决定、编译期展开'的操作，应当是 (a) 模块可以注册的通用展开钩子，(b) 等类型函数能在结构体/元组上递归后用 Camel 写，还是 (c) 接受为编译器内建？例如以后要支持 `tree_map` 遍历 `Key` 叶子或数组内部节点（T1），按 (a) 只改 `tree` 模块，按 (c) 要改 `src/compile/gir/tree.cpp`。"

### 2. "带类型实参的原生算子"同时有四种做法

同一需求（调用处的类型决定原生算子做什么）在这一周出现了四种互不相同的机制：

| 算子 | 做法 | 位置 |
|---|---|---|
| `tree_*` | 编译器按 URI 字符串识别并展开 | 见第 1 条 |
| `tensor::from_bytes::<T>` | `resolveForCall` 在编译期按类型选 URI；每个 dtype 一个内核、一个 URI，列表写死 `f32/i64/bool` | `modules/tensor/ops/binary_ops.cpp:53`、`:127-129`（1a5517ad） |
| `nn::load_tree::<T>` | `resolveForCall` 只定结果类型；内核在运行时从新加的 `ArgsView::resultType()` 读类型，再按类型递归解码 | `modules/nn/src/checkpoint.cpp:332`、`:351`；`include/camel/core/operator.h:55-62`（13d4152a） |
| `json::decode_as` | 仍是"形状值"：把一个值的类型当目标类型，内核运行时读 `with.type(0)` | `modules/json/module.cpp:121-148`、`modules/json/operators.cpp:495` |

- 为什么不对：
  - 2.13 明确说"有了这两层之后，`decode_as` 以'形状值'给出目标类型的写法退掉"。`::<T>` 已经实现，`decode_as` 没迁（type-arguments.cn.md 末尾自己也承认）。
  - `resultType()` 是一条新的"类型交给内核"的通道，要求每个后端在调用前 `setResultType`。这次改了 nodevm、fastvm、taskflow、generic 优化遍、onnx 导出器共 5 处；`std::macro` 的静态求值（`src/execute/macro/eval.cpp:635`）和 `std::cpp` 的桥（`src/execute/standalone_bridge.cpp:60` 的 `ArgsOfCall`）没有设置，在那里被调用的、依赖 `resultType()` 的算子拿到的是空指针（`load_tree` 会报"backend did not supply the static checkpoint result layout"；它是有效应的，`std::macro` 目前不会求值它，但下一个依赖这条通道的纯算子就会碰上）。这是每加一个后端都要记住的义务，靠人记。
  - `from_bytes` 的 dtype 列表是封闭的，`::<i32>`、`::<f64>`、`::<bf16>` 都要再写内核、再加 URI；结果类型在 resolver 里和 `OpDef.infer` 里各算一遍（`binary_ops.cpp` 的 `resultType` 被两处调用）。
  - 一天之内两次升模块 ABI（`resolveForCall` 虚函数 → 2，`ArgsView::resultType_` → 3，`include/camel/core/module/dynamic.h:31`），各为一个功能单独开口。
- 优雅的形式：一条规则。类型实参只在编译期参与 resolver 选择，选择的结果是 OPER 节点上已有的具体函数类型（节点的 `dataType` 已经是它）；内核需要知道"本次调用的静态签名"时，从节点的签名这一处统一拿到（由运行时框架在一个地方提供，而不是每个后端各自 `setResultType`）。`from_bytes` 可以是一个内核读签名里的 dtype；`decode_as` 迁到 `::<T>` 与期望类型；`tree_*` 见第 1 条。
- 严重度：高（它决定今后所有"泛型的 C++ 算子"怎么写，也决定模块 ABI）。
- 需要裁定：部分。技术形式可由实现方定；需要确认的是："原生算子的内核能不能在运行时读到本次调用的静态类型（例如 `load_tree` 按 `State` 的结构递归解码）？还是要求类型只在编译期选内核、内核不看类型？"（2.7 的"Camel 没有在运行时判断类型的能力"与 2.9"运行时不需要类型信息"都和这个问题有关。）

### 3. `string` 与 `Bytes` 的边界没定：`string` 仍能按字节构造非 UTF-8，边界处再报故障

- 位置：`src/builtin/operators/ops.cpp:835-846`；`stdlib/signatures/str.cml:122-127`；`stdlib/signatures/bytes.cml:5`、`fs.cml:14`、`:59`（dd0d6176）；`include/camel/core/type/base.h:75`。
- 现在的做法：
  - 2.16 定了"新增 `Bytes`，`string` 只表示文本"。但 `'中'[0]` 仍返回只含一个字节的字符串（`ops.cpp:845-846`，注释还写着"there is no byte type in the language"），`str::from_bytes([255])` 文档写明"The result need not be valid UTF-8"，`str::is_valid_utf8(from_bytes([255]))` 是参考文档里的例子。
  - 新接口在边界检查：`bytes::encode` "fault `InvalidArgument` when legacy string content is malformed UTF-8"，`fs::write_text` 遇到非法 UTF-8 也是故障。
  - core 里还留着一个没人从源码用得到的原始类型 `TypeCode::Byte`（名字 "byte"），而 `Bytes` 是另起的 `OtherType`。`Bytes` 这个类型又由 `fs` 与 `bytes` 两个模块各导出一次（`modules/fs/module.cpp:15`、`modules/bytes/module.cpp:107`）。
  - `str::from_bytes(int[]): string` 与 `tensor::from_bytes::<T>(Bytes, shape)` 同名不同义。
- 为什么不对：同一个事实（字符串是不是一定合法的 UTF-8）现在由"约定 + 若干边界检查"维持，而不是 `string` 的不变量。结果是同一段字节，`s[0]` 合法，`encode(s[0])` 和 `write_text(p, s[0])` 是故障；而 2.9"错误与故障"的划分要求哪些操作可能失败在签名上就看得出。
- 优雅的形式：先定不变量，再收口。若 `string` 一定是合法 UTF-8：按字节的下标与 `substr` 返回 `Bytes` 或只在码点边界上合法，`str::from_bytes`/`str::bytes` 退给 `bytes` 模块，`encode` 变成全函数；若不是：`string` 只是"按约定是文本"，`encode`/`write_text` 不该是故障。`TypeCode::Byte` 删除或与 `Bytes` 统一。`Bytes` 作为内建类型名（像 `string`、`Map`）还是模块类型，二选一。
- 严重度：高（数据模型）。
- 需要裁定：是。问题："`string` 是否保证是合法的 UTF-8？例如 `let c = '中'[0]`：(a) 仍得到一个只含字节 0xE4 的字符串，之后 `encode(c)` 是故障；(b) `'中'[0]` 本身是故障或返回 `Bytes`；(c) 下标改为按码点。另外 `Bytes` 是像 `string` 一样的内建类型名，还是要从 `bytes` 模块导入？"

### 4. "什么是树、什么是叶子"写了五份，已经出现分歧

- 位置：
  - `src/compile/gir/tree.cpp:26`（`internal`）——tree_* 展开；
  - `modules/tiles/src/device.cpp:72`（`distributeDevice`）——`Device<T>` 规范化（1b724238）；
  - `modules/tiles/src/tree_transfer.cpp:27`（`transferTreeType` 与 `walk`）——upload/download；
  - `modules/tiles/src/module.cpp:399`（`supportedLeaves`）——`@gpu` 的签名检查；
  - `modules/nn/src/checkpoint.cpp:57`（`internal`，与 gir 版逐字相同）——检查点（13d4152a）。
- 已经出现的分歧（都已核对）：
  - `supportedLeaves` 不检查 `isTagged()`/`isCell()`，会把 `T?`、`T!` 的内部元组当树递归；其余四处都把它们排除在内部节点之外。
  - `supportedLeaves` 拒绝空元组/空结构体；`tree_map` 接受。
  - 根与路径的写法：`tree_paths` 的根是 `""`，tree 诊断与检查点错误是 `"$"`，`Device` 的错误是 `"$.a.b"`，检查点文件名是 `"_root"`。设计稿 8.2 要求检查点"以 `tree_paths` 命名"，两边是各自拼的。
  - 叶子集合各不相同：`Device<…>` 接受任何 dtype 的张量，而 `upload` 只接受 f32 张量与 `float`；检查点另有一套 `kind()`。
- 为什么不对：2.16 把"树的形状由类型决定、叶子顺序深度优先按字段声明顺序"定为语义（"顺序是语义的一部分，不得由实现改变"）。同一个语义事实分散在 5 处、3 个模块里，下一次加 `Key` 叶子或数组内部节点（T1）要改 5 处，而且已经不一致。
- 优雅的形式：在 core 的类型层提供唯一的树视图（`core::type::tree`：是否内部节点、子项的名字与类型、规范路径的拼法、按规范顺序遍历），各模块只决定自己接受哪些叶子。`tree_paths`、检查点文件名、诊断路径都用它。
- 严重度：高（会随训练基础设施一起长）。
- 需要裁定：一处。问题："路径的规范写法是什么？根是 `""` 还是 `$`？元组下标写作 `blocks.0` 还是 `blocks[0]`？检查点文件名是否必须与 `tree_paths` 的结果逐字一致？"

---

## 中

### 5. 重载谁"更具体"由 resolver 的 C++ 类决定；内建算子组按注册顺序取第一个

- 位置：`src/compile/gir/builder.cpp:3240`、`:3375`（1c3c693f）；`src/compile/gir/type_arguments.cpp:28-34`（1a5517ad）；`include/camel/core/operator.h:122-132`、`:141-153`。
- 现在的做法：

  ```cpp
  const auto *fixed = dynamic_cast<const StaticFuncTypeResolver *>(entry.second.get());
  return fixed != nullptr && exactFor(fixed->funcType()) && ...
  ```

  内建算子用 `StaticFuncTypeResolver` 写的，用户的同签名重载是冲突；用 `DynamicFuncTypeResolver` 写的，用户重载"更具体"、悄悄胜出。`signatureForArgumentContext` 也只对 `StaticFuncTypeResolver` 给实参期望类型，所以 `from_bytes(...)` 的实参拿不到期望类型。
  `OperatorGroup::resolve` 与新加的 `resolveForCall` 都是"按注册顺序第一个接受的"，重叠的动态规则由注册顺序决定。
- 为什么不对：2.13 定的是"用户的重载比内建的更具体时优先"，这是签名之间的关系；现在变成"模块作者用了哪个 C++ 类"。模块作者把一个静态签名改写成动态规则（比如为了加形状检查），用户程序的语义就变了。2.12"同一层里同名的函数合成重载集，签名相同是错误"对内建组也不成立。
- 优雅的形式：`FuncTypeResolver` 声明自己接受的参数模式（具体类型或带类型变量的签名，可选），特异性按模式之间的包含关系判断；拿不到模式的规则一律视为"一族类型"。内建组内部的重叠在注册时报错，而不是按顺序取。
- 严重度：中。
- 需要裁定：否（2.13 已定方向），但应当确认"内建组内部两个规则都接受同一组实参"是注册错误。

### 6. 期望类型有三条互不相通的通道，覆盖的位置各不相同

- 位置：`src/compile/gir/builder.cpp:5240-5241`、`:5304-5313`、`:6128`、`:3451`、`:7572`（`expectedValueType_`，1a5517ad）；`coerceTo` `:6273`；`genericValueFor` `:1285`。
- 现在的做法：
  - `expectedValueType_` 在建子表达式之前往下推：只在 `as`/带类型的 `let`（`visitCastNode`）、`return`、已知签名的实参处设置，进入调用后清空。
  - `coerceTo` 在建好之后往上补：实参、带类型的 `let`、数组元素、分支两臂、循环状态。
  - `genericValueFor` 为泛型函数值另做一次延迟实例化。
  于是"目标类型已知"的位置在三处各列一遍，彼此不同：`let xs: int[][] = [empty()]` 中数组元素对 `coerceTo` 是已知目标，对 `expectedValueType_` 不是；结构体字段、分支臂也是（type-arguments.cn.md 末段承认）。
- 为什么不对：2.13 把第一层说成"2.10'泛型延迟到上下文能确定时实例化'的延伸"，即同一个概念。三套各自增长，每加一个位置要改三处，漏一处就是"这里能推、那里不能推"。
- 优雅的形式：一个 `check(expr, expected)` 入口（双向类型检查的标准形状），装入 `T?`/`T!`、空数组元素类型、泛型函数值、只出现在返回类型里的类型变量都在这里按同一份"期望类型"处理；各语法位置只负责把期望类型传进来。
- 严重度：中（不改语义，但决定以后推断能力怎么长）。
- 需要裁定：否。

### 7. 就地改写有两套：`append` 按名字写死，`sort` 走通用的 `setInPlaceForm`

- 位置：`src/compile/gir/builder.cpp:4367-4460`（`rebindingAppendOf`，b6ba66a8）对比 `:4471` 起的 `inPlaceRebindingOf`（f60d70d0）；声明处 `modules/algo/module.cpp:173`。
- 现在的做法：

  ```cpp
  !isDref(binding->at(0), "append")) {
  ...
  return overload.first == ":struct/append_arr";
  ...
  getExportedEntity(Reference("__assn_append__"))
  ```

  `sort` 那次提交已经把机制做成通用的（算子在算子表里声明自己的就地形式），却没有把最初的 `append` 迁过去；两者条件也不同（`inPlaceRebindingOf` 只在 `sync` 块里、只对数组；`append` 不要求 `sync`）。
- 为什么不对：同一个优化两种机制；而且两者都在前端按语法形状 `xs = xs->op` 改写成 `:var/take`/`:op/assn_owned` 等节点，图的形状因"算子有没有就地形式"而不同。2.12 说就地改写"只是实现层面的优化……不进入语义"，第 4 节说优化属于遍。
- 优雅的形式：`append` 用 `setInPlaceForm` 声明；长期把"旧值不再被使用时就地做"放进一个遍（按图上的引用计数/唯一性分析），前端只产出规范的 `xs = op(xs)`。
- 严重度：中。
- 需要裁定：否。

### 8. 复合赋值两套语义；运算符符号与组名的对应写了至少四份

- 位置：`src/compile/gir/builder.cpp:4327-4352`（`compoundAssignmentByRebinding`，f4ad727b）；`src/builtin/modules/globals.cpp:418` 起的 `__assn_add__` 等；`src/core/operator_names.cpp:47`（`kForms`）、`:149`（`operatorSymbolOf`）；`src/compile/gct/builder.cpp:1643-1677`；`src/parse/ast/builder.cpp:1670-1990`。
- 现在的做法：`a += b` 对内建类型调用单独注册的 `__assn_add__` 组（另一张按类型列的重载表，`:op/assn_add_i/l/f/d/s` 对应 `:op/add_i/l/f/d/s`）；只有模块定义的类型或程序重载了 `+` 时才改写成 `a = a + b`：

  ```cpp
  if (target == nullptr ||
      (!target->isOtherType() && !hasProgramOperatorOverloads(plain->second))) {
      return nullptr;
  ```

  `whyNotOverloadable` 对 `+=` 的解释却是"'a += b' is 'a = a + b', so it follows an overload of '+'"。"`+` 对哪些类型有定义"与"`+=` 对哪些类型有定义"是两张手写的表。
  符号到组名的对应在 `kForms`、`operatorSymbolOf`、GCT 的 `makeDRef("__add__")` 分支、`kPlainOperator` 各写一次。
- 为什么不对：2.11 定了"`a &= b` 即 `a = a & b`"、2.10 定了张量的 `t += u` 是重新绑定；同一个运算符在不同类型上走两条路，靠两张表保持一致。
- 优雅的形式：GCT 一律把 `a op= b` 降为 `a = a op b`；`__assn_x__` 内建若只是为性能，改为 `__x__` 的就地形式（同第 7 条的机制）。符号表只留 `operator_names.cpp` 一份，GCT 与 GIR 都查它。
- 严重度：中。
- 需要裁定：否（2.11 已定）。

### 9. 检查点：放在 `nn`；类型静态已知却在运行期才报不支持的叶子；归为文件系统故障

- 位置：`modules/nn/src/checkpoint.cpp:143`、`:300-325`、`:384-399`（13d4152a）。
- 现在的做法：`save_tree` 的类型规则对第二个参数什么都接受（`DynamicFuncTypeResolver`，只检查目录与可选的哈希是字符串）；数组、映射、函数等叶子在运行时 `snapshot` 里才 `mismatch(path, "unsupported checkpoint leaf type ...")`，再被包成 `RuntimeDiag::FileSystemError` 故障。`load_tree::<T>` 同样在运行期才发现 `T` 里有不支持的叶子，给 `CheckpointError` 错误值。
- 为什么不对：叶子类型在编译期完全确定（2.16"树的形状由类型决定"）；2.7 的精神是编译期能知道的就在编译期报（Device 那边就是"否则编译错误并指出路径"）。"写出了一个函数值"被说成文件系统故障，也不符合 2.9 对错误与故障的区分。保存/载入一棵任意的树是通用的数据持久化，与神经网络无关（第 1 节"不限于 AI"），放在 `nn` 里，用户要为存一个配置结构体导入 `nn`。
- 优雅的形式：resolver 用第 4 条的统一树视图在编译期检查叶子并指出路径；检查点放在 `tree` 或 `fs` 一侧（或单独的 `checkpoint` 模块）。
- 严重度：中。
- 需要裁定：模块归属可以问一句："`save_tree`/`load_tree` 属于 `tree`、`fs` 还是 `nn`？"

### 10. 算子的同一组事实写在三处：C++ 注册、算子表声明、`stdlib/signatures/*.cml`

- 位置：例如 `fs`：签名在 `modules/fs/module.cpp`（`StaticFuncTypeResolver::create(...)`），纯度在同文件 `OperatorTable::declare("fs", {{"read_bytes", false}, ...})`，资源在 `declareResources`，再在 `stdlib/signatures/fs.cml` 写一遍签名、`@effect`、`@fails`（b71f4b18、712bb97c、dd0d6176）。
- 现在的做法：`.cml` 文件头说"For reference only: these declarations are not compiled. They are checked against the operators the module registers"，校验只覆盖列出的模块与名字。新加的 `tree`、`nn::save_tree/load_tree`、`tensor::from_bytes/to_bytes` 没有签名文件，也不在 `docs/stdlib-reference.md` 里。`tree` 为了满足"每个算子都声明纯度"的测试，给永远不成为节点的名字声明了 `false`。
- 为什么不对：签名、纯度、效应、可能的故障是同一个算子的事实，三处手写，靠测试比对；这正是"同一事实两处维护"。
- 优雅的形式：一个来源。要么 `.cml` 的 `inner func` 声明是权威（C++ 只提供内核与不能用签名表达的类型规则，纯度/效应从注解读），要么 C++ 注册是权威、`.cml` 与参考文档都生成出来。
- 严重度：中。
- 需要裁定：否。

### 11. `crypto::sha256` 两个重载共用一个 URI，内核在运行时按类型分派

- 位置：`modules/crypto/module.cpp:33-37`；`modules/crypto/operators.cpp:44`（dd0d6176）。
- 现在的做法：

  ```cpp
  {{"crypto:sha256", StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String())},
   {"crypto:sha256", StaticFuncTypeResolver::create({}, {{BytesType::get(), false}}, Type::String())}}
  ...
  const auto input = camel::core::type::isBytesType(norm.type(0)) ? ... : ...;
  ```

  `OperatorGroup` 构造时把每个 resolver 按 URI 发布进算子表（`operator.h:164-170` → `OperatorTable::setResolver`），第二个覆盖第一个，表里按 URI 查到的只有 `Bytes` 版。
- 为什么不对：URI 是算子的身份（纯度、语义、定义图、缓存都按它挂），一个 URI 两个签名破坏了这一点；重载本来在编译期已经选定，内核再按类型分派是多余的运行时类型判断。
- 优雅的形式：`crypto:sha256_str`、`crypto:sha256_bytes` 两个 URI（与 `:op/add_i`/`:op/add_l` 的惯例一致）；算子表对重复 URI 在注册时报错。
- 严重度：中（单点，但暴露出算子表不防重复 URI）。
- 需要裁定：否。

---

## 低

### 12. 编译器按名字给 `error` 注入调用位置；`foreach` 按 URI 列表检查被丢弃的值

- 位置：`src/compile/gir/builder.cpp:5318-5330`（17ac776a）；`:5832-5833`（16bb14ac）。
- 现在的做法：任何名为 `error` 的算子组、恰好两个实参时，编译器补第三个实参"源码位置"；`uri == ":mark/foreach_arr" || uri == ":mark/unordered_foreach_arr"` 时检查元素函数的值被丢弃。
- 为什么不对：都是"算子的性质"写成编译器里的名字表。第三方模块里另一个名为 `error` 的算子会被意外补参；`para` 或用户模块里另一个"丢弃回调结果"的遍历算子拿不到这条检查。2.12 已经有"算子表声明会调用子图、是否可能故障"的先例。
- 优雅的形式：算子表上的两个声明——"接收调用处的位置"（类似 Rust 的 `#[track_caller]`，以后 `assert`、`attempt` 也能用）与"丢弃回调结果"。
- 严重度：低。需要裁定：否。

### 13. 显式类型实参的顺序取"类型变量在签名中首次出现的顺序"

- 位置：`src/compile/gir/type_arguments.cpp:63-77`；`docs/language/07_statements.md`（1a5517ad）。
- 现在的做法：`collectTypeVars(tmpl->funcType, names)` 的顺序就是 `f::<A, B>` 的顺序（`with` 形参在前）。
- 为什么值得问：Camel 没有 Rust 那样的泛型参数列表，`typeas T` 就地绑定（2.9）；于是调换两个形参的位置、或把一个形参改成 `with` 形参，会悄悄改变已有调用 `f::<int, string>` 的含义。2.13 只定了写法 `f::<T>`，没有定顺序。
- 需要裁定：是。问题："`func pair(b: typeas B, with a: typeas A)` 写 `pair::<int, string>` 时，`int` 绑到 `A` 还是 `B`？是否需要一种不依赖位置的写法（如 `pair::<A = int>`），或者要求只有一个类型变量定不下来时才能写 `::<>`？"
- 严重度：低（现在调用点少，越晚定越难改）。

### 14. `std::macro` 的循环上限写死，与 2.14 不符

- 位置：`src/execute/macro/eval.cpp:72-74`。
- 现在的做法：`constexpr size_t kMaxMacroLoopRounds = 1'000'000;`，注释仍写"the bound is a question to the language author, this is the narrow reading meanwhile"。2.14 已定"至多 100 万轮（与递归上限一样可调）"。
- 优雅的形式：与 `--max-recursion-depth` 同一套配置。严重度：低。需要裁定：否。

### 15. 语言服务器另有一套词法作用域；类型与命名空间的引用不与编译器核对

- 位置：`src/service/semantic/queries.cpp:1620-1645`（b78c6976）。
- 现在的做法：重命名时值引用同时按 LSP 自己的 `file.syntax.lookup` 和编译器记录的事实判断，不一致就拒绝；类型与命名空间"by scope alone"，只用 LSP 自己那一套。
- 为什么值得注意：2.12 的名字优先级（局部 > 本模块 > 按名导入 > `using namespace` > 内建）是在编译器里实现的，LSP 重新实现一遍且对类型不核对，两者以后可能悄悄分歧。
- 优雅的形式：编译器的记录器也报告类型与命名空间的解析结果，LSP 只用它。严重度：低。需要裁定：否。

### 16. `print<fmt>` 的格式串是字面量，却只在运行时检查

- 位置：`src/builtin/operators/io.cpp`（`writeArguments`，9b2b90ce）。格式与实参个数不符是运行期 `FormatFailed` 故障。格式串几乎总是常量，按 2.7"编译期能知道的就在编译期报"的精神可以在 resolver 里用静态实参检查。严重度：低。

---

## 检查过、认为合理的部分

- `@gpu`（`modules/tiles/src/module.cpp:505` 起）是普通的库算子，经通用的装饰器机制在宏期应用（`builder.cpp:4930-5000`）；这正是 `@numeric` 应当采用的形状。
- C++ 模块导出值（10d2260a）：`Entity` 能持有常量、`Module::exportValue`，`math::pi` 在图上等同字面量；是通用机制，不是对 `math` 的特例。
- 关键字只有一个来源 `include/camel/parse/syntax/token_kinds.def`，LSP 与词法器共用。
- `::<T>` 不产生运行时类型值；`f::<T>` 作函数值复用实例缓存；三条实例化途径（推断、函数值、显式）的合法性检查（`int??`、映射键）已统一到 `instantiateGeneric`。
- `typeas` 允许写在返回类型、由期望类型推出，符合 2.13 第一层。
- `Device<T>` 在类型构造时规范化为逐字段的 `Device`（`OtherType::cloneWithParams` 可返回结构类型），符合 2.16"同一个类型，不是可互相转换的两个类型"。
- tree_* 展开后只有普通的 ACCS/CALL/FILL 节点，叶子边界取自树实参而非回调，左折叠顺序固定：语义符合 2.16；问题只在放置（第 1、4 条）。
- `sort` 的就地形式经 `OperatorTable::setInPlaceForm` 声明，是正确的通用形状（第 7 条只批评 `append` 没迁）。
- 运算符重载：`&&`、`||`、`??` 等不可重载并给出原因表；重载 `==` 即得 `!=`；命名空间块里的运算符属于模块：都与 2.11–2.13 一致。
- `do` 式循环头经 AST、GCT 到 `LOOP` 节点，多状态为元组：与 2.13 一致。
- `print`/`println` 带格式与不带格式共用一个 `writeArguments`，`std::cpp` 的运行时同步修改；返回第一个实参，与改动前一致。
- `Bytes` 是不可变值；`read_chunk` 改返回 `Bytes`（T5）；`fs::replace` 原子替换、`write_*` 保持原行为：与设计稿 8.1 一致（但见第 3 条的边界问题）。
- `json::decode` 对坏文本返回 `Json!` 错误值（2.13）。
- 编译器里对 `main` 的特殊处理（入口、LSP 拒绝重命名 `main`）是语言规则本身，不算特例。
