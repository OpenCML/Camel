# 「写死 `float64`」清单与验收尺子（审查 P1-9）

> **这张表是验收尺子**：每一行给一个可 `grep` 的锚（`文件:行` + 那句原文）。改一处划掉一处，每笔提交后 `git grep 'Float64()'` 复核**不出现表外的新写死**。
>
> **生成方式**：`git grep -n 'Float64()' modules/transforms/transpose.cpp modules/transforms/jvp.cpp`（这份表就是它的逐行展开，行号以合并 `a916f2ee0` 后的树为准）。
>
> **三分类**（每行在"归类"列打勾）：
> - **A 应问 `tangentSpaceOf`**：这句在判断"这个值有没有切向/切空间是什么"，答案属于类型，不该写死。
> - **B 应按节点类型实例化**：这句在造一个签名/端口，类型应从**节点或参数自己的类型**来，不该写死。
> - **C 本来就该是 `float64`**：这句的 `float64` 是语言事实（例如"余切灌入的种子是 `float`"），**必须在理由列写清为什么推不出**。
| `transpose.cpp:41` | `if (function->exitType() != Type::Float64()) {` | |
| `transpose.cpp:71` | `if (function->normTypeAt(i) != Type::Float64()) {` | |
| `transpose.cpp:75` | `cotangentsOfParticipants.push_back(Type::Float64());` | |
| `transpose.cpp:77` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:83` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:100` | `if (function->normTypeAt(i) != Type::Float64()) {` | |
| `transpose.cpp:103` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:104` | `cotangentsOfAll.push_back(Type::Float64());` | |
| `transpose.cpp:106` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:128` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:137` | `if (count != 1 || function->normTypeAt(0) != Type::Float64()) {` | |
| `transpose.cpp:141` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:142` | `params.emplace_back(Type::Float64(), false);` | |
| `transpose.cpp:143` | `return FunctionType::create(camel::core::type::param_vec_t{}, params, Type` | |
| `transpose.cpp:248` | `: Type::Float64();` | |
| `transpose.cpp:259` | `draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64` | |
| `transpose.cpp:263` | `const gc_node_ref_t made = draft.addOperNode(Type::Float64(), nullptr, uri` | |
| `transpose.cpp:269` | `camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),` | |
| `transpose.cpp:276` | `std::vector<Type *> partTypes(parts.size(), Type::Float64());` | |
| `transpose.cpp:886` | `? Type::Float64()` | |
| `transpose.cpp:888` | `std::vector<Type *>(answerCount, Type::Float64()));` | |
| `transpose.cpp:907` | `draft.addAccsNode(Type::Float64(), static_cast<uint32_t>(i));` | |
| `transpose.cpp:1161` | `Type::Float64(), rt::kInvalidNodeRef, static_cast<rt::gc_cnt_t>(arms.size(` | |
| `transpose.cpp:1410` | `draft.addPortNode(Type::Float64(), draft.allocateRuntimeSlot(Type::Float64` | |
| `transpose.cpp:1537` | `Type::Float64(), draft.allocateRuntimeSlot(Type::Float64()));` | |
| `transpose.cpp:1545` | `Type *loopType             = Type::Float64();` | |
| `transpose.cpp:1606` | `const gc_node_ref_t part = draft.addAccsNode(Type::Float64(), index);` | |
| `transpose.cpp:1745` | `? Type::Float64()` | |
| `transpose.cpp:1747` | `std::vector<Type *>(calleeParams, Type::Float64()));` | |
| `transpose.cpp:1757` | `calleeValue = draft.addFuncNode(transposed, Type::Float64());` | |
| `transpose.cpp:1789` | `const gc_node_ref_t part = draft.addAccsNode(Type::Float64(), static_cast<` | |
| `transpose.cpp:1826` | `partTypes.push_back(Type::Float64());` | |
| `jvp.cpp:366` | `context, ruleId, std::vector<camel::core::type::Type *>{Type::Float64()}, ` | |
| `jvp.cpp:541` | `auto *lifted = TupleType::create(std::vector<Type *>{Type::Float64(), Type` | |
| `jvp.cpp:660` | `Type *joinType = join->dataType() == Type::Float64() ? lifted : join->data` | |
| `jvp.cpp:762` | `const bool carried                     = node->dataType() == Type::Float64` | |
| `jvp.cpp:767` | `const gc_node_ref_t half = draft.addAccsNode(Type::Float64(), 0);` | |
| `jvp.cpp:769` | `const gc_node_ref_t halfTangent = draft.addAccsNode(Type::Float64(), 1);` | |
| `jvp.cpp:799` | `if (node->dataType() == Type::Float64()) {` | |
| `jvp.cpp:802` | `camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),` | |
| `jvp.cpp:871` | `if (in != nullptr && in->dataType() == Type::Float64()) {` | |
| `jvp.cpp:906` | `const gc_node_ref_t half = draft.addAccsNode(Type::Float64(), 0);` | |
| `jvp.cpp:908` | `const gc_node_ref_t halfTangent = draft.addAccsNode(Type::Float64(), 1);` | |
| `jvp.cpp:933` | `if (in != nullptr && in->dataType() == Type::Float64()) {` | |
| `jvp.cpp:964` | `const gc_node_ref_t half = draft.addAccsNode(Type::Float64(), 0);` | |
| `jvp.cpp:966` | `const gc_node_ref_t halfTangent = draft.addAccsNode(Type::Float64(), 1);` | |
| `jvp.cpp:1057` | `if (node->dataType() == Type::Float64()) {` | |
| `jvp.cpp:1060` | `camel::core::rtdata::toSlot<double>(0.0), Type::Float64(),` | |
| `jvp.cpp:1264` | `if (in != nullptr && in->dataType() == Type::Float64()) {` | |

## 分类与进度

- 行数：`transpose.cpp` 32 处、`jvp.cpp` 17 处（合计 **49**），与审查说的 34+25 同量级（审查数的是另一轮的树）。
- **已完成**（第 4 步这一笔，`151212c7b`）：`jvp.cpp` 的端口构造那两处（值端口与切向端口）——**B 类，已改、已从尺子上划掉**。
- **已完成**（A 类第一处，本笔）：`transpose.cpp` 多参数分支的 100/103/104 三行（判型与两处 `Float64()` 槽）——参数按自己的类型进、余切槽按 `tangentSpaceOf(参数).space` 给；**种子那一处 `params.emplace_back(Type::Float64())` 保留**，C 类并写理由（`transpose` 关心的是数的函数，种子就是那个数）。三条用例 `transpose_many_parameters`、`.is_linear_in_the_cotangent`、`transpose_unread_parameters` 改后仍绿。
- **待办（A 类优先）**：`transpose.cpp` 的多参数分支（种子与余切槽）、`jvp.cpp` 的走图判型处。
- **`with` 参数那一半**（原则 9.9）：位置在 `jvpSignature`（`function->withTypesCount()` 的处理）与 `transposeSignature`（`withTypesCount() != 0` 直接 `nullptr`，见 `transpose.cpp:38`），**判据**：一个带 `with` 参数的函数被提升/转置时不再整体拒绝，而是按 9.9 走"捕获值不产切向、但仍是已知值"。

## 这张表为什么这样写

审查的原话是"**同一个事实写两遍、在症状处补一针、为了让测试过而加例外、新旧两套并存却没有终点**"。所以这张表**不许**长出表外的新写死：每一处要么归 A、要么归 B、要么给出 C 的理由。@
