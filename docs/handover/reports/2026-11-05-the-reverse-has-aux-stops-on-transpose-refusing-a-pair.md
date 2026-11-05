# 2026-11-05 反向的 `has_aux` 撞在 `transpose` 对二元组的拒绝上

**一句话**：作者 `cb47d540d` 给的写法 `let ((v, aux), lin) = split(reinterp<jvp>(f))(w)` →
`transpose((t) => lin(t).0)` 在 `1.0` 求值，**四版实测都停在同一处**：`transpose` 不接受返回二元组的
函数。这条拒绝不在 `stdlib`，在转置自己的判据里——所以「删掉二元组没有余切」是引擎层的活，不是库层一行。

## 四版与各自的读数（都在 `merge-train-2`、`develop = 6c4ac6f3e` 的树上量的）

| 写法 | 读数 |
| --- | --- |
| `let ((v, aux), lin) = reading(w)` | `stdlib/vjp.cml:141:14: Unexpected '('; expected a name`——`let` 不收嵌套元组模式。改成两步 `let (pair, lin)` + `let (v, aux) = pair`。 |
| `let value_direction = (t: float) => lin(t).0` 再 `transpose(value_direction)(1.0)` | `transforms: transpose has no transpose for this function: this call's callee has no transpose of its own, so the call cannot be turned around`——**lambda 体里对 `lin` 的调用无法转置**。这是「按值调用时被调的图要到运行时才知道」出现在**转置**那一侧。 |
| 同上，`transpose(...)(1.0)` 补 `.0` | 编译过，`has_aux.library.composition` 仍红（另两处）；先解决了 `Return type '(w: float) => ((float, float), (float,))' does not match …`。 |
| 按 9.9 逐成员、不给辅助值种子：`transpose(lin)((1.0, 0.0)).0` | `stdlib/vjp.cml:146:33: Invalid call to 'transpose' with argument types <> ((float) => (float, float)): transpose takes a function whose result has a cotangent: …` |

第四版在数学上正是作者写的「二元组的余切就是两个成员余切组成的二元组」，被拒的原因是**签名的判据只认
`float`/`f32` 结果**：`modules/transforms/transpose_operator.cpp` 的 `transposeSignature(...) == nullptr`
分支先答否，紧接着那句注释还写着「这是 `transposeSignature` 说 no 的**理由**，不是判据本身」。

## 由此得到的次序（相对作者的四步，多一个第 0 步）

0. **`transpose` 接受返回二元组的函数**：`transposeSignature` 与按成员累加余切的那条路；随后
   `has_aux.vjp_pair` 那条用例**改写而不是删除**（它钉的拒绝本来就不该存在，但「不存在」要用
   「能答出数」来钉，不是用删掉用例来钉）。
1. `first_with_as_param`：新算子 + resolver + kernel（搬图、把第一个 `with` 端口重定为普通端口、
   **后面 `with` 的端口序号整体前移一位**）。判据用 `<w1, w2>`、体内 `w1 + w2`、只移 `w1`、
   **`w2` 的值不变**，两个不同的数各调一次；只测一个 `with` 参数测不出这一错。
2. `grad(f) = vjp(first_with_as_param(f))` 取第一个参数余切。
3. `has_aux` 反向组合（上面的第四版写法，等第 0 步通了直接套）。
4. 改写 `with_value_call_is_not_lifted`，删 `jvp` 的 `with` 提升（`withIsPromoted` 四处调用点同一次删除）。

## 试过的改动没有留在树里

`stdlib/vjp.cml` 的四版改动**没有提交**，试完即 `git checkout --` 还原；补丁在仓库外
`D:\Projects\Camel-wt\hasaux-attempt.patch`。留下的只有这一页和 `reviews/` 那一页顶部的标记。