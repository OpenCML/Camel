# 泛型 `gate` 用例与它那个输入上的 `Cast`

Author: 协调者（合并闸门），2026-10-24。Status: open — 需要负责人裁定。测量由 `handover/h383-generic-body-literal`（`94371c2dc`）在 `handover/h383-generic-body-literal` 上给出；原始输出在提交信息与其用例里。

## 事实（三条，都实测）

1. **裁定里的写法今天写不出来。** `func gate::<T>(t: Tensor<T>)` 被拒：`The arguments of the type 'Tensor<...>' are not accepted: a type variable cannot be an argument of a module type`（`src/compile/gir/builder.cpp` 的模块类型检查；`TensorType` 没有放类型变量的槽，那是 h366 要重写的文件）。`T[..]` 能绑定，但 `f32[..]` 在需要 `Tensor` 的位置不被接受（P4 的别名缺口），于是张量算子接不上。**所以"把 `gate` 改成头部声明 `T` 的写法"这一步在 P4/h366 之前无法落地。**
2. **字面量那一半已经交付**（本次落地）：泛型实例里的张量字面量是 `value_type="f32"`；元素类型放不下的字面量现在报错并点名两者——`The literal '0.5' cannot be represented as type 'int': the value would be truncated`（`SemanticDiag::LiteralNotRepresentable`），红前 `Tensor<int>` 上 `x * 0.5` 会**静默算成 f32 乘积**（打 `5.0`）。泛型实例报在实例处：`… [in the instance 'half<Tensor<int64, [4]>>' of the generic function 'half' with T = …]`。
3. **范例里那一个 `Cast` 不在字面量上，在模型输入上。** 拿一个只有比较、没有任何张量字面量的程序 `gt(x: Tensor<f32>) = x[0] > 0.5`，`CAMEL_ONNX_DUMP=1` 的节点级导出是 `Cast[n_input_cast](input)->input_cast | Gather[n_Gather] | Greater[n_Greater] | Identity`——`Cast` 把 **f32 输入加宽到 f64**，因为 `t[0]` 是标量边界而比较在 f64 里做。这个 `Cast` 在普通 `Tensor<f32>` 写法、泛型实例、裸 `Tensor` 三种情形下都在；而**只含张量字面量的体不再导出 `Cast`**（导出 `Mul,Sub,Identity`，字面量是 f32）。所以(a)"导出里没有 `Cast`"这个验收项，改字面量规则或改头部写法都拿不掉——它问的是**比较的元素类型**。

另外两条已确认：泛型 `#gpu` 作用域现在能编译（h371 的拒绝已消失；宿主机卡被占满，设备读数仍欠）；裸 `Tensor` 行为按裁定的过渡条款未变（字面量仍是 `value_type="float"`，算子串仍是 `Cast,Gather,Greater,If,Identity`）。

## 选项与代价

1. **先落 P4/h366（`Tensor` 成为数值数组类型），再写 `Tensor<T>` 头部**：字面量那一半已经做完，头部写法届时是改写；代价是等翻转落地（它自己有 464 条红与"五个算子声明搬不搬"的决定）。范例的 `Cast` 仍然取决于下面的比较问题。
2. **保留过渡期写法**，接受范例算子串里的 `Cast` 到比较元素类型被裁定为止：代价是验收项(a)暂时无法勾；收益是不用等翻转。
3. **现在就裁比较的元素类型**（例如：`Tensor<f32>` 的标量边界 `t[0]` 是 f32，比较随张量元素类型，不再加宽到 f64）：这一条超出"泛型体字面量"的裁定范围，且会动标量边界与比较的类型规则——**不建议**在没有测量支持面时先裁（要动的是每个 `t[i]` 参与算式的类型，波及面比字面量大）。

## 建议

**选 1**，并把(a)的验收拆成两句：字面量那一半"实例里的字面量取元素类型、放不下就报错"**已达成**；`Cast` 那一半**取决于新的裁定**——`t[0] > 0.5` 在 `Tensor<f32>` 上该在 f32 里比较还是 f64（若选"随张量元素类型"，范例的算子串才有机会去掉 `Cast`，且这条要单独立项测量，因为它改的是标量边界的类型）。