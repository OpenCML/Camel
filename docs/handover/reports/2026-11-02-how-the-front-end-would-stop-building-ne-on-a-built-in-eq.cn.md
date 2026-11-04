# `!=` 的前端：机制已画清，改法已定，未落地

## 三句（交班用）

1. **`negated` 只在"`==` 是程序声明的重载"那条路上被读**——`builder_operators.cpp:2795-2797` 的 `negateResult = negatedEqualities_.contains(targetNode)` 写在 `if (operatorOverload != nullptr)` **之内**；
2. **内建 `__eq__` 那一支因此需要自己就地构造取反**（在 DREF 之上盖一个 `__not__` 调用），**不能"记一笔等别人来包"**；
3. **`negatedEqualities_` 有读也有写，但读者只服务声明重载那一支**：把它用于内建组 = 写进一个那一支不读的账。

## 四处命中（读写分明）

```
src/compile/gir/builder.h#Builder            std::unordered_set<node_handle_t> negatedEqualities_;   // 声明
src/compile/gir/builder_operators.cpp#Builder::addDeclaringModuleOverloads  negatedEqualities_.insert(dref);                        // 写
src/compile/gir/builder_operators.cpp#Builder::operatorDrefOf  negatedEqualities_.insert(drefNode);                    // 写
src/compile/gir/builder_operators.cpp#Builder::visitLinkNode negateResult = negatedEqualities_.contains(targetNode); // 读（仅声明重载支）
```

## 两次红的读数（都干净回退）

**第一次**——删注册 9 条（`:op/ne_i/l/f/d/b/s/bytes/c/z`，63 行）：

```
total: 8390, pass: 7866, fail: 508
[FAIL] array_whole.nvm / .fvm / .jit / .prl
```

**第二次**——`operatorDrefOf` 内建支改成 `set.ops = 内建 __eq__ 组 + negated = true`（**不经取反构造**）：

```
total: 8390, pass: 7994, fail: 380
[FAIL] permissions.strict_algebra / module_import_builtins / branch_arm_behavior / registry_sweep_unique_arm
```

**第二次红的名字（`permissions.strict_algebra` 这种）正说明"语义整体翻转"而不是"某一族坏"**——与"`negated` 那一支没人读"完全吻合。

## 改法（下一轮照此，不必再量）

1. `operatorDrefOf` 内建组那一支：`set.ops = operatorOverloadsOf("__eq__").ops`（**借用**：`ops` 是 `oper_group_ptr_t`，算子表比这次 DREF 活得久，赋值只加一个引用）；
2. 返回前，**若 `negated` 为真且走的是内建那一支**，就地在这个 DREF 之上构造 `__not__` 调用（`kForms`: `{"!", 1, 1, "__not__"}`；内核 `:op/not`，已注册于 `builtin.cpp:345`）；
3. **注释写死两条**：`a != b` 就是 `!(a == b)`，内建与声明的一视同仁；**两侧表达式各只建一次，不许复制一份来写取反**。

**判据三条**（先只跑 `1 != 2` 的 `std::gir`）：`grep -oE "op/(ne|eq)[a-z_]*" | sort | uniq -c` ⇒ **`eq_l` 一次、`ne_l` 零次**；取反节点那一行 `type=bool`。**过了才跑全量，期望 `8390/8374/0`。**

## 基线（改前只存在一次）

```
func side(tag: string, v: int): int sync { println(tag)  return v }
let same = side("L", 1) != side("R", 2)   ⇒  输出 L / R / true      （各一次、先左后右）
```

## 数：作者的 11 = 9 + 2

`globals.cpp` 里 `:op/ne_*` 是 **9 条**（`i/l/f/d/b/s/bytes/c/z`，63 行）；另两条是 **`op/ne_bf`、`op/ne_f6`**（bf16/f16，不同组，在 `builtin.cpp` 的执行器映射里）。**11 = 9 + 2，别再让这两个数在下一个人手里变成"哪个对"。**

## 这条线的账

**六轮**：定位发射侧 → 记副作用基线 → 查所有权（`ops` 是借用）→ 查 `negatedEqualities_` 的读者 → 两次改动两次红 → 两次干净回退。**树始终是绿的**（`432359f2c`，全量 `8389/8376/0`，`inspect --ops` 649，三栏 481/22/146，`--verify-ops` 552）。**没有未知项，剩下的只是动手。**

`等点名`。